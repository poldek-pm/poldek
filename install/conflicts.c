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

#include "ictx.h"

static
int find_replacement(struct install_ctx *ictx, struct pkg *pkg,
                     struct pkg **rpkg)
{
    tn_array *pkgs;
    struct pkg *bypkg = NULL;
    struct pkgset *ps;

    ps = ictx->ts->ctx->ps;
    *rpkg = NULL;

    /* simply higest version */
    if ((bypkg = pkgset_lookup_1package(ps, pkg->name)) && 
        pkg_cmp_name_evr(bypkg, pkg) > 0) {
        
        *rpkg = bypkg;
        
    } else if ((pkgs = pkgset_search(ps, PS_SEARCH_OBSL, pkg->name))) {
        int i;
        
        for (i=0; i < n_array_size(pkgs); i++) {
            struct pkg *p = n_array_nth(pkgs, i);
            if (pkg_caps_obsoletes_pkg_caps(p, pkg) &&
                pkg_cmp_name_evr(p, pkg) > 0) { /* XXX bug propably, testit */
                *rpkg = p;
                break;
            }
        }
        n_array_free(pkgs);
    }

    if (*rpkg && pkg_is_marked(ictx->ts->pms, *rpkg)) {
        *rpkg = NULL;
        return 1;
    }
    	
    return (*rpkg != NULL);
}

/* trying to resolve conflict by package upgrade */
int in_resolve_conflict(int indent, struct install_ctx *ictx,
                        struct pkg *pkg, const struct capreq *cnfl,
                        struct pkg *dbpkg)
{
    struct capreq *req = NULL;
    struct pkg *tomark = NULL;
    struct pkg **tomark_candidates = NULL, ***tomark_candidates_ptr = NULL;
    int found = 0, by_replacement = 0;

    if (!ictx->ts->getop(ictx->ts, POLDEK_OP_FOLLOW))
        return 0;

    if (!capreq_versioned(cnfl))
        return 0;
    
    req = capreq_clone(NULL, cnfl);
    
#if 0    
    printf("B %s -> ", capreq_snprintf_s(req));
    capreq_revrel(req);
    printf("%s -> ", capreq_snprintf_s(req));
    capreq_revrel(req);
    printf("%s\n", capreq_snprintf_s(req));
#endif

    DBGF("find_req %s %s\n", pkg_id(pkg), capreq_snprintf_s(req));
    capreq_revrel(req);
    DBGF("find_req %s %s\n", pkg_id(pkg), capreq_snprintf_s(req));

    
    if (ictx->ts->getop(ictx->ts, POLDEK_OP_EQPKG_ASKUSER) && ictx->ts->askpkg_fn)
        tomark_candidates_ptr = &tomark_candidates;

    found = in_find_req(ictx, pkg, req, &tomark, tomark_candidates_ptr,
                        IN_FIND_REQ_BEST);
    capreq_revrel(req);
    
    if (found) {
        struct pkg *real_tomark = tomark;
        if (tomark_candidates) {
            int n;
            n = ictx->ts->askpkg_fn(capreq_snprintf_s(req), tomark_candidates, tomark);
            real_tomark = tomark_candidates[n];
            n_cfree(&tomark_candidates);
        }
        tomark = real_tomark;
        
    } else { // !found
        found = find_replacement(ictx, dbpkg, &tomark);
        by_replacement = 1;
    }
    	
    if (!found)
        goto l_end;
        
    if (tomark == NULL)   /* already in install set */
        goto l_end;
    
    found = 0;
    if (by_replacement || pkg_obsoletes_pkg(tomark, dbpkg)) {
        if (pkg_is_marked_i(ictx->ts->pms, tomark)) {
            //msg_i(1, indent, "%s 'MARX' => %s (cnfl %s)\n",
            //      pkg_id(pkg), pkg_id(tomark),
            //      capreq_snprintf_s(req));
            found = in_mark_package(ictx, tomark);
            indent = -2;
                
        } else {
            //msg_i(1, indent, "%s 'DEPMARX' => %s (cnfl %s)\n",
            //      pkg_id(pkg), pkg_id(tomark),
            //      capreq_snprintf_s(req));
            found = in_dep_mark_package(indent, ictx, tomark, pkg, req,
                                        PROCESS_AS_NEW);
        }
        
        if (found)
            in_process_package(indent, ictx, tomark, PROCESS_AS_NEW);
    }
    
l_end:
    capreq_free(req);
    return found;
}


/* check if cnfl conflicts with db */
static
int find_db_conflicts_cnfl_with_db(int indent, struct install_ctx *ictx,
                                   struct pkg *pkg, const struct capreq *cnfl)
{
    int i, ncnfl = 0;
    tn_hash *ht = NULL;
    tn_array *dbpkgs;

    dbpkgs = pkgdb_get_provides_dbpkgs(ictx->ts->db, cnfl,
                                       ictx->uninst_set->dbpkgs,
                                       PKG_LDWHOLE_FLDEPDIRS);
    if (dbpkgs == NULL)
        return 0;
                
    msgn_i(4, indent, "Processing conflict %s:%s...", pkg_id(pkg),
           capreq_snprintf_s(cnfl));
    
    if (ictx->ts->getop(ictx->ts, POLDEK_OP_ALLOWDUPS) &&
        n_array_size(dbpkgs) > 1) {
        
        ht = n_hash_new(21, NULL);
        n_hash_ctl(ht, TN_HASH_NOCPKEY);
        
        for (i=0; i<n_array_size(dbpkgs); i++) {
            struct pkg *dbpkg = n_array_nth(dbpkgs, i);
            if (n_hash_exists(ht, dbpkg->name))
                continue;
            
            if (!pkg_match_req(dbpkg, cnfl, 1)) {
                msgn_i(5, indent, "%s: conflict disarmed by %s",
                       capreq_snprintf_s(cnfl), pkg_id(dbpkg));
                n_hash_insert(ht, dbpkg->name, pkg);
            }
        }
    }
    
    for (i=0; i<n_array_size(dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(dbpkgs, i);
        
        msg_i(6, indent, "%d. %s (%s) <-> %s ?\n", i, pkg_id(pkg),
            capreq_snprintf_s(cnfl), pkg_id(dbpkg));
        
        if (ht && n_hash_exists(ht, dbpkg->name))
            continue;
        
        if (pkg_match_req(dbpkg, cnfl, 1)) {
            if (!in_resolve_conflict(indent, ictx, pkg, cnfl, dbpkg)) {
                logn(LOGERR, _("%s (cnfl %s) conflicts with installed %s"),
                    pkg_id(pkg), capreq_snprintf_s(cnfl), pkg_id(dbpkg));
                ncnfl++;
            }
        }
    }

    if (ht)
        n_hash_free(ht);

    n_array_free(dbpkgs);
    
    return ncnfl;
}

/* check if db cnfl conflicts with cap */
static
int find_db_conflicts_dbcnfl_with_cap(int indent, struct install_ctx *ictx,
                                      struct pkg *pkg, const struct capreq *cap)
{
    int i, j, ncnfl = 0;
    tn_array *dbpkgs;

    dbpkgs = pkgdb_get_conflicted_dbpkgs(ictx->ts->db, cap,
                                         ictx->uninst_set->dbpkgs,
                                         PKG_LDWHOLE_FLDEPDIRS);
    if (dbpkgs == NULL)
        return 0;

    for (i = 0; i < n_array_size(dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(dbpkgs, i);
        
        msg(6, "%s (%s) <-> %s ?\n", pkg_id(pkg),
            capreq_snprintf_s(cap), pkg_id(dbpkg));
        
        for (j = 0; j < n_array_size(dbpkg->cnfls); j++) {
            struct capreq *cnfl = n_array_nth(dbpkg->cnfls, j);
            if (cap_match_req(cap, cnfl, 1)) {
                if (!in_resolve_conflict(indent, ictx, pkg, cnfl, dbpkg)) {
                    logn(LOGERR, _("%s (cap %s) conflicts with installed %s (%s)"),
                        pkg_id(pkg), capreq_snprintf_s(cap), 
                        pkg_id(dbpkg), capreq_snprintf_s0(cnfl));
                    ncnfl++;
                }
            }
        }
    }
    n_array_free(dbpkgs);
    return ncnfl;
}

int in_process_pkg_conflicts(int indent, struct install_ctx *ictx,
                             struct pkg *pkg)
{
    struct pkgdb *db;
    int i, n, ncnfl = 0;

    db = ictx->ts->db;

    if (!ictx->ts->getop(ictx->ts, POLDEK_OP_CONFLICTS))
        return 1;
    
    /* conflicts in install set */
    if (pkg->cnflpkgs != NULL)
        for (i = 0; i < n_array_size(pkg->cnflpkgs); i++) {
            struct reqpkg *cpkg = n_array_nth(pkg->cnflpkgs, i);

            if (pkg_is_marked(ictx->ts->pms, cpkg->pkg)) {
                logn(LOGERR, _("%s conflicts with %s"), pkg_id(pkg),
                     pkg_id(cpkg->pkg));
                ictx->nerr_cnfl++;
                ncnfl++;
            }
        }

    
    /* conflicts with db packages */

    for (i = 0; i < n_array_size(pkg->caps); i++) {
        struct capreq *cap = n_array_nth(pkg->caps, i);
        
        if ((ictx->ps->flags & PSET_VRFY_MERCY) && capreq_is_bastard(cap))
            continue;
        
        msg_i(3, indent, "cap %s\n", capreq_snprintf_s(cap));
        n = find_db_conflicts_dbcnfl_with_cap(indent, ictx, pkg, cap);
        ictx->nerr_cnfl += n;
        ictx->nerr_dbcnfl += n;
        ncnfl += n;
        if (n)
            pkg_set_unmetdeps(ictx->unmetpms, pkg);
    }
        
        
    if (pkg->cnfls != NULL)
        for (i = 0; i < n_array_size(pkg->cnfls); i++) {
            struct capreq *cnfl = n_array_nth(pkg->cnfls, i);
            
            if (capreq_is_obsl(cnfl))
                continue;

            msg_i(3, indent, "cnfl %s\n", capreq_snprintf_s(cnfl));
                
            n = find_db_conflicts_cnfl_with_db(indent, ictx, pkg, cnfl);
            ictx->nerr_cnfl += n;
            ictx->nerr_dbcnfl += n;
            ncnfl += n;
            if (n)
                pkg_set_unmetdeps(ictx->unmetpms, pkg);
        }
        
#ifdef ENABLE_FILES_CONFLICTS  /* too slow, needs rpmlib API modifcations */
    msgn(1, "%s's files...", pkg_id(pkg));
    ncnfl += find_db_files_conflicts(pkg, ictx->ts->db, ps, 
                                     ictx->uninst_set->dbpkgs, ictx->strict);
#endif        
        
    return ncnfl == 0;
}
