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

#include <stdlib.h>
#include <string.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <fcntl.h>              /* for O_* */

#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
#include <trurl/narray.h>

#include "compiler.h"
#include "sigint/sigint.h"
#include "i18n.h"
#include "capreq.h"
#include "poldek_ts.h"
#include "pm.h"
#include "mod.h"
#include "log.h"



static
struct pkgdb *pkgdb_malloc(struct pm_ctx *ctx, const char *rootdir,
                           const char *path, const char *key, va_list ap)
{
    struct pkgdb *db;

    db = n_calloc(sizeof(*db), 1);
    if (path)
        db->path = n_strdup(path);

    if (rootdir)
        db->rootdir = n_strdup(rootdir);

    db->kw = n_hash_new(16, NULL);
    while (key != NULL) {
        void *val = va_arg(ap, void*);
        DBGF("%s %p\n", key, val);

        if (val)
            n_hash_insert(db->kw, key, val); /* pm module should free() val */
        key = va_arg(ap, const char*);
    }
    db->_ctx = ctx;
    return db;
}


struct pkgdb *pkgdb_open(struct pm_ctx *ctx, const char *rootdir,
                         const char *path, mode_t mode,
                         const char *key, ...)
{
    struct pkgdb *db;
    char dbpath[PATH_MAX];
    va_list ap;

    if (path == NULL && pm_dbpath(ctx, dbpath, sizeof(dbpath)))
        path = dbpath;

    va_start(ap, key);
    db = pkgdb_malloc(ctx, rootdir, path, key, ap);
    va_end(ap);

    if (pkgdb_reopen(db, mode)) {
        return db;
    }

    pkgdb_free(db);

    return NULL;
}

int pkgdb_reopen(struct pkgdb *db, mode_t mode)
{
    if (db->_opened)
        return 1;

    if (mode == 0)
        mode = O_RDONLY;

    n_assert(db->_ctx->mod->dbopen);
    db->dbh = db->_ctx->mod->dbopen(db->_ctx->modh, db->dbh, db->rootdir,
                                    db->path, mode, db->kw);

    if (db->dbh == NULL)
        return 0;

    db->_opened = 1;
    db->mode = mode;

    return 1;
}

pkgdb_filter_fn pkgdb_set_filter(struct pkgdb *db,
                                 pkgdb_filter_fn filter,
                                 void *filter_arg)
{
    void *prevfitler = db->_filter;
    db->_filter = filter;
    db->_filter_arg = filter_arg;
    return prevfitler;
}


void pkgdb_close(struct pkgdb *db)
{
    if (db->_opened) {
        n_assert(db->_ctx->mod->dbclose);
        db->_ctx->mod->dbclose(db->dbh);
        db->_opened = 0;
    }
}

void pkgdb_free(struct pkgdb *db)
{
    pkgdb_close(db);

    if (db->path) {
        free(db->path);
        db->path = NULL;
    }

    if (db->rootdir) {
        free(db->rootdir);
        db->rootdir = NULL;
    }

    if (db->dbh && db->_ctx->mod->dbfree)
        db->_ctx->mod->dbfree(db->dbh);

    n_hash_free(db->kw);
    free(db);
}

int pkgdb_tx_begin(struct pkgdb *db, struct poldek_ts *ts)
{
    if (db->_txcnt == 0 && db->_ctx->mod->dbtxbegin)
        db->_ctx->mod->dbtxbegin(db->dbh, ts);

    db->_txcnt++;
    return db->_txcnt;
}

int pkgdb_tx_commit(struct pkgdb *db)
{
    if (db->_txcnt > 0)
        db->_txcnt--;

    if (db->_txcnt == 0 && db->_ctx->mod->dbtxcommit)
        db->_ctx->mod->dbtxcommit(db->dbh);
    return db->_txcnt;
}


int pkgdb_it_init(struct pkgdb *db, struct pkgdb_it *it,
                  int tag, const char *arg)
{
    memset(it, 0, sizeof(*it));

    it->_db = db;
    if (db->_filter)
        pkgdb_it_set_filter(it, db->_filter, db->_filter_arg);

    return db->_ctx->mod->db_it_init(it, tag, arg);
}

pkgdb_filter_fn pkgdb_it_set_filter(struct pkgdb_it *it,
                                    pkgdb_filter_fn filter,
                                    void *filter_arg)
{
    void *prevfitler = it->_filter;
    it->_filter = filter;
    it->_filter_arg = filter_arg;
    return prevfitler;
}


void pkgdb_it_destroy(struct pkgdb_it *it)
{
    it->_destroy(it);
    memset(it, 0, sizeof(*it));
}

const struct pm_dbrec *pkgdb_it_get(struct pkgdb_it *it)
{
    struct pm_dbrec *dbrec;

    dbrec = (struct pm_dbrec *)it->_get(it);
    if (it->_filter) {
        while (dbrec) {
            dbrec->_ctx = it->_db->_ctx;
            if (it->_filter(it->_db, dbrec, it->_filter_arg))
                break;

            dbrec = (struct pm_dbrec *)it->_get(it);
        }
    }

    if (dbrec)
        dbrec->_ctx = it->_db->_ctx;

    return dbrec;
}

int pkgdb_it_get_count(struct pkgdb_it *it)
{
    return it->_get_count(it);
}

int pm_dbrec_nevr(const struct pm_dbrec *dbrec,
                  const char **name, int32_t *epoch,
                  const char **ver, const char **rel,
                  const char **arch, uint32_t *color)
{
    n_assert(dbrec->_ctx->mod->hdr_nevr);
    return dbrec->_ctx->mod->hdr_nevr(dbrec->hdr, name, epoch, ver, rel,
                                      arch, color);
}


/* NOTICE: function doing pkg cmp hdr */
static
int pkg_hdr_cmp_evr(struct pm_ctx *ctx, void *hdr, const struct pkg *pkg,
                    int *cmprc)
{
    struct pkg tmpkg;
    const char *arch;

    if (!ctx->mod->hdr_nevr(hdr, (const char **)&tmpkg.name, &tmpkg.epoch,
                            (const char **)&tmpkg.ver, (const char **)&tmpkg.rel,
                            &arch, NULL))
        return 0;

    *cmprc = pkg_cmp_evr(pkg, &tmpkg);
    return 1;
}

int do_search_package(struct pkgdb *db, const struct pkg *pkg, int *cmprcptr,
                      struct pm_dbrec *todbrec)
{
    const struct pm_dbrec *dbrec;
    struct pkgdb_it it;
    int n = 0, cmprc = 0;

    pkgdb_it_init(db, &it, PMTAG_NAME, pkg->name);
    while ((dbrec = pkgdb_it_get(&it)) != NULL) {
        if (!pkg_hdr_cmp_evr(db->_ctx, dbrec->hdr, pkg, &cmprc))
            continue; /* fail */

        if (cmprc == 0) {
            n++;
            if (todbrec) {
                todbrec->hdr = db->_ctx->mod->hdr_link(dbrec->hdr);
                todbrec->recno = dbrec->recno;
                todbrec->_ctx = db->_ctx;
            }
            break;
        }
    }
    pkgdb_it_destroy(&it);
    if (cmprcptr)
        *cmprcptr = cmprc;
    return n;
}

int pkgdb_is_pkg_installed(struct pkgdb *db, const struct pkg *pkg, int *cmprc)
{
    return do_search_package(db, pkg, cmprc, NULL);
}


int pkgdb_get_package_hdr(struct pkgdb *db, const struct pkg *pkg,
                          struct pm_dbrec *dbrec)
{
    n_assert(dbrec);
    dbrec->hdr = NULL;

    if (!do_search_package(db, pkg, NULL, dbrec))
        return 0;

    n_assert(dbrec->hdr);
    return 1;
}


static struct pkg *load_pkg(tn_alloc *na, struct pkgdb *db,
                            const struct pm_dbrec *dbrec, unsigned ldflags)
{
    struct pkg *pkg;
    int ownedna = 0;

    if (na == NULL) {
        na = n_alloc_new(2, TN_ALLOC_OBSTACK);
        ownedna = 1;
    }

    if ((pkg = db->_ctx->mod->hdr_ld(na, dbrec->hdr, NULL, 0, ldflags))) {
        pkg->recno = dbrec->recno;
        pkg_add_selfcap(pkg);
        pkg->flags |= PKG_DBPKG;
    }

    if (ownedna)
        n_alloc_free(na); /* per pkg na (pkg holds its reference); TODO better */

    return pkg;
}

int pkgdb_install(struct pkgdb *db, const char *path,
                  const struct poldek_ts *ts)
{
    n_assert(db->dbh);
    if (db->_ctx->mod->dbinstall)
        return db->_ctx->mod->dbinstall(db, path, ts);
    logn(LOGERR, "%s: dbinstall is not supported", db->_ctx->mod->name);
    return 0;
}

static int dbpkg_array_has(const tn_array *pkgs, int recno)
{
    struct pkg tmp;
    tmp.recno = recno;
    n_assert(n_array_ctl_get_cmpfn(pkgs) == (tn_fn_cmp)pkg_cmp_recno);
    return n_array_bsearch(pkgs, &tmp) != NULL;
}

int pkgdb_search(struct pkgdb *db, tn_array **dbpkgs,
                 enum pkgdb_it_tag tag,
                 const char *value,
                 const tn_array *exclude,
                 unsigned ldflags)
{
    struct pkgdb_it        it;
    const struct pm_dbrec  *dbrec;
    int                    nfound = 0, dbpkgs_was_null = 0;

    pkgdb_it_init(db, &it, tag, value);
    while ((dbrec = pkgdb_it_get(&it))) {
        struct pkg *pkg;

        if (exclude && dbpkg_array_has(exclude, dbrec->recno))
            continue;

        if (dbpkgs == NULL) {
            nfound++;
            continue;
        }

        if (*dbpkgs == NULL) {
            *dbpkgs = pkgs_array_new_ex(16, pkg_cmp_recno);
            dbpkgs_was_null = 1;
        }

        if (!dbpkgs_was_null && dbpkg_array_has(*dbpkgs, dbrec->recno))
            continue;

        if ((pkg = load_pkg(NULL, db, dbrec, ldflags))) {
            nfound++;
            n_array_push(*dbpkgs, pkg);
        }
    }

    pkgdb_it_destroy(&it);
    return nfound;
}


static int header_evr_match_req(struct pm_ctx *ctx, void *hdr,
                                const struct capreq *req)
{
    struct pkg pkg;

    if (!ctx->mod->hdr_nevr(hdr, (const char**)&pkg.name, &pkg.epoch,
                            (const char **)&pkg.ver, (const char **)&pkg.rel,
                            NULL, NULL)) {
        return -1;
    }

    DBGF("%s match %s?\n", pkg_evr_snprintf_s(&pkg),
         capreq_snprintf_s0(req));

    if (pkg_evr_match_req(&pkg, req, POLDEK_MA_PROMOTE_VERSION)) {
        DBGF("%s match %s!\n", pkg_evr_snprintf_s(&pkg),
             capreq_snprintf_s0(req));
        return 1;
    }

    return 0;
}

static int header_cap_match_req(struct pm_ctx *ctx, void *hdr,
                                const struct capreq *req, unsigned ma_flags)
{
    struct pkg  pkg;
    int         rc;

    rc = 0;
    memset(&pkg, 0, sizeof(pkg));
    pkg.caps = capreq_arr_new(0);
    if (!ctx->mod->hdr_ld_capreqs(pkg.caps, hdr, PMCAP_CAP))
        return -1;

    if (n_array_size(pkg.caps) > 0) {
        n_array_sort(pkg.caps);
        rc = pkg_caps_match_req(&pkg, req, ma_flags);
    }

    n_array_free(pkg.caps);
    return rc;
}

static int db_match(struct pkgdb *db, enum pkgdb_it_tag tag,
                    const struct capreq *cap, const tn_array *exclude,
                    unsigned ma_flags)
{
    struct pkgdb_it        it;
    const struct pm_dbrec  *dbrec;
    int                    match = 0, is_file;

    is_file = (*capreq_name(cap) == '/' ? 1 : 0);

    pkgdb_it_init(db, &it, tag, capreq_name(cap));
    while ((dbrec = pkgdb_it_get(&it))) {
        if (exclude && dbpkg_array_has(exclude, dbrec->recno))
            continue;

        if (is_file || header_cap_match_req(db->_ctx, dbrec->hdr, cap, ma_flags)) {
            match = 1;
            break;
        }
    }

    pkgdb_it_destroy(&it);
    return match;
}


int pkgdb_match_req(struct pkgdb *db, const struct capreq *req, unsigned ma_flags,
                    const tn_array *exclude)
{
    int is_file;

    is_file = (*capreq_name(req) == '/' ? 1 : 0);
    //tracef(0, "%s %d", capreq_snprintf_s(req), n_array_size(exclude));

    if (!is_file && db_match(db, PMTAG_NAME, req, exclude, ma_flags))
        return 1;

    if (db_match(db, PMTAG_CAP, req, exclude, ma_flags))
        return 1;

    if (is_file && db_match(db, PMTAG_FILE, req, exclude, ma_flags))
        return 1;

    return 0;
}


static int get_obsoletedby_cap(struct pkgdb *db, int tag, tn_array *dbpkgs,
                               struct capreq *cap,
                               const tn_array *exclude, unsigned ldflags)
{
    struct pkgdb_it it;
    const struct pm_dbrec *dbrec;
    int n = 0;

    pkgdb_it_init(db, &it, tag, capreq_name(cap));
    while ((dbrec = pkgdb_it_get(&it)) != NULL) {
        int add = 0;

        if (exclude && dbpkg_array_has(exclude, dbrec->recno))
            continue;

        if (dbpkg_array_has(dbpkgs, dbrec->recno))
            continue;

        switch (tag) {
            case PMTAG_NAME:
                add = header_evr_match_req(db->_ctx, dbrec->hdr, cap);
                break;

            case PMTAG_CAP:
                add = header_cap_match_req(db->_ctx, dbrec->hdr, cap, 1);
                break;

            default:
                n_assert(0);
                break;
        }

        if (add) {
            struct pkg *pkg;
            if ((pkg = load_pkg(NULL, db, dbrec, ldflags))) {
                n_array_push(dbpkgs, pkg);
                n_array_sort(dbpkgs);
                n++;
            }
        }
    }

    pkgdb_it_destroy(&it);
    return n;
}

static int get_obsoletedby_pkg_nevr(struct pkgdb *db, tn_array *dbpkgs,
                                    const struct pkg *pkg,
                                    const tn_array *exclude,
                                    unsigned ldflags, int rev)
{
    struct capreq *self_cap;
    int n, relflags = REL_EQ | REL_LT;

    if (rev)
        relflags = REL_EQ | REL_GT;

    self_cap = capreq_new(NULL, pkg->name, pkg->epoch, pkg->ver, pkg->rel,
                          relflags, 0);
    n = get_obsoletedby_cap(db, PMTAG_NAME, dbpkgs, self_cap, exclude, ldflags);
    capreq_free(self_cap);
    return n;
}

int pkgdb_q_obsoletedby_pkg(struct pkgdb *db, tn_array *dbpkgs,
                            const struct pkg *pkg, unsigned flags,
                            const tn_array *exclude, unsigned ldflags)
{
    int i, n;

    n_assert(flags & PKGDB_GETF_OBSOLETEDBY_NEVR);

    n = get_obsoletedby_pkg_nevr(db, dbpkgs, pkg, exclude, ldflags,
                                 flags & PKGDB_GETF_OBSOLETEDBY_REV);

    if ((flags & PKGDB_GETF_OBSOLETEDBY_OBSL) == 0)
        return n;

    if (pkg->cnfls == NULL)
        return n;

    /* Obsoletes */
    for (i=0; i < n_array_size(pkg->cnfls); i++) {
        struct capreq *cnfl = n_array_nth(pkg->cnfls, i);

        if (!capreq_is_obsl(cnfl))
            continue;

/* FIXME: is reverse match should be performed there too? */
        n += get_obsoletedby_cap(db, PMTAG_NAME, dbpkgs, cnfl, exclude, ldflags);
#ifdef HAVE_RPM_4_1             /* TODO -- code this in pm's module */
        n += get_obsoletedby_cap(db, PMTAG_CAP, dbpkgs, cnfl, exclude, ldflags);
#endif
    }

    return n;
}

struct pkgdir *pkgdb_to_pkgdir(struct pm_ctx *ctx, const char *rootdir,
                               const char *path, unsigned pkgdir_ldflags,
                               const char *key, ...)
{
    struct pkgdb *db;
    struct pkgdir *pkgdir;
    va_list ap;

    if (ctx->mod->db_to_pkgdir == NULL)
        return NULL;

    va_start(ap, key);
    db = pkgdb_malloc(ctx, rootdir, path, key, ap);
    va_end(ap);

    pkgdir = ctx->mod->db_to_pkgdir(ctx->modh, db->rootdir,
                                    db->path, pkgdir_ldflags, db->kw);
    pkgdb_free(db);
    return pkgdir;
}

static int q_what_requires(struct pkgdb *db, tn_array *dbpkgs,
                           enum pkgdb_it_tag tag, const struct capreq *cap,
                           const tn_array *exclude, unsigned ldflags,
                           unsigned ma_flags)
{
    struct pkgdb_it it;
    const struct pm_dbrec *dbrec;
    const char *value = capreq_name(cap);
    int n = 0;

    tracef(0, "%s", value);

    (void)ma_flags;  /* unused */

    pkgdb_it_init(db, &it, tag, value);
    while ((dbrec = pkgdb_it_get(&it)) != NULL) {
        struct pkg *pkg;

        if (exclude && dbpkg_array_has(exclude, dbrec->recno))
            continue;

#if ENABLE_TRACE
        pkg = load_pkg(NULL, db, dbrec, ldflags);
        DBGF("%s <- %s ????\n", capreq_name(cap), pkg_snprintf_s(pkg));
#endif
        if (dbpkg_array_has(dbpkgs, dbrec->recno))
            continue;

        if ((pkg = load_pkg(NULL, db, dbrec, ldflags)) == NULL)
            continue;

        DBGF("%s required by %s? => %s\n", capreq_name(cap), pkg_id(pkg),
             pkg_satisfies_req(pkg, cap, 1) ? "no" : "yes");

        if (pkg_satisfies_req(pkg, cap, 1)) { /* self matched? */
            trace(2, "- required %s: self matched", pkg_id(pkg));
            pkg_free(pkg);

         /* XXX wrong assumption; disabled */
        } else if (0 && capreq_versioned(cap) && /* old !strict */
                   !pkg_requires_cap(pkg, cap)) {
            DBGF("skipped %s (%s is not really required)\n", pkg_id(pkg),
                 capreq_snprintf_s(cap));
            pkg_free(pkg);

        } else {
            trace(2, "- required %s", pkg_id(pkg));
            DBGF("%s <- %s\n", capreq_snprintf_s(cap), pkg_id(pkg));
            n_array_push(dbpkgs, pkg);
            n_array_isort(dbpkgs);
            n++;
#if ENABLE_TRACE
            {
                int i;
                DBGF("%s <- %s\n", capreq_name(cap), pkg_snprintf_s(pkg));
                if (pkg->caps)
                    for (i=0; i<n_array_size(pkg->caps); i++)
                        DBGF("- %s\n",
                             capreq_snprintf_s0(n_array_nth(pkg->caps, i)));


            }
#endif
        }
    }

    pkgdb_it_destroy(&it);
    return n;
}

int pkgdb_q_what_requires(struct pkgdb *db, tn_array *dbpkgs,
                          const struct capreq *cap,
                          const tn_array *exclude, unsigned ldflags,
                          unsigned ma_flags)
{   int n;

    n = q_what_requires(db, dbpkgs, PMTAG_REQ, cap, exclude, ldflags, ma_flags);
    if (n == 0 && capreq_isdir(cap))
        n = q_what_requires(db, dbpkgs, PMTAG_DIRNAME, cap, exclude,
                            ldflags, ma_flags);

    return n;
}

static int q_is_required(struct pkgdb *db, int tag, const struct capreq *cap,
                         const tn_array *exclude)
{
    struct pkgdb_it it;
    const struct pm_dbrec *dbrec;
    unsigned ldflags = PKG_LDNEVR | PKG_LDCAPS;
    int n = 0;

    if (*capreq_name(cap) == '/')
        ldflags |= PKG_LDFL_DEPDIRS;

    pkgdb_it_init(db, &it, tag, capreq_name(cap));
    while ((dbrec = pkgdb_it_get(&it)) != NULL) {
        struct pkg *pkg;

        if (exclude && dbpkg_array_has(exclude, dbrec->recno))
            continue;
        DBGF("%s yes\n", capreq_snprintf_s(cap));

        if ((pkg = load_pkg(NULL, db, dbrec, ldflags))) {
            if (0 && pkg_satisfies_req(pkg, cap, 1)) { /* self matched? */
                pkg_free(pkg);

            } else {
                n++;
                break;
            }
        }
    }
    pkgdb_it_destroy(&it);
    return n;
}


int pkgdb_q_is_required(struct pkgdb *db, const struct capreq *cap,
                        const tn_array *exclude)
{
    if (q_is_required(db, PMTAG_REQ, cap, exclude))
        return 1;

    if (q_is_required(db, PMTAG_DIRNAME, cap, exclude))
        return 1;

    return 0;
}
