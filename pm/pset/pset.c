/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@k2.net.pl>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

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
#include "pm_pset.h"
#include "pkgset.h"

struct pm_psetdb {
    struct source *src;
    struct pkgset *ps;
    char *tsdir;
    tn_array *paths_added;
    tn_array *paths_removed;
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
    return pm;
}

void pm_pset_destroy(void *pm_pset) 
{
    struct pm_pset *pm = pm_pset;
    struct source *src;
    
    n_cfree(&pm->installer_path);
    src = n_hash_get(pm->cnf, "source");
    if (src)
        source_free(src);
    n_hash_free(pm->cnf);
    
    memset(pm, 0, sizeof(*pm));
    free(pm);
}


int pm_pset_configure(void *pm_pset, const char *key, void *val)
{
    struct pm_pset *pm = pm_pset;
    
    if (n_str_eq(key, "source"))
        n_hash_insert(pm->cnf, key, val);
    
    return 1;
}

void *pm_pset_opendb(void *pm_pset, void *dbh,
                     const char *rootdir, const char *dbpath,
                     mode_t mode, tn_hash *kw)
{
    struct pm_pset *pm = pm_pset;
    struct pm_psetdb *db = dbh;
    struct source *src;
    struct pkgdir *dir;
    struct pkgset *ps;
    
    rootdir = rootdir; dbpath = dbpath; mode = mode;
    
    if (db)
        return db;

    src = n_hash_get(kw, "source");
    if (src == NULL) {
        src = n_hash_get(pm->cnf, "source"); /* default */
    }
    
    if (!src) {
        logn(LOGERR,
             _("Could not open 'pset' database: missing source parameter"));
        return NULL;
    }

    if (source_is_remote(src) && 0 /* for testing */)
        return NULL;

    if ((dir = pkgdir_srcopen(src, 0)) == NULL) {
        if (!source_is_type(src, "dir") && is_dir(src->path)) {
            logn(LOGNOTICE, _("trying to scan directory %s..."), src->path);
            source_set_type(src, "dir");
            dir = pkgdir_srcopen(src, 0);
        }
    }
    
    if (dir == NULL)
        return NULL;
    
    if (!pkgdir_load(dir, 0, 0)) {
        pkgdir_free(dir);
        return NULL;
    }
    
    if ((ps = pkgset_new(NULL)) == NULL)
        return NULL;
        
    if (!pkgset_add_pkgdir(ps, dir)) {
        logn(LOGERR, _("no packages loaded"));
        pkgset_free(ps);
        pkgdir_free(dir);
        return NULL;
    }

    pkgset_setup(ps, PSET_VRFY_MERCY);
    db = n_malloc(sizeof(*db));
    db->src = src;
    db->ps = ps;
    db->paths_added = n_array_new(32, free, NULL);
    db->paths_removed = n_array_new(32, free, NULL);
    db->tsdir = NULL;
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
    
    if (db->tsdir) {
        int i;
        for (i=0; i < n_array_size(db->paths_added); i++) {
            char *path = n_array_nth(db->paths_added, i);
            DBGF_F("unlink %s\n", path);
            unlink(path);
        }
        rmdir(db->tsdir);
    }
    
    n_array_free(db->paths_added);
    n_array_free(db->paths_removed);
    memset(db, 0, sizeof(*db));
    free(db);
}

static int do_cp(const char *src, const char *dst)
{
    struct p_open_st pst;
    unsigned p_open_flags = P_OPEN_KEEPSTDIN;
    char *argv[5];
    int n, ec;
    
    p_st_init(&pst);

    n = 0;
    argv[n++] = "cp";
    argv[n++] = "-v";
    argv[n++] = (char*)src;
    argv[n++] = (char*)dst;
    argv[n++] = NULL;

    if (p_open(&pst, p_open_flags, "/bin/cp", argv) == NULL) {
        if (pst.errmsg) {
            logn(LOGERR, "%s", pst.errmsg);
            p_st_destroy(&pst);
        }
    }

    //process_output(&pst, verbose_level);
    if ((ec = p_close(&pst) != 0) && pst.errmsg != NULL)
        logn(LOGERR, "cp %s: %s", pst.errmsg, src);

    p_st_destroy(&pst);
    return ec == 0;
}

/* remeber! don't touch any member */
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
            die();
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



int pm_pset_hdr_nevr(void *h, char **name,
                    int32_t *epoch, char **version, char **release)
{
    struct pkg *pkg = h;
    
    *name = pkg->name;
    *epoch = pkg->epoch;
    *version = pkg->ver;
    *release = pkg->rel;
    return 1;
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

static int do_pkgtslink(struct pm_psetdb *db, const char *cachedir,
                         struct pkg *pkg, const char *pkgpath)
{
    char tspath[PATH_MAX];
    
    if (db->tsdir == NULL) {
        char tsdir[PATH_MAX];
        n_snprintf(tsdir, sizeof(tsdir), "%s/tsXXXXXX", cachedir);
#ifdef HAVE_MKDTEMP
        if (mkdtemp(tsdir) == NULL) {
            logn(LOGERR, "mkdtemp %s: %m", tsdir);
            return 0;
        }
#else
#error "mkdtemp is needed"        
#endif        
        db->tsdir = n_strdup(tsdir);
    }
    
    n_snprintf(tspath, sizeof(tspath), "%s/%s", db->tsdir, n_basenam(pkgpath));


    if (pkg_file_url_type(pkg) == VFURL_PATH) {
        if (symlink(pkgpath, tspath) != 0) {
            logn(LOGERR, "%s: symlink failed: %m\n", pkgpath);
            return 0;
        }
    } else {
        if (link(pkgpath, tspath) != 0) {
            if (!do_cp(pkgpath, tspath))
                return 0;
        }
    }
    n_array_push(db->paths_added, n_strdup(tspath));
    return 1;
}


int pm_pset_packages_install(struct pkgdb *pdb,
                             tn_array *pkgs, tn_array *pkgs_toremove,
                             struct poldek_ts *ts) 
{
    struct pm_psetdb *db = pdb->dbh;
    struct pkgdir *pkgdir;
    char path[PATH_MAX];
    int i;

    if (ts->getop(ts, POLDEK_OP_RPMTEST))
        return 1;

    n_assert(n_array_size(db->ps->pkgdirs) == 1);
    pkgdir = n_array_nth(db->ps->pkgdirs, 0);

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        if (pkg_localpath(pkg, path, sizeof(path), ts->cachedir)) {
            pkgset_add_package(db->ps, pkg);
            pkgdir_add_package(pkgdir, pkg);
            if (ts->getop(ts, POLDEK_OP_JUSTDB))
                continue;
            
            if (!do_pkgtslink(db, ts->cachedir, pkg, path))
                return 0;
            msgn(2, "%%install %s %s", path, pkgdir->path);
        }
    }

    pm_pset_packages_uninstall(pdb, pkgs_toremove, ts);
    return 1;
}


int pm_pset_packages_uninstall(struct pkgdb *pdb,
                               tn_array *pkgs, struct poldek_ts *ts)
{
    struct pm_psetdb *db = pdb->dbh;
    struct pkgdir *pkgdir;
    char path[PATH_MAX];
    int i;

    if (ts->getop(ts, POLDEK_OP_RPMTEST))
        return 1;

    n_assert(n_array_size(db->ps->pkgdirs) == 1);
    pkgdir = n_array_nth(db->ps->pkgdirs, 0);
    ts = ts;
    
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        
        if (pkg_path(pkg, path, sizeof(path))) {
            pkgset_remove_package(db->ps, pkg);
            pkgdir_remove_package(pkgdir, pkg);
            if (ts->getop(ts, POLDEK_OP_JUSTDB))
                continue;
            n_array_push(db->paths_removed, n_strdup(path));
            msgn(2, "%%uninstall %s", path);
        }
    }
    return 1;
}

int pm_pset_commitdb(void *dbh) 
{
    struct pm_psetdb *db = dbh;
    struct pkgdir *pkgdir;
    char dstpath[PATH_MAX];
    int i, rc = 1, nchanges = 0;

    n_assert(n_array_size(db->ps->pkgdirs) == 1);
    pkgdir = n_array_nth(db->ps->pkgdirs, 0);

    msgn(0, "Installing to %s", pkgdir->path);
    for (i=0; i < n_array_size(db->paths_removed); i++) {
        const char *path = n_array_nth(db->paths_removed, i);
        DBGF_F("rm %s\n", path);
        unlink(path);
        nchanges++;
    }

    for (i=0; i < n_array_size(db->paths_added); i++) {
        const char *path = n_array_nth(db->paths_added, i);
        n_snprintf(dstpath, sizeof(dstpath), "%s/%s", pkgdir->path,
                   n_basenam(path));
        DBGF_F("cp %s %s\n", path, dstpath);
        if (!do_cp(path, dstpath)) {
            rc = 0;
            break;
        }
        unlink(path);
        nchanges++;
    }
    
    
    if (nchanges && rc && pkgdir_type_info(pkgdir->type) & PKGDIR_CAP_SAVEABLE)
        rc = pkgdir_save(pkgdir, NULL, NULL, 0);
    
    return rc;
}


struct pkgdir *pm_pset_db_to_pkgdir(void *pm_pset, const char *rootdir,
                                    const char *dbpath, tn_hash *kw)
{
    struct pm_pset *pm = pm_pset;
    struct source *src;
    struct pkgdir *dir;
    
    rootdir = rootdir; dbpath = dbpath; 
    
    src = n_hash_get(kw, "source");
    if (src == NULL)
        src = n_hash_get(pm->cnf, "source"); /* default */
    
    if (!src) {
        logn(LOGERR,
             _("Could not open 'pset' database: missing source parameter"));
        return NULL;
    }

    if (source_is_remote(src)) {
        logn(LOGERR, _("%s: source could not be remote one"),
             source_idstr(src)); 
        return NULL;
    }
    
    if ((dir = pkgdir_srcopen(src, 0)) == NULL) {
        if (!source_is_type(src, "dir") && is_dir(src->path)) {
            logn(LOGNOTICE, _("trying to scan directory %s..."), src->path);
            source_set_type(src, "dir");
            dir = pkgdir_srcopen(src, 0);
        }
    }
    
    if (dir == NULL)
        return NULL;
    
    if (!pkgdir_load(dir, 0, 0)) {
        pkgdir_free(dir);
        return NULL;
    }
    
    return dir;
}



