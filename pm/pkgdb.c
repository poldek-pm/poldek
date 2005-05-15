/*
  Copyright (C) 2000 - 2005 Pawel A. Gajda <mis@k2.net.pl>

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

#include <stdlib.h>
#include <string.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <fcntl.h>              /* for O_* */

#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
#include <trurl/narray.h>

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
        n_hash_insert(db->kw, key, val); /* pm module should free val */
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

    if (pkgdb_reopen(db, mode))
        return db;
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

int pkgdb_tx_begin(struct pkgdb *db)
{
    db->_txcnt++;
    if (db->_ctx->mod->dbtxbegin)
        db->_ctx->mod->dbtxbegin(db->dbh);
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
    return db->_ctx->mod->db_it_init(it, tag, arg);
}

void pkgdb_it_destroy(struct pkgdb_it *it)
{
    it->_destroy(it);
    memset(it, 0, sizeof(*it));
}

const struct pm_dbrec *pkgdb_it_get(struct pkgdb_it *it)
{
    return it->_get(it);
}

int pkgdb_it_get_count(struct pkgdb_it *it)
{
    return it->_get_count(it);
}

int pkgdb_map(struct pkgdb *db,
              void (*mapfn)(unsigned recno, void *header, void *arg),
              void *arg) 
{
    int n = 0;
    struct pkgdb_it it;
    const struct pm_dbrec *dbrec;

    pkgdb_it_init(db, &it, PMTAG_RECNO, NULL);
    while ((dbrec = pkgdb_it_get(&it))) {
        if (dbrec->hdr) {
            mapfn(dbrec->recno, dbrec->hdr, arg);
            n++;
        }

        if (sigint_reached()) {
            n = 0;
            break;
        }
    }
    
    pkgdb_it_destroy(&it);
    return n;
}


int pkgdb_map_nevr(struct pkgdb *db,
                   void (*mapfn)(const char *name, uint32_t epoch,
                                 const char *ver, const char *rel, void *arg),
                   void *arg) 
{
    int n = 0;
    struct pkgdb_it it;
    const struct pm_dbrec *dbrec;

    pkgdb_it_init(db, &it, PMTAG_RECNO, NULL);
    while ((dbrec = pkgdb_it_get(&it))) {
        struct pkg tmpkg;
        if (dbrec->hdr == NULL)
            continue;
        n_assert(db->_ctx->mod->hdr_nevr);
        if (db->_ctx->mod->hdr_nevr(dbrec->hdr, &tmpkg.name,
                                    &tmpkg.epoch, &tmpkg.ver, &tmpkg.rel)) {
            
            mapfn(tmpkg.name, tmpkg.epoch, tmpkg.ver, tmpkg.rel, arg);
            n++;
        }
        
        if (sigint_reached()) {
            n = 0;
            break;
        }
    }
    
    pkgdb_it_destroy(&it);
    return n;
}

static 
int pkg_hdr_cmp_evr(struct pm_ctx *ctx, void *hdr, const struct pkg *pkg,
                    int *cmprc)
{
    struct pkg tmpkg;

    if (!ctx->mod->hdr_nevr(hdr, &tmpkg.name, &tmpkg.epoch,
                            &tmpkg.ver, &tmpkg.rel))
        return 0;
    
    *cmprc = pkg_cmp_evr(pkg, &tmpkg);
    return 1;
}

int do_search_package(struct pkgdb *db, const struct pkg *pkg, int *cmprcptr,
                      struct pm_dbrec *todbrec)
{
    int count = 0, n, cmprc = 0;
    struct pkgdb_it it;
    const struct pm_dbrec *dbrec;

    pkgdb_it_init(db, &it, PMTAG_NAME, pkg->name);
    count = pkgdb_it_get_count(&it);
    if (count > 0) {
        n = 0;
        while ((dbrec = pkgdb_it_get(&it)) != NULL) {
            n++;
            if (!pkg_hdr_cmp_evr(db->_ctx, dbrec->hdr, pkg, &cmprc)) 
                continue; /* fail */

            if (cmprc == 0) {
                if (todbrec) {
                    todbrec->hdr = db->_ctx->mod->hdr_link(dbrec->hdr);
                    todbrec->recno = dbrec->recno;
                    todbrec->_ctx = db->_ctx;
                }
                break;
            }
        }
    }

    if (count > 0 && n == 0) {
        logn(LOGWARN, "%s: pkgdb iterator returns NULL, "
             "possibly corrupted %s database", pkg->name,
             db->_ctx->mod->name);
        count = 0; /* assume package isn't installed */
    }
    
    pkgdb_it_destroy(&it);
    if (cmprcptr)
        *cmprcptr = cmprc;
    return count;
}

int pkgdb_is_pkg_installed(struct pkgdb *db, const struct pkg *pkg, int *cmprc)
{
    return do_search_package(db, pkg, cmprc, NULL);
}

int pkgdb_get_package_hdr(struct pkgdb *db, const struct pkg *pkg,
                          struct pm_dbrec *dbrec)
{
    n_assert(dbrec);
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
        n_alloc_free(na); /* per pkg na (pkg hold its reference); TODO better */

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

static int dbpkg_array_has(tn_array *pkgs, int recno)
{
    struct pkg tmp;
    tmp.recno = recno;
    n_assert(n_array_ctl_get_cmpfn(pkgs) == (tn_fn_cmp)pkg_cmp_recno);
    return n_array_bsearch(pkgs, &tmp) != NULL;
}


tn_array *pkgdb_get_conflicted_dbpkgs(struct pkgdb *db,
                                      const struct capreq *cap,
                                      tn_array *unistdbpkgs, unsigned ldflags)
{
    tn_array *dbpkgs = NULL;
    struct pkgdb_it it;
    const struct pm_dbrec *dbrec;
    

    pkgdb_it_init(db, &it, PMTAG_CNFL, capreq_name(cap));
    while ((dbrec = pkgdb_it_get(&it))) {
        struct pkg *pkg;
        
        if (dbpkg_array_has(unistdbpkgs, dbrec->recno))
            continue;

        if (dbpkgs == NULL)
            dbpkgs = pkgs_array_new_ex(4, pkg_cmp_recno);

        if ((pkg = load_pkg(NULL, db, dbrec, ldflags)))
            n_array_push(dbpkgs, pkg);
    }
    
    pkgdb_it_destroy(&it);
    return dbpkgs;
}


tn_array *pkgdb_get_provides_dbpkgs(struct pkgdb *db, const struct capreq *cap,
                                    tn_array *unistdbpkgs, unsigned ldflags)
{
    tn_array *dbpkgs = NULL;
    struct pkgdb_it it;
    const struct pm_dbrec *dbrec;
    

    pkgdb_it_init(db, &it, PMTAG_CAP, capreq_name(cap));
    while ((dbrec = pkgdb_it_get(&it))) {
        struct pkg *pkg;
        if (unistdbpkgs && dbpkg_array_has(unistdbpkgs, dbrec->recno))
            continue;

        if (dbpkgs == NULL)
            dbpkgs = pkgs_array_new_ex(4, pkg_cmp_recno);
        
        else if (dbpkg_array_has(dbpkgs, dbrec->recno))
            continue;

        if ((pkg = load_pkg(NULL, db, dbrec, ldflags)))
            n_array_push(dbpkgs, pkg);
    }
    
    pkgdb_it_destroy(&it);
    return dbpkgs;
}


tn_array *pkgdb_get_file_conflicted_dbpkgs(struct pkgdb *db, const char *path,
                                           tn_array *cnfldbpkgs, 
                                           tn_array *unistdbpkgs,
                                           unsigned ldflags)
{
    const struct pm_dbrec *dbrec;
    struct pkgdb_it it;

    if (cnfldbpkgs == NULL)
        cnfldbpkgs = pkgs_array_new_ex(4, pkg_cmp_recno);
    
    pkgdb_it_init(db, &it, PMTAG_FILE, path);
    while ((dbrec = pkgdb_it_get(&it)) != NULL) {
        struct pkg *pkg;
        if (unistdbpkgs && dbpkg_array_has(unistdbpkgs, dbrec->recno))
            continue;
        
        if (cnfldbpkgs && dbpkg_array_has(cnfldbpkgs, dbrec->recno))
            continue;

        if ((pkg = load_pkg(NULL, db, dbrec, ldflags)))
            n_array_push(cnfldbpkgs, pkg);
        break;	
    }
    pkgdb_it_destroy(&it);
    
    if (n_array_size(cnfldbpkgs) == 0) {
        n_array_free(cnfldbpkgs);
        cnfldbpkgs = NULL;
    }
    
    return cnfldbpkgs;
}



static
int lookup_file(struct pkgdb *db, const struct capreq *req, tn_array *unistdbpkgs)
{
    struct pkgdb_it it;
    const struct pm_dbrec *dbrec;
    int found = 0;
    
    pkgdb_it_init(db, &it, PMTAG_FILE, capreq_name(req));
    while ((dbrec = pkgdb_it_get(&it)) != NULL) {
        if (unistdbpkgs && dbpkg_array_has(unistdbpkgs, dbrec->recno))
            continue;
        found = 1;
        break;	
    }
    pkgdb_it_destroy(&it);
    return found;
}


static
int header_evr_match_req(struct pm_ctx *ctx, void *hdr,
                         const struct capreq *req)
{
    struct pkg  pkg;

    if (!ctx->mod->hdr_nevr(hdr, &pkg.name, &pkg.epoch, &pkg.ver, &pkg.rel))
        return -1;

    DBGF("%s match %s?\n", pkg_evr_snprintf_s(&pkg), 
         capreq_snprintf_s0(req));
    
    if (pkg_evr_match_req(&pkg, req, POLDEK_MA_PROMOTE_VERSION)) {
        DBGF("%s match %s!\n", pkg_evr_snprintf_s(&pkg), 
             capreq_snprintf_s0(req));
        return 1;
    }

    return 0;
}


static
int header_cap_match_req(struct pm_ctx *ctx, void *hdr,
                         const struct capreq *req, int strict)
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
        rc = pkg_caps_match_req(&pkg, req, strict ? 0 : POLDEK_MA_PROMOTE_VERSION);
    }

    n_array_free(pkg.caps);
    return rc;
}


static
int lookup_cap(struct pkgdb *db, const struct capreq *req, int strict,
               tn_array *unistdbpkgs)
{
    struct pkgdb_it it;
    const struct pm_dbrec *dbrec;
    int rc = 0;
    
    pkgdb_it_init(db, &it, PMTAG_CAP, capreq_name(req));
    while ((dbrec = pkgdb_it_get(&it)) != NULL) {
        if (unistdbpkgs && dbpkg_array_has(unistdbpkgs, dbrec->recno))
            continue;
        
        if (header_cap_match_req(db->_ctx, dbrec->hdr, req, strict)) {
            rc = 1;
            break;
        }
    }
    pkgdb_it_destroy(&it);
    return rc;
}


static
int lookup_pkg(struct pkgdb *db, const struct capreq *req, tn_array *unistdbpkgs)
{
    int rc = 0;

    const struct pm_dbrec *dbrec;
    struct pkgdb_it it;

    pkgdb_it_init(db, &it, PMTAG_NAME, capreq_name(req));
    while ((dbrec = pkgdb_it_get(&it)) != NULL) {
        if (unistdbpkgs && dbpkg_array_has(unistdbpkgs, dbrec->recno))
            continue;

        if (header_evr_match_req(db->_ctx, dbrec->hdr, req)) {
            rc = 1;
            break;
        }
    }
    pkgdb_it_destroy(&it);
    
    return rc;
}


int pkgdb_match_req(struct pkgdb *db, const struct capreq *req, int strict,
                    tn_array *unistallrnos) 
{
    int is_file;

    is_file = (*capreq_name(req) == '/' ? 1 : 0);

    if (!is_file && lookup_pkg(db, req, unistallrnos) > 0)
        return 1;
    
    if (lookup_cap(db, req, strict, unistallrnos) > 0)
        return 1;

    if (is_file && lookup_file(db, req, unistallrnos) > 0)
        return 1;
    
    return 0;
}


int pkgdb_get_pkgs_requires_capn(struct pkgdb *db, tn_array *dbpkgs, const char *capname,
                                 tn_array *unistdbpkgs, unsigned ldflags)
{
    struct pkgdb_it it;
    const struct pm_dbrec *dbrec;
    int n = 0;
    
    pkgdb_it_init(db, &it, PMTAG_REQ, capname);
    while ((dbrec = pkgdb_it_get(&it)) != NULL) {
        struct pkg *pkg;
        
        if (unistdbpkgs && dbpkg_array_has(unistdbpkgs, dbrec->recno))
            continue;
        
        if (dbpkg_array_has(dbpkgs, dbrec->recno))
            continue;

        
        if ((pkg = load_pkg(NULL, db, dbrec, ldflags))) {
            n_array_push(dbpkgs, pkg);
            //msg(1, "%s <- %s\n", capname, pkg_snprintf_s(dbpkg->pkg));
            //mem_info(1, "get_pkgs_requires_capn\n");
            n++;
        }
    }
    pkgdb_it_destroy(&it);
    return n;
}

static
int get_obsoletedby_cap(struct pkgdb *db, int tag, tn_array *dbpkgs,
                        struct capreq *cap, unsigned ldflags)
{
    struct pkgdb_it it;
    const struct pm_dbrec *dbrec;
    int n = 0;
    
    pkgdb_it_init(db, &it, tag, capreq_name(cap));
    while ((dbrec = pkgdb_it_get(&it)) != NULL) {
        int add = 0;
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


int pkgdb_get_obsoletedby_cap(struct pkgdb *db, tn_array *dbpkgs, struct capreq *cap,
                              unsigned ldflags)
{
    return get_obsoletedby_cap(db, PMTAG_NAME, dbpkgs, cap, ldflags);
}

static
int get_obsoletedby_pkg_nevr(struct pkgdb *db, tn_array *dbpkgs,
                             const struct pkg *pkg, unsigned ldflags, int rev)
{
    struct capreq *self_cap;
    int n, relflags = REL_EQ | REL_LT;

    if (rev)
        relflags = REL_EQ | REL_GT;
    
    self_cap = capreq_new(NULL, pkg->name, pkg->epoch, pkg->ver, pkg->rel,
                          relflags, 0);
    n = pkgdb_get_obsoletedby_cap(db, dbpkgs, self_cap, ldflags);
    capreq_free(self_cap);
    return n;
}

int pkgdb_get_obsoletedby_pkg(struct pkgdb *db, tn_array *dbpkgs,
                              const struct pkg *pkg, unsigned flags,
                              unsigned ldflags)
{
    int i, n;

    n_assert(flags & PKGDB_GETF_OBSOLETEDBY_NEVR);
    
    n = get_obsoletedby_pkg_nevr(db, dbpkgs, pkg, ldflags,
                                 flags & PKGDB_GETF_OBSOLETEDBY_REV);

    if ((flags & PKGDB_GETF_OBSOLETEDBY_OBSL) == 0)
        return n;
    
    if (pkg->cnfls == NULL)
        return n;
    
    for (i=0; i < n_array_size(pkg->cnfls); i++) {
        struct capreq *cnfl = n_array_nth(pkg->cnfls, i);

        if (!capreq_is_obsl(cnfl))
            continue;
/* FIXME: is reverse match should be performed there too? */
        n += get_obsoletedby_cap(db, PMTAG_NAME, dbpkgs, cnfl, ldflags);
#ifdef HAVE_RPM_4_1             /* TODO -- code this in pm's module */
        n += get_obsoletedby_cap(db, PMTAG_CAP, dbpkgs, cnfl, ldflags);
#endif 
    }
    
    return n;
}


struct pkgdir *pkgdb_to_pkgdir(struct pm_ctx *ctx, const char *rootdir,
                               const char *path, const char *key, ...)
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
                                    db->path, db->kw);
    pkgdb_free(db);
    return pkgdir;
}


int pkgdb_q_what_requires(struct pkgdb *db, tn_array *dbpkgs,
                          const struct capreq *cap,
                          tn_array *blacklist, unsigned ldflags)
{
    struct pkgdb_it it;
    const struct pm_dbrec *dbrec;
    int n = 0;
    
    pkgdb_it_init(db, &it, PMTAG_REQ, capreq_name(cap));
    while ((dbrec = pkgdb_it_get(&it)) != NULL) {
        struct pkg *pkg;
        
        if (blacklist && dbpkg_array_has(blacklist, dbrec->recno))
            continue;
#if ENABLE_TRACE        
        pkg = load_pkg(NULL, db, dbrec, ldflags);
        DBGF("%s <- %s ????\n", capreq_name(cap), pkg_snprintf_s(pkg));
#endif        
        if (dbpkg_array_has(dbpkgs, dbrec->recno))
            continue;
#if ENABLE_TRACE        
        DBGF("%s <- %s ??\n", capreq_name(cap), pkg_snprintf_s(pkg));
#endif        
        if ((pkg = load_pkg(NULL, db, dbrec, ldflags))) {
            if (pkg_satisfies_req(pkg, cap, 1)) { /* self matched? */
                pkg_free(pkg);
                
            } else {
                n_array_push(dbpkgs, pkg);
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
            n++;
        }
    }
    pkgdb_it_destroy(&it);
    return n;
}
