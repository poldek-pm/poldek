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

int in_is_pkg_installed(struct install_ctx *ictx, struct pkg *pkg, int *cmprc) 
{
    tn_array *dbpkgs = NULL;
    int n;
    
    n = pkgdb_search(ictx->ts->db, &dbpkgs, PMTAG_NAME, pkg->name,
                     NULL, PKG_LDNEVR);
    n_assert(n >= 0);
    if (n == 0) {
        n_assert(dbpkgs == NULL);
        return 0;
    }

    if (poldek_conf_MULTILIB) {
        int i;
        tn_array *arr = n_array_clone(dbpkgs);
        
        for (i=0; i < n_array_size(dbpkgs); i++) {
            struct pkg *dbpkg = n_array_nth(dbpkgs, i);
            
            if (pkg_is_kind_of(pkg, dbpkg))
                n_array_push(arr, pkg_link(dbpkg));
        }

        n_array_cfree(&dbpkgs);
        dbpkgs = arr;
        n = n_array_size(arr);
    }

    if (n) {
        struct pkg *dbpkg = n_array_nth(dbpkgs, 0);
        *cmprc = pkg_cmp_evr(pkg, dbpkg);
    }

    n_array_free(dbpkgs);
    return n;
}


/* RET: 0 - not installable,  1 - installable,  -1 - something wrong */
int in_is_pkg_installable(struct install_ctx *ictx, struct pkg *pkg,
                          int is_hand_marked)
{
    int cmprc = 0, npkgs, installable = 1, freshen = 0, force;
    struct poldek_ts *ts = ictx->ts;
    
    freshen = ts->getop(ts, POLDEK_OP_FRESHEN);
    force = ts->getop(ts, POLDEK_OP_FORCE);
    npkgs = in_is_pkg_installed(ictx, pkg, &cmprc);
    
    if (npkgs < 0) 
        die();

    n_assert(npkgs >= 0);

    installable = 1;
    
    if (npkgs == 0) {
        if (is_hand_marked && freshen)
            installable = 0;
        
    } else if (is_hand_marked && npkgs > 1 &&
               poldek_ts_issetf(ts, POLDEK_TS_UPGRADE) && force == 0) {
        logn(LOGERR, _("%s: multiple instances installed, give up"), pkg->name);
        installable = -1;
        
    } else {
        if (pkg_is_scored(pkg, PKG_HELD) && ts->getop(ts, POLDEK_OP_HOLD)) {
            logn(LOGERR, _("%s: refusing to upgrade held package"),
                 pkg_id(pkg));
            installable = 0;
            
        } else if (cmprc <= 0 && force == 0 &&
                   (poldek_ts_issetf(ts, POLDEK_TS_UPGRADE) || cmprc == 0)) {
            char *msg = "%s: %s version installed, %s";
            char *eqs = cmprc == 0 ? "equal" : "newer";
            char *skiped =  "skipped";
            char *giveup =  "give up";

            installable = 0;
            
            if (cmprc == 0 && poldek_ts_issetf(ts, POLDEK_TS_REINSTALL)) {
                installable = 1;
                
            } else if (cmprc < 0 && poldek_ts_issetf(ts, POLDEK_TS_DOWNGRADE)) {
                installable = 1;
            
            } else if (!is_hand_marked) {
                logn(LOGERR, msg, pkg_id(pkg), eqs, giveup);
                installable = -1;
                
            } else if (is_hand_marked && !freshen) { /* msg without "freshen" */
                msgn(0, msg, pkg_id(pkg), eqs, skiped);
                
            }
        }
    }

    return installable;
}

int in_pkg_drags(struct install_ctx *ictx, struct pkg *pkg)
{
    int i, ntoinstall = 0;
    
    if (ictx->nerr_fatal || pkg->reqs == NULL)
        return ntoinstall;
    
    DBGF("start %s\n", pkg_id(pkg));
    
    for (i=0; i < n_array_size(pkg->reqs); i++) {
        struct capreq *true_req, *req = NULL;
        struct pkg    *tomark = NULL;
        
        true_req = n_array_nth(pkg->reqs, i);
        
        if (capreq_is_rpmlib(true_req)) 
            continue;

        capreq_new_name_a(capreq_name(true_req), req);

        if (in_find_req(ictx, pkg, req, &tomark, NULL, IN_FIND_REQ_NIL)) {
            if (tomark == NULL) /* satisfied by already being installed set */
                continue;
        }
        
        DBGF("req %s tomark=%s\n", capreq_snprintf_s(true_req),
             tomark ? pkg_id(tomark) : "NONE");
        
        /* cached */
        if (db_deps_provides(ictx->db_deps, req, DBDEP_DBSATISFIED)) {
            DBGF("%s: satisfied by db [cached]\n", capreq_snprintf_s(req));
            
        } else if (tomark && in_is_marked_for_removal(ictx, tomark)) {
            DBGF("%s: marked for removal\n", pkg_id(tomark));
            
        } else if (pkgdb_match_req(ictx->ts->db, req, 1,
                                   ictx->uninst_set->dbpkgs)) {

            DBGF("%s: satisfied by dbX\n", capreq_snprintf_s(req));
            //dbpkg_set_dump(ictx->uninst_set);
            //db_deps_add(ictx->db_deps, true_req, pkg, tomark,
            //            PROCESS_AS_NEW | DBDEP_DBSATISFIED);
            
        } else if (tomark || tomark == NULL) { /* don't care found or not */
            ntoinstall++;
        }
    }
    
    DBGF("end %s -> %d\n", pkg_id(pkg), ntoinstall);
    return ntoinstall;
}


static
int do_select_best_pkg(struct install_ctx *ictx, const struct pkg *marker,
                       struct pkg **candidates, int npkgs)
{
    int *ncnfls, i, j, i_best, cnfl_min;
    int i_ver_eq = -1, i_evr_eq = -1;

    DBGF("marker=%s (ncandiates%d)\n", marker ? pkg_id(marker) : "(nil)", npkgs);
    n_assert(npkgs > 0);
    
    if (npkgs == 1)
        return 0;

    ncnfls = alloca(npkgs * sizeof(*ncnfls));
    for (i=0; i < npkgs; i++)
        ncnfls[i] = 0;

    for (i=0; i < npkgs; i++) {
        struct pkg *pkg = candidates[i];
        
        DBGF("%d. %s %s (color white %d, marked %d, %p)\n", i, 
             marker ? pkg_id(marker) : "(nil)", pkg_id(pkg),
             -1, //pkg_is_color(pkg, PKG_COLOR_WHITE), 
             pkg_is_marked(ictx->ts->pms, pkg), pkg);

        if (marker && pkg_eq_name_prefix(marker, pkg)) {
            if (i_evr_eq == -1 && pkg_cmp_evr(marker, pkg) == 0)
                i_evr_eq = i;
            
            if (i_ver_eq == -1 && pkg_cmp_ver(marker, pkg) == 0)
                i_ver_eq = i;
        }
        
        if (pkg->cnflpkgs != NULL)
            for (j = 0; j < n_array_size(pkg->cnflpkgs); j++) {
                struct reqpkg *cpkg = n_array_nth(pkg->cnflpkgs, j);
                if (pkg_is_marked(ictx->ts->pms, cpkg->pkg))
                    ncnfls[i]++;
            }
    }

    if (i_evr_eq > -1 && ncnfls[i_evr_eq] == 0)
        return i_evr_eq;

    if (i_ver_eq > -1 && ncnfls[i_ver_eq] == 0)
        return i_ver_eq;

    cnfl_min = INT_MAX;
    i_best = -1;
    for (i=0; i < npkgs; i++) {
        DBGF("%d. %s %d\n", i, pkg_id(candidates[i]), ncnfls[i]);
        if (cnfl_min > ncnfls[i]) {
            cnfl_min = ncnfls[i];
            i_best = i;
        }
    }
    
    DBGF("[after cnfls] i_best = %d\n", i_best);
    if (cnfl_min == 0) {
        int n = INT_MAX, *nmarks;
        
        nmarks = alloca(npkgs * sizeof(*nmarks));
        
        for (i=0; i < npkgs; i++) {
            if (in_is_other_version_marked(ictx, candidates[i], NULL)) {
                DBGF("%d. %s other version is already marked, skipped\n",
                     i, pkg_id(candidates[i]));
                continue;
            }

            nmarks[i] = in_pkg_drags(ictx, candidates[i]);
            DBGF("%d. %s -> %d\n", i, pkg_id(candidates[i]), nmarks[i]);
            if (n > nmarks[i])
                n = nmarks[i];

            if (n == 0 && ncnfls[i] == 0) {
                i_best = i;
                break;
            }
        }
    }

    if (i_best == -1) 
        i_best = 0;
    DBGF("RET %d. %s\n", i_best, pkg_id(candidates[i_best]));
    return i_best;
}

int in_select_best_pkg(struct install_ctx *ictx, const struct pkg *marker,
                       tn_array *candidates)
{
    struct pkg **candidates_buf;
    int i, npkgs;

    DBGF("marker=%s, ncandiates=%d\n", pkg_id(marker), n_array_size(candidates));
    npkgs = n_array_size(candidates);
    candidates_buf = alloca(sizeof(*candidates_buf) * (npkgs + 1));
    
    for (i=0; i < n_array_size(candidates); i++) {
        candidates_buf[i] = n_array_nth(candidates, i);
        DBGF("cand[%d of %d] %p %s\n", i, n_array_size(candidates),
               candidates_buf[i], pkg_id(candidates_buf[i]));
    }

    candidates_buf[i] = NULL;
    return do_select_best_pkg(ictx, marker, candidates_buf, npkgs);
}


struct pkg *in_select_pkg(struct install_ctx *ictx, const char *name,
                          tn_array *pkgs)
{
    struct pkg tmpkg, *curr_pkg, *pkg, *selected_pkg;
    char prefix1[128], prefix2[128], *p;
    int i;
    
    tmpkg.name = (char*)name;

    n_array_sort(pkgs);
    i = n_array_bsearch_idx_ex(pkgs, &tmpkg, (tn_fn_cmp)pkg_cmp_name);
    DBGF("%s -> %d\n", name, i);
    if (i < 0)
        return NULL;

    selected_pkg = NULL;
    pkg = n_array_nth(pkgs, i);
    curr_pkg = n_array_nth(ictx->pkg_stack, n_array_size(ictx->pkg_stack) - 1);
    

    snprintf(prefix1, sizeof(prefix1), "%s", name);
    if ((p = strchr(prefix1, '-')))
        *p = '\0';

    snprintf(prefix2, sizeof(prefix2), "%s", curr_pkg->name);
    if ((p = strchr(prefix2, '-')))
        *p = '\0';

    DBGF("current pkg %s, name = %s, p1, p2 = %s, %s\n", pkg_id(curr_pkg), name,
           prefix1, prefix2);
    
    if (strcmp(prefix1, prefix2) != 0)
        return pkg;
    
    for (; i < n_array_size(pkgs); i++) {
        struct pkg *p = n_array_nth(pkgs, i);
        if (strcmp(p->name, name) != 0)
            break;
        
        if (pkg_cmp_evr(p, curr_pkg) == 0) {
            if (selected_pkg && pkg_cmp_evr(selected_pkg, curr_pkg) > 0)
                selected_pkg = NULL;
            
            if (selected_pkg == NULL)
                selected_pkg = p;
            DBGF("%s [yes (higher ver)]\n", pkg_id(selected_pkg));
            break;
            
        } else if (selected_pkg == NULL && pkg_cmp_ver(p, curr_pkg) == 0) {
            selected_pkg = p;
            DBGF("%s [maybe (evr are eq)]\n", pkg_id(selected_pkg));
            
        } else {
            DBGF("%s [no (lower ver)]\n", pkg_id(p));
        }
    }

    if (selected_pkg == NULL)
        selected_pkg = pkg;
    
    return selected_pkg;
}

/* any of pkgs is marked? */
static inline int one_is_marked(struct pkgmark_set *pms, struct pkg *pkgs[],
                                int npkgs)
{
    int i;

    for (i=0; i < npkgs; i++) 
        if (pkg_is_marked(pms, pkgs[i])) 
            return 1;
    
    return 0;
}


int in_find_req(struct install_ctx *ictx,
                  const struct pkg *pkg, struct capreq *req,
                  struct pkg **best_pkg, struct pkg ***candidates, int dobest)
{
    struct pkg **suspkgs = NULL;
    int nsuspkgs = 0, found = 0;

    
    *best_pkg = NULL;
    if (candidates)
        *candidates = NULL;

    found = psreq_find_match_packages(ictx->ps, pkg, req, &suspkgs, &nsuspkgs,
                                      ictx->strict);
    if (found && nsuspkgs) {
        struct pkg **tmpkgs;
        int n, i;
        
        /* remove marked for removal items from suspected packages */
        tmpkgs = alloca(sizeof(*tmpkgs) * nsuspkgs);
        n = 0;
        for (i=0; i < nsuspkgs; i++) {
            if (in_is_marked_for_removal(ictx, suspkgs[i])) {
                DBGF("%s: removed marked for removal\n", pkg_id(suspkgs[i]));
                continue;
            }
            tmpkgs[n++] = suspkgs[i];
        }

        memcpy(suspkgs, tmpkgs, n *  sizeof(*tmpkgs));
        nsuspkgs = n;
        
        if (nsuspkgs == 0) {
            found = 0;
            goto l_end;
        }

        /* already not marked for upgrade */
        if (nsuspkgs > 0 && !one_is_marked(ictx->ts->pms, suspkgs, nsuspkgs)) {
            int best_i = 0;
            
            if (dobest && nsuspkgs > 1)
                best_i = do_select_best_pkg(ictx, pkg, suspkgs, nsuspkgs);
            
            *best_pkg = suspkgs[best_i];

            if (in_is_other_version_marked(ictx, *best_pkg, NULL)) {
                found = 0;
                *best_pkg = NULL;
            }
        }
    }
    
 l_end:
    
    if (candidates && nsuspkgs) {
        n_assert(suspkgs);
        *candidates = suspkgs;
        
    } else if (suspkgs)
        free(suspkgs);
    
    return found;
}

int in_pkgdb_match_req(struct install_ctx *ictx, struct capreq *req) 
{
    if (db_deps_provides(ictx->db_deps, req, DBDEP_DBSATISFIED)) {
        DBGF("%s: satisfied by db [cached]\n", capreq_snprintf_s(req));
        return 1;
    }
        
    return pkgdb_match_req(ictx->ts->db, req, ictx->strict,
                           ictx->uninst_set->dbpkgs);
}

