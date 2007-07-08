/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

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

#include "ictx.h"

#define DBPKG_ORPHANS_PROCESSED   (1 << 15) /* is its orphan processed ?*/
#define DBPKG_DEPS_PROCESSED      (1 << 16) /* is its deps processed? */
#define DBPKG_TOUCHED             (1 << 17)

static
int is_iset_provides(struct install_ctx *ictx,
                     const struct pkg *pkg, struct capreq *cap)
{
    struct pkg *tomark = NULL;
    
    if (in_find_req(ictx, pkg, cap, &tomark, NULL, IN_FIND_REQ_BEST) &&
        tomark == NULL) {
        //printf("cap satisfied %s\n", capreq_snprintf_s(cap));
        return 1;
    }
    return 0;
}

static
int is_iset_provides_capn(struct install_ctx *ictx,
                          const struct pkg *pkg, const char *capn)
{
    struct capreq *cap;
    
    capreq_new_name_a(capn, cap);
    return is_iset_provides(ictx, pkg, cap);
}

static
int get_pkgs_requires_capn(struct pkgdb *db, tn_array *dbpkgs,
                           const char *capname,
                           const tn_array *exclude, unsigned ldflags)
{
    return pkgdb_search(db, &dbpkgs, PMTAG_REQ, capname, exclude, ldflags);
}


/* add to ictx->orphan_dbpkgs packages required by pkg */
static int process_pkg_orphans(struct install_ctx *ictx, struct pkg *pkg)
{
    unsigned ldflags = PKG_LDNEVR | PKG_LDREQS;
    int i, n = 0;
    struct pkgdb *db;

    if (sigint_reached())
        return 0;
    
    db = ictx->ts->db;
    DBGF("%s\n", pkg_id(pkg));
    MEMINF("process_pkg_orphans:");

    if (!is_iset_provides_capn(ictx, pkg, pkg->name))
        n += get_pkgs_requires_capn(db, ictx->orphan_dbpkgs, pkg->name,
                                    ictx->uninst_set->dbpkgs, ldflags);
        
    if (pkg->caps)
        for (i=0; i < n_array_size(pkg->caps); i++) {
            struct capreq *cap = n_array_nth(pkg->caps, i);
            int strict = 0;
            
            if (is_iset_provides(ictx, pkg, cap)) 
                continue;

            if (capreq_versioned(cap) &&
                is_iset_provides_capn(ictx, pkg, capreq_name(cap)))
                strict = 1;
            
            n += pkgdb_q_what_requires(db, ictx->orphan_dbpkgs, cap,
                                       ictx->uninst_set->dbpkgs,
                                       ldflags, strict);
        }
    
    if (pkg->fl) {
        struct pkgfl_it it;
        const char *path;

        pkgfl_it_init(&it, pkg->fl);
        while ((path = pkgfl_it_get(&it, NULL))) {
            if (!is_iset_provides_capn(ictx, pkg, path)) 
                n += get_pkgs_requires_capn(db, ictx->orphan_dbpkgs, path,
                                            ictx->uninst_set->dbpkgs,
                                            ldflags);
        }
    }
    
    return n;
}


static
int verify_unistalled_cap(int indent, struct install_ctx *ictx,
                          struct capreq *cap, struct pkg *pkg)
{
    struct db_dep *db_dep;
    struct capreq *req;

    DBGF("VUN %s: %s\n", pkg_id(pkg), capreq_snprintf_s(cap));
    if ((db_dep = db_deps_contains(ictx->db_deps, cap, 0)) == NULL) {
        DBG("  [1] -> NO in db_deps\n");
        return 1;
    }

    if (db_dep->spkg && pkg_is_marked(ictx->ts->pms, db_dep->spkg)) {
        DBG("  [1] -> marked %s\n", pkg_id(db_dep->spkg));
        return 1;
    }
    
    DBG("  spkg %s\n", db_dep->spkg ? pkg_id(db_dep->spkg) : "NO");
    req = db_dep->req;

    // still satisfied by db? 
    if (pkgdb_match_req(ictx->ts->db, req, ictx->strict,
                        ictx->uninst_set->dbpkgs)) {
        DBG("  [1] -> satisfied by db\n");
        return 1;
    }

    if (db_dep->spkg && is_iset_provides(ictx, NULL, req)) {
        if (poldek_VERBOSE > 1)
            logn(LOGWARN, "cap %s satisfied by install set, shouldn't happen",
                 capreq_snprintf_s(req));
        DBGF("cap %s satisfied by install set\n", capreq_snprintf_s(req));
        return 1;
    }
    
    if (db_dep->spkg &&
        !in_is_marked_for_removal_by_req(ictx, db_dep->spkg, req) &&
        !in_is_other_version_marked(ictx, db_dep->spkg, req))
    {
        struct pkg *marker;
        int rc;
        
        n_assert(n_array_size(db_dep->pkgs));
        marker = n_array_nth(db_dep->pkgs, 0);
        rc = in_dep_mark_package(indent, ictx, db_dep->spkg, marker, req,
                                 PROCESS_AS_ORPHAN);
        if (!rc)
            return 0;
        
        return in_process_package(indent, ictx, db_dep->spkg, PROCESS_AS_NEW);
    }
    
    if (db_dep->flags & PROCESS_AS_NEW) {
        int i, n;
        char errmsg[4096];

        n = n_snprintf(errmsg, sizeof(errmsg), _("%s is required by "), 
                       capreq_snprintf_s(req));
        
        for (i=0; i < n_array_size(db_dep->pkgs); i++) {
            struct pkg *p = n_array_nth(db_dep->pkgs, i);
        
            n_snprintf(&errmsg[n], sizeof(errmsg) - n, "%s%s",
                       (p->flags & PKG_DBPKG) ? "" : "already marked ", 
                       pkg_id(p));
            
            logn(LOGERR, "%s", errmsg);
        }
        
        pkg_set_unmetdeps(ictx->unmetpms, pkg);
        ictx->nerr_dep++;
            
                
    } else if (db_dep->flags & PROCESS_AS_ORPHAN) {
        int i;
        tn_array *pkgs;

        n_assert(db_dep->pkgs);
        pkgs = n_array_clone(db_dep->pkgs);
        for (i=0; i < n_array_size(db_dep->pkgs); i++) {
            struct pkg *pp = n_array_nth(db_dep->pkgs, i);
            if (n_array_has_free_fn(db_dep->pkgs))
                pp = pkg_link(pp);
            
            n_array_push(pkgs, pp);
        }
        

        for (i=0; i < n_array_size(pkgs); i++) {
            //for (i=0; db_dep->pkgs && i < n_array_size(db_dep->pkgs); i++) {
            struct pkg *opkg = n_array_nth(pkgs, i);
            struct pkg *p;
            int not_found = 1;

            
            if (pkg_cmp_name_evr(opkg, pkg) == 0) /* packages orphanes itself */
                continue;
                
            if ((p = in_select_pkg(ictx, opkg, ictx->ps->pkgs))) {
                if (pkg_is_marked_i(ictx->ts->pms, p))
                    in_mark_package(ictx, p);

                if (pkg_is_marked(ictx->ts->pms, p)) {
                    in_process_package(-2, ictx, p, PROCESS_AS_NEW);
                    not_found = 0;
                        
                } else if (ictx->ts->getop(ictx->ts, POLDEK_OP_GREEDY)) {
                    if (in_greedy_mark_package(indent, ictx, p, opkg, req)) {
                        in_process_package(indent, ictx, p, PROCESS_AS_NEW);
                        not_found = 0;
                    }
                }
            }
            
            if (not_found) {
                logn(LOGERR, _("%s (cap %s) is required by %s"),
                     pkg_id(pkg), capreq_snprintf_s(req),
                     pkg_id(opkg));
                
                
                pkg_set_unmetdeps(ictx->unmetpms, pkg);
                ictx->nerr_dep++;
            }
        }
        n_array_free(pkgs);
    }
    
    return 1;
}


static int obs_filter(struct pkgdb *db, const struct pm_dbrec *dbrec,
                      void *apkg) 
{
    struct pkg dbpkg, *pkg = apkg;
    char *arch;

    db = db;
    if (dbrec->hdr == NULL)
        return 0;
    
    if (!pm_dbrec_nevr(dbrec, &dbpkg.name, &dbpkg.epoch,
                       &dbpkg.ver, &dbpkg.rel, &arch, &dbpkg.color))
        return 0;

    if (arch) 
        pkg_set_arch(&dbpkg, arch);
    
    DBGF("%s.%s:%d colored like %s:%d => %d\n", pkg_evr_snprintf_s(&dbpkg),
         arch, dbpkg.color,
         pkg_id(pkg), pkg->color, pkg_is_colored_like(&dbpkg, pkg));
        
    if (pkg_is_colored_like(&dbpkg, pkg))
        return 1;
    
    return 0;
}

static
int get_obsoletedby_pkg(struct pkgdb *db, tn_array *unpkgs, struct pkg *pkg,
                        unsigned getflags, unsigned ldflags) 
{
    tn_array *dbpkgs;
    int n;
    
    if (poldek_conf_MULTILIB)
        pkgdb_set_filter(db, obs_filter, pkg);
    
    dbpkgs = pkgs_array_new_ex(16, pkg_cmp_recno);    
    n = pkgdb_q_obsoletedby_pkg(db, dbpkgs, pkg, getflags, unpkgs, ldflags);
    
    if (poldek_conf_MULTILIB)
        pkgdb_set_filter(db, NULL, NULL);

    while (n_array_size(dbpkgs)) {
        struct pkg *p = n_array_shift(dbpkgs);
        n_array_push(unpkgs, p);
    }
    n_array_free(dbpkgs);
    
    return n;
}


int in_process_pkg_obsoletes(int indent, struct install_ctx *ictx,
                             struct pkg *pkg)
{
    tn_array *orphans;
    struct pkgdb *db = ictx->ts->db;
    unsigned getflags = PKGDB_GETF_OBSOLETEDBY_NEVR;
    int n, i;
    
    if (!poldek_ts_issetf(ictx->ts, POLDEK_TS_UPGRADE))
        return 1;

    if (sigint_reached())
        return 0;
    
    DBGF("%s\n", pkg_id(pkg));

    if (ictx->ts->getop(ictx->ts, POLDEK_OP_OBSOLETES))
        getflags |= PKGDB_GETF_OBSOLETEDBY_OBSL;

    if (poldek_ts_issetf(ictx->ts, POLDEK_TS_DOWNGRADE))
        getflags |= PKGDB_GETF_OBSOLETEDBY_REV;

    
    n = get_obsoletedby_pkg(db, ictx->uninst_set->dbpkgs, pkg, getflags,
                            PKG_LDWHOLE_FLDEPDIRS);
    
    DBGF("%s, n = %d\n", pkg_id(pkg), n);
    if (n == 0)
        return 1;
    
    n = 0;
    for (i=0; i < n_array_size(ictx->uninst_set->dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(ictx->uninst_set->dbpkgs, i);
        if (pkgmark_isset(ictx->dbpms, dbpkg, DBPKG_TOUCHED))
            continue;
        
        msgn_i(1, indent, _("%s obsoleted by %s"), pkg_id(dbpkg), pkg_id(pkg));
        pkg_rm_mark(ictx->ts->pms, dbpkg);
        db_deps_remove_pkg(ictx->db_deps, dbpkg);
        db_deps_remove_pkg_caps(ictx->db_deps, pkg,
                                (ictx->ps->flags & PSET_DBDIRS_LOADED) == 0);
        
        pkgmark_set(ictx->dbpms, dbpkg, 1, DBPKG_TOUCHED);
        
        DBGF("verifyuninstalled %s caps\n", pkg_id(dbpkg));
        if (dbpkg->caps) {
            int j;
            for (j=0; j < n_array_size(dbpkg->caps); j++) {
                struct capreq *cap = n_array_nth(dbpkg->caps, j);
                verify_unistalled_cap(indent, ictx, cap, dbpkg);
            }
        }
        DBGF("verifyuninstalled %s files? => %s \n", pkg_id(dbpkg), 
             dbpkg->fl ? "YES" : "NO");

        if (dbpkg->fl) {
            struct capreq *cap;
            struct pkgfl_it it;
            const char *path;
            
            cap = alloca(sizeof(cap) + PATH_MAX);
            memset(cap, 0, sizeof(*cap));
            cap->_buf[0] = '\0';

            pkgfl_it_init(&it, dbpkg->fl);
            while ((path = pkgfl_it_get(&it, NULL))) {
                int len = strlen(path);
                if (len < PATH_MAX - 2) {
                    memcpy(&cap->_buf[1], path, len + 1); /* XXX: hacky */
                    verify_unistalled_cap(indent, ictx, cap, dbpkg);
                }
            }
        }
        n += process_pkg_orphans(ictx, dbpkg);
    }

    if (n == 0)
        return 1;

    orphans = pkgs_array_new(n_array_size(ictx->orphan_dbpkgs));
    for (i=0; i<n_array_size(ictx->orphan_dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(ictx->orphan_dbpkgs, i);
        if (pkgmark_isset(ictx->dbpms, dbpkg, DBPKG_DEPS_PROCESSED))
            continue;
        pkgmark_set(ictx->dbpms, dbpkg, 1, DBPKG_DEPS_PROCESSED);
        n_array_push(orphans, pkg_link(dbpkg));
    }

    for (i=0; i<n_array_size(orphans); i++) {
        struct pkg *pkg = n_array_nth(orphans, i);
        in_process_package(indent, ictx, pkg, PROCESS_AS_ORPHAN);
    }
    
    n_array_free(orphans);
    return 1;
}
