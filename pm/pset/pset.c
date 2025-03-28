/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/ioctl.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>
#include <trurl/nmalloc.h>
#include <trurl/n_snprintf.h>


#include "vfile/vfile.h"
#include "sigint/sigint.h"
#include "i18n.h"

#include "depdirs.h"
#include "misc.h"
#include "log.h"
#include "pkg.h"
#include "capreq.h"
#include "pkgdir/pkgdir.h"
#include "pkgdir/source.h"
#include "pm_pset.h"
#include "pkgset.h"


#define IMMUTABLE_REMOTESRC (1 << 0)
#define IMMUTABLE_MULTISRC  (1 << 1)
#define AUTODIRDEP          (1 << 2)

struct pm_pset {
    char      *installer_path;
    tn_hash   *cnf;
    tn_array  *sources;
    unsigned  flags;
};


struct pm_psetdb {
    struct poldek_ts *ts;
    struct pkgset    *ps;
    char             *tsdir;       /* transaction temp directory */
    tn_array         *pkgs_added;
    tn_array         *paths_added;
    tn_array         *paths_removed;
    int              _recno;
    struct pm_pset   *pm;
};

void *pm_pset_init(void)
{
    struct pm_pset *pm;
    char path[PATH_MAX];

    pm = n_malloc(sizeof(*pm));

    if (vf_find_external_command(path, sizeof(path), "pset-pm.sh", NULL))
        pm->installer_path = n_strdup(path);
    else
        pm->installer_path = NULL;

    pm->cnf = n_hash_new(16, NULL);
    pm->sources = n_array_new(4, (tn_fn_free)source_free,
                              (tn_fn_cmp)source_cmp);
    pm->flags = 0;

    return pm;
}

void pm_pset_destroy(void *pm_pset)
{
    struct pm_pset *pm = pm_pset;

    n_cfree(&pm->installer_path);
    n_hash_free(pm->cnf);
    n_array_free(pm->sources);

    memset(pm, 0, sizeof(*pm));
    free(pm);
}


int pm_pset_configure(void *pm_pset, const char *key, void *val)
{
    struct pm_pset *pm = pm_pset;

    if (n_str_eq(key, "source")) {
        struct source *src = val;
        n_array_push(pm->sources, source_link(src));

        if (n_array_size(pm->sources) > 1)
            pm->flags |= IMMUTABLE_MULTISRC;

        if (source_is_remote(src))
            pm->flags |= IMMUTABLE_REMOTESRC;

    } else if (n_str_eq(key, "autodirdep")) {
        pm->flags |= AUTODIRDEP;
    }

    return 1;
}

int pm_pset_satisfies(void *pm_pset, const struct capreq *req)
{
    pm_pset = pm_pset;
    if (capreq_is_rpmlib(req))
        return 1;

    return 0;
}

static int setup_source(const struct pm_pset *pm,
                        struct pkgset *ps, struct source *src)
{
    struct pkgdir *dir;
    unsigned ldflags = 0;

    if ((dir = pkgdir_srcopen(src, 0)) == NULL) {
        if (!source_is_type(src, "dir") && util__isdir(src->path)) {
            logn(LOGNOTICE, _("trying to scan directory %s..."), src->path);
            source_set_type(src, "dir");
            dir = pkgdir_srcopen(src, 0);
        }
    }

    if (dir == NULL)
        return 0;

    if (pm->flags & AUTODIRDEP)
        ldflags = PKGDIR_LD_DIRINDEX;

    if (!pkgdir_load(dir, NULL, ldflags)) {
        pkgdir_free(dir);
        return 0;
    }

    if (!pkgset_add_pkgdir(ps, dir)) {
        logn(LOGERR, _("%s: failed to add to pkgset..."), source_idstr(src));
        pkgdir_free(dir);
        return 0;
    }
    return 1;
}


void *pm_pset_opendb(void *pm_pset, void *dbh,
                     const char *rootdir, const char *dbpath,
                     mode_t mode, tn_hash *kw)
{
    struct pm_pset *pm = pm_pset;
    struct pm_psetdb *db = dbh;
    struct pkgset *ps;
    int i, iserr = 0, recno;

    rootdir = rootdir; dbpath = dbpath; mode = mode;

    if (db)
        return db;

    n_assert(n_hash_exists(kw, "source") == 0);      /* use pm_configure() */

    if (n_array_size(pm->sources) == 0) {
        logn(LOGERR,
             _("Could not open 'pset' database: missing source parameter"));
        return NULL;
    }

    if ((ps = pkgset_new(NULL)) == NULL)
        return NULL;

    for (i=0; i < n_array_size(pm->sources); i++) {
        struct source *src = n_array_nth(pm->sources, i);
        if (!setup_source(pm, ps, src))
            iserr = 1;
    }

    if (iserr) {
        logn(LOGERR, _("no packages loaded"));
        pkgset_free(ps);
        return NULL;
    }

    recno = 1;
    for (i=0; i < n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
        pkg->recno = recno++;
    }

    /* pkgset_setup(ps, PSET_VRFY_MERCY); */
    db = n_calloc(1, sizeof(*db));
    db->ts = NULL;
    db->ps = ps;
    db->pkgs_added = pkgs_array_new(32);
    db->paths_added = n_array_new(32, free, NULL);
    db->paths_removed = n_array_new(32, free, NULL);
    db->tsdir = NULL;
    db->_recno = recno;
    db->pm = pm;
    return db;
}

void pm_pset_closedb(void *dbh)
{
    dbh = dbh;
    return;
}

void pm_pset_freedb(void *dbh)
{
    struct pm_psetdb *db = dbh;

    if (db == NULL)
        return;

    if (db->ps)
        pkgset_free(db->ps);

    if (db->pkgs_added) {       /* clean our recno's */
        int i;
        for (i=0; i < n_array_size(db->pkgs_added); i++) {
            struct pkg *pkg = n_array_nth(db->pkgs_added, i);
            pkg->recno = 0;
        }
        n_array_free(db->pkgs_added);
    }

    if (db->tsdir) {     /* remove transaction directory */
        int i;
        for (i=0; i < n_array_size(db->paths_added); i++) {
            char *path = n_array_nth(db->paths_added, i);
            DBGF("unlink %s\n", path);
            unlink(path);
        }
        rmdir(db->tsdir);
        n_free(db->tsdir);
    }

    n_array_free(db->paths_added);
    n_array_free(db->paths_removed);
    memset(db, 0, sizeof(*db));
    free(db);
}


/* remember! don't touch any member */
struct psetdb_it {
    int                  tag;
    int                  i;
    struct pm_dbrec      dbrec;
    tn_array             *pkgs;
};


static
int psetdb_it_init(struct pm_psetdb *db, struct psetdb_it *it,
                   int tag, const char *arg)
{
    int pstag = 0;

    switch (tag) {
        case PMTAG_RECNO:
            pstag = PS_SEARCH_RECNO;
            break;

        case PMTAG_NAME:
            pstag = PS_SEARCH_NAME;
            break;

        case PMTAG_FILE:
        case PMTAG_DIRNAME:
            pstag = PS_SEARCH_FILE;
            break;

        case PMTAG_CAP:
            pstag = PS_SEARCH_CAP;
            break;

        case PMTAG_REQ:
            pstag = PS_SEARCH_REQ;
            break;

        case PMTAG_CNFL:
            pstag = PS_SEARCH_CNFL;
            break;

        case PMTAG_OBSL:
            pstag = PS_SEARCH_OBSL;
            break;

        default:
            n_assert(0);
    }

    it->i = 0;
    it->tag = tag;
    it->pkgs = pkgset_search(db->ps, pstag, arg);
    return it->pkgs ? n_array_size(it->pkgs) : 0;
}

static
void psetdb_it_destroy(struct psetdb_it *it)
{
    if (it->pkgs)
        n_array_free(it->pkgs);
}


static
const struct pm_dbrec *psetdb_it_get(struct psetdb_it *it)
{
    struct pkg *pkg;

    if (it->pkgs == NULL)
        return NULL;

    if (it->i == n_array_size(it->pkgs))
        return NULL;

    pkg = n_array_nth(it->pkgs, it->i++);
    it->dbrec.hdr = pkg;
    it->dbrec.recno = pkg->recno;
    return &it->dbrec;
}


static
int psetdb_it_get_count(struct psetdb_it *it)
{
    if (it->pkgs)
        return n_array_size(it->pkgs);
    return 0;
}

static
int pm_pset_db_it_get_count(struct pkgdb_it *it)
{
    return psetdb_it_get_count(it->_it);
}

static
const struct pm_dbrec *pm_pset_db_it_get(struct pkgdb_it *it)
{
    return psetdb_it_get(it->_it);
}


void pm_pset_db_it_destroy(struct pkgdb_it *it)
{
    psetdb_it_destroy(it->_it);
    n_free(it->_it);
    it->_it = NULL;
}


int pm_pset_db_it_init(struct pkgdb_it *it, int tag, const char *arg)
{
    struct psetdb_it *psit;

    psit = n_malloc(sizeof(*psit));
    psetdb_it_init(it->_db->dbh, psit, tag, arg);
    it->_it = psit;
    it->_get = pm_pset_db_it_get;
    it->_get_count = pm_pset_db_it_get_count;
    it->_destroy = pm_pset_db_it_destroy;
    return 1;
}



int pm_pset_hdr_nevr(void *h, const char **name, int32_t *epoch,
                     const char **ver, const char **rel,
                     const char **arch, uint32_t *color)
{
    struct pkg *pkg = h;

    *name = pkg->name;
    *epoch = pkg->epoch;
    *ver = pkg->ver;
    *rel = pkg->rel;

    if (arch)
        *arch = (char*)pkg_arch(pkg);

    if (color)
        *color = pkg->color;

    return 1;
}

void *pm_pset_hdr_link(void *h)
{
    struct pkg *pkg = h;
    return pkg_link(pkg);
}

void pm_pset_hdr_free(void *h)
{
    struct pkg *pkg = h;
    pkg_free(pkg);
}

struct pkg *pm_pset_ldhdr(tn_alloc *na, void *hdr, const char *fname,
                          unsigned fsize, unsigned ldflags)
{
    struct pkg *pkg = hdr;
    na = na; fname = fname; fsize = fsize; ldflags = ldflags;
    return pkg_link(pkg);

}

tn_array *pm_pset_ldhdr_capreqs(tn_array *arr, void *hdr, int crtype)
{
    tn_array *crs = NULL;
    struct pkg *pkg = hdr;
    int i;

    switch (crtype) {
        case PMCAP_CAP:
            crs = pkg->caps;
            break;

        case PMCAP_REQ:
            crs = pkg->reqs;
            break;

        case PMCAP_CNFL:
        case PMCAP_OBSL:
            crs = pkg->cnfls;
            break;

        default:
            n_die("%d: unknown type (internal error)", crtype);
    }

    if (crs == NULL)
        return NULL;

    for (i=0; i < n_array_size(crs); i++) {
        struct capreq *cr = n_array_nth(crs, i);
        switch (crtype) {
            case PMCAP_CAP:
            case PMCAP_REQ:
                n_array_push(arr, capreq_clone(NULL, cr));
                break;

            case PMCAP_CNFL:
                if (!capreq_is_obsl(cr))
                    n_array_push(arr, capreq_clone(NULL, cr));
                break;

            case PMCAP_OBSL:
                if (capreq_is_obsl(cr))
                    n_array_push(arr, capreq_clone(NULL, cr));
                break;
        }
    }

    return arr;
}

static char *mktsdir(const char *cachedir)
{
    char tsdir[PATH_MAX];
    n_snprintf(tsdir, sizeof(tsdir), "%s/tsXXXXXX", cachedir);

#ifdef HAVE_MKDTEMP
    if (mkdtemp(tsdir) == NULL) {
        logn(LOGERR, "mkdtemp %s: %m", tsdir);
        return NULL;
    }
#else
#error "mkdtemp is needed"
#endif

    return n_strdup(tsdir);
}

static int do_pkgtslink(struct pm_psetdb *db, const char *cachedir,
                        struct pkg *pkg, const char *pkgpath)
{
    char tspath[PATH_MAX];

    if (db->tsdir == NULL) {
        if ((db->tsdir = mktsdir(cachedir)) == NULL)
            return 0;
    }
    n_snprintf(tspath, sizeof(tspath), "%s/%s", db->tsdir, n_basenam(pkgpath));

    if (pkg_file_url_type(pkg) == VFURL_PATH) {
        if (symlink(pkgpath, tspath) != 0) {
            logn(LOGERR, "%s: symlink failed: %m", pkgpath);
            return 0;
        }
    } else {
        if (link(pkgpath, tspath) != 0) {
            if (!poldek_util_copy_file(pkgpath, tspath))
                return 0;
        }
    }

    n_array_push(db->paths_added, n_strdup(tspath));
    return 1;
}

#if ENABLE_TRACE
static void dumpdir(struct pkgdir *pkgdir)
{
    int i;
    DBGF("dump:\n");
    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
        if (strcmp(pkg->name, "fix-info-dir") == 0)
            DBGF("  %p %s\n", pkg, pkg->name);
    }
}
#endif

static int is_immutable(struct pm_pset *pm, const char *oplabel)
{
    char reason[64];
    int n = 0;

    if (pm->flags & IMMUTABLE_REMOTESRC)
        n += n_snprintf(&reason[n], sizeof(reason) - n, "remote source");

    if (pm->flags & IMMUTABLE_MULTISRC)
        n += n_snprintf(&reason[n], sizeof(reason) - n, "%smultiple sources",
                        n > 0 ? ", " : "");
    if (n > 0)
        logn(LOGERR, "'pset' database is immutable (%s), %s refused",
             reason, oplabel);
    return n;
}

int pm_pset_packages_install(struct pkgdb *pdb, const tn_array *pkgs,
                             const tn_array *pkgs_toremove,
                             struct poldek_ts *ts)
{
    struct pm_psetdb *db = pdb->dbh;
    struct pkgdir *pkgdir;
    char path[PATH_MAX];
    int i;

    if (ts->getop(ts, POLDEK_OP_RPMTEST))
        return 1;

    n_assert(ts->getop(ts, POLDEK_OP_TEST) == 0);
    if (is_immutable(db->pm, "installation"))
        return 0;

    pm_pset_packages_uninstall(pdb, pkgs_toremove, ts);

    n_assert(n_array_size(db->ps->pkgdirs) == 1);
    pkgdir = n_array_nth(db->ps->pkgdirs, 0);

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *tmp, *pkg = n_array_nth(pkgs, i);

        if (!pkg_localpath(pkg, path, sizeof(path), ts->cachedir))
            continue;

        tmp = n_array_bsearch(pkgdir->pkgs, pkg);

        DBGF("in %p(%p) %s\n", pkg, tmp, pkg_id(pkg));
        if (pkg->recno > 0)
            logn(LOGERR, _("%s: recno is set, should not happen"), pkg_id(pkg));

        pkgset_add_package(db->ps, pkg);
        pkgdir_add_package(pkgdir, pkg);
        pkg->recno = db->_recno++;
        n_array_push(db->pkgs_added, pkg_link(pkg));

        tmp = n_array_bsearch(pkgdir->pkgs, pkg);
        DBGF("after in %p(%p) %s\n", pkg, tmp, pkg_id(pkg));

        if (ts->getop(ts, POLDEK_OP_JUSTDB))
            n_array_push(db->paths_added, n_strdup(path));

        else if (!do_pkgtslink(db, ts->cachedir, pkg, path))
            return 0;

        msgn(2, _("Copying %s to %s"), path, pkgdir->path);
    }
    //dumpdir(pkgdir);
    return 1;
}


int pm_pset_packages_uninstall(struct pkgdb *pdb, const tn_array *pkgs,
                               struct poldek_ts *ts)
{
    struct pm_psetdb *db = pdb->dbh;
    struct pkgdir *pkgdir;
    char path[PATH_MAX];
    int i;

    if (ts->getop(ts, POLDEK_OP_RPMTEST))
        return 1;

    n_assert(ts->getop(ts, POLDEK_OP_TEST) == 0);
    if (is_immutable(db->pm, "removal"))
        return 0;

    n_assert(n_array_size(db->ps->pkgdirs) == 1);
    pkgdir = n_array_nth(db->ps->pkgdirs, 0);
    ts = ts;

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        if (pkg_path(pkg, path, sizeof(path))) {
            struct pkg *tmp = n_array_bsearch(pkgdir->pkgs, pkg);

            if (tmp == NULL) {
                logn(LOGERR, "%s: not found, should not happen", pkg_id(pkg));
                n_assert(0);
            }

            tmp->recno = 0;
            pkgset_remove_package(db->ps, tmp);
            pkgdir_remove_package(pkgdir, tmp);

            DBGF("un %p(%p) %s\n", pkg, tmp, pkg_id(pkg));
            n_array_push(db->paths_removed, n_strdup(path));
            msgn(2, _("Removing %s"), path);
        }
    }
    return 1;
}

void pm_pset_tx_begin(void *dbh, struct poldek_ts *ts)
{
    struct pm_psetdb *db = dbh;

    n_assert(db->ts == NULL);
    db->ts = ts;
}


/* TODO: transaction like behaviour */
int pm_pset_tx_commit(void *dbh)
{
    struct pm_psetdb *db = dbh;
    struct poldek_ts *ts = db->ts;
    struct pkgdir *pkgdir;
    char dstpath[PATH_MAX];
    int i, rc = 1, nchanges;

    n_assert(db->ts);
    db->ts = NULL;

    nchanges = n_array_size(db->paths_removed) + n_array_size(db->paths_added);
    if (nchanges == 0)
        return 1;

    nchanges = 0;               /* count real made changes */
    n_assert(n_array_size(db->ps->pkgdirs) == 1);
    pkgdir = n_array_nth(db->ps->pkgdirs, 0);
    msgn(0, _("Operating on %s"), pkgdir->path);

    for (i=0; i < n_array_size(db->paths_removed); i++) {
        const char *path = n_array_nth(db->paths_removed, i);

        msgn_f(0, "%%add %s", n_basenam(path));
        msgn_f(0, "%%rm %s\n", path);
        msgn_tty(1, "rm %s\n", path);

        if (!ts->getop(ts, POLDEK_OP_JUSTDB)) {
            if (unlink(path) != 0) {
                logn(LOGERR, _("%s: unlink failed: %m"), path);
                rc = 0;
                break;
            }
        }
        nchanges++;
    }

    if (rc) {
        for (i=0; i < n_array_size(db->paths_added); i++) {
            const char *path = n_array_nth(db->paths_added, i);
            int n;

            n = n_snprintf(dstpath, sizeof(dstpath), "%s", pkgdir->path);
            n_snprintf(&dstpath[n], sizeof(dstpath) - n, "%s%s",
                       dstpath[n-1] == '/' ? "":"/", n_basenam(path));

            msgn_f(0, "%%del %s", n_basenam(path));
            msgn_f(0, "%%cp %s %s\n", path, dstpath);
            msgn_tty(1, "cp %s %s\n", n_basenam(path), dstpath);

            if (!ts->getop(ts, POLDEK_OP_JUSTDB)) {
                if (!poldek_util_copy_file(path, dstpath)) {
                    rc = 0;
                    break;
                }
                unlink(path);
            }
            nchanges++;
        }
    }

    if (nchanges == 0 || !rc)
        return rc;

    if (pkgdir_type_info(pkgdir->type) & PKGDIR_CAP_SAVEABLE)
        rc = pkgdir_save(pkgdir, 0);

    else if (ts->getop(ts, POLDEK_OP_JUSTDB))
        logn(LOGWARN, "--justdb makes no sense for non-db repository");

    return rc;
}


struct pkgdir *pm_pset_db_to_pkgdir(void *pm_pset, const char *rootdir,
                                    const char *dbpath, unsigned pkgdir_ldflags,
                                    tn_hash *kw)
{
    struct pm_pset *pm = pm_pset;
    struct source *src;
    struct pkgdir *dir;

    rootdir = rootdir; dbpath = dbpath;

    n_assert(n_hash_exists(kw, "source") == 0); /* use pm_configure() */

    if (n_array_size(pm->sources) == 0) {
        logn(LOGERR,
             _("Could not open 'pset' database: missing source"));
        return NULL;
    }

    if (n_array_size(pm->sources) > 1) {
        logn(LOGNOTICE, "Could not make pkgdir from multiple sources, "
             "making only from the first one");
    }

    src = n_array_nth(pm->sources, 0);

    if ((dir = pkgdir_srcopen(src, 0)) == NULL) {
        if (!source_is_type(src, "dir") && util__isdir(src->path)) {
            logn(LOGNOTICE, _("trying to scan directory %s..."), src->path);
            source_set_type(src, "dir");
            dir = pkgdir_srcopen(src, 0);
        }
    }

    if (dir == NULL)
        return NULL;

    if (!pkgdir_load(dir, NULL, pkgdir_ldflags)) {
        pkgdir_free(dir);
        return NULL;
    }

    return dir;
}
