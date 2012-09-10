/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "ictx.h"

int i3_is_pkg_installed(struct poldek_ts *ts, struct pkg *pkg, int *cmprc)
{
    tn_array *dbpkgs = NULL;
    int n = 0, freshen = 0;
    freshen = ts->getop(ts, POLDEK_OP_FRESHEN)
	    || poldek_ts_issetf(ts, POLDEK_TS_UPGRADE)
	    || poldek_ts_issetf(ts, POLDEK_TS_DOWNGRADE)
	    || poldek_ts_issetf(ts, POLDEK_TS_UPGRADEDIST);

    n = pkgdb_search(ts->db, &dbpkgs, PMTAG_NAME, pkg->name, NULL, PKG_LDNEVR);
    n_assert(n >= 0);
    
    if (n == 0) {
        n_assert(dbpkgs == NULL);
        return 0;
    }
    
    if (poldek_conf_MULTILIB) { /* filter out different architectures */
        tn_array *arr = n_array_clone(dbpkgs);

        //DBGF("pkg = %s\n", pkg_id(pkg));
        //pkgs_array_dump(dbpkgs, "before_multilib");
        unsigned int i;
        for (i=0; i < n_array_size(dbpkgs); i++) {
            struct pkg *dbpkg = n_array_nth(dbpkgs, i);

	    msgn(4, "from pkg %s.%s => to pkg %s-%s-%s.%s freshen:%d kind:%d up_arch:%d",
	    pkg_snprintf_s(dbpkg), pkg_arch(dbpkg), pkg->name, pkg->ver, pkg->rel, pkg_arch(pkg),
	    freshen, pkg_is_kind_of(dbpkg, pkg), pkg_is_arch_compat(dbpkg, pkg));

	    // if freshen (upgrade) preffer same arch but
	    // change from/to noarch depends on which pkg is noarch
	    // add package if pkg_is_kind_of (have same name and color)
            if (pkg_is_kind_of(dbpkg, pkg)
		&& !(freshen && !pkg_is_arch_compat(dbpkg, pkg)))
			n_array_push(arr, pkg_link(dbpkg));
        }

        n_array_cfree(&dbpkgs);
        dbpkgs = arr;
        n = n_array_size(arr);
        //pkgs_array_dump(dbpkgs, "after_multilib");
    }

    if (n) {
        struct pkg *dbpkg = n_array_nth(dbpkgs, 0);
        *cmprc = pkg_cmp_evr(pkg, dbpkg);
    }

    n_array_free(dbpkgs);
    return n;
}

/* RET: 0 - not installable,  1 - installable,  -1 - something wrong */
int i3_is_pkg_installable(struct poldek_ts *ts, struct pkg *pkg,
                           int is_hand_marked)
{
    int cmprc = 0, npkgs, installable = 1, freshen = 0, force;
    
    freshen = ts->getop(ts, POLDEK_OP_FRESHEN);
    force = ts->getop(ts, POLDEK_OP_FORCE);
    npkgs = i3_is_pkg_installed(ts, pkg, &cmprc);
    
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
        /* upgrade flag is set for downgrade and reinstall too */
        if (cmprc <= 0 && force == 0 &&
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
        
        if (installable > 0 && poldek_ts_issetf(ts, POLDEK_TS_UPGRADE) && 
            ts->getop(ts, POLDEK_OP_HOLD) && pkg_is_scored(pkg, PKG_HELD)) {
            logn(LOGERR, _("%s: refusing to upgrade held package"),
                 pkg_id(pkg));
            installable = 0;
        }
    }

    return installable;
}

/* i.e score how many marker's requirements are satisfied by pkg */
static
int satisfiability_score(const struct pkg *marker, const struct pkg *pkg)
{
    struct pkg_req_iter  *it = NULL;
    const struct capreq  *req = NULL;
    unsigned itflags = PKG_ITER_REQIN | PKG_ITER_REQDIR | PKG_ITER_REQSUG;
    int nyes = 0, nno = 0;
    
    
    n_assert(marker->reqs);

    it = pkg_req_iter_new(marker, itflags);
    while ((req = pkg_req_iter_get(it))) {
        if (pkg_satisfies_req(pkg, req, 1))
            nyes++;
        else
            nno++;
    }
    pkg_req_iter_free(it);

    if (nyes > 2 && nno == 0)               /* all requirements satisfied */
        return 3;

    if (nyes > 1)
        return 2;

    return 0;
}

/* scores must be the same size of n_array_size(pkgs) */
static void add_arch_scores(int *scores, const tn_array *pkgs)
{
    int i, min_score = INT_MAX;
    int *arch_scores;

    arch_scores = alloca(n_array_size(pkgs) * sizeof(*arch_scores));
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        
        arch_scores[i] = pkg_arch_score(pkg);
        if (min_score > arch_scores[i])
            min_score = arch_scores[i];
    }
    
    for (i=0; i < n_array_size(pkgs); i++) {
        if (arch_scores[i] == min_score)
            scores[i] += 1;      /* 1 point for best fit */
        DBGF("%s %d\n", pkg_id(n_array_nth(pkgs, i)), arch_scores[i]);
    }
}

    

static int do_select_best_pkg(int indent, struct i3ctx *ictx,
                              const struct pkg *marker, tn_array *candidates)
{
    int *conflicts, min_nconflicts, j, i_best, *scores;
    int i, max_score, npkgs, same_packages_different_arch = 0;

    npkgs = n_array_size(candidates);
    tracef(indent, "marker is %s, ncandidates=%d",
           marker ? pkg_id(marker) : "(null)", npkgs);
    
    n_assert(npkgs > 0);
    if (npkgs == 1)
        return 0;
    
    scores = alloca(npkgs * sizeof(*scores));
    conflicts = alloca(npkgs * sizeof(*conflicts));
    min_nconflicts = 0;


    i_best = -1;    
    for (i=0; i < n_array_size(candidates); i++) {
        struct pkg *pkg = n_array_nth(candidates, i);
        int cmprc = 0;

        conflicts[i] = 0;
        scores[i] = 0;
        
        trace(indent, "- %d. %s (marked=%d)", i, pkg_id(pkg),
              i3_is_marked(ictx, pkg));

        //not needed any more?
        if (pkg_isset_mf(ictx->processed, pkg, PKGMARK_BLACK))
            scores[i] = -999;

        trace(indent, "- %d. %s (pts=%d), marker: %d, multilib %d", i, pkg_id(pkg),scores[i], marker ? -1 : 0, poldek_conf_MULTILIB ? -1:0);

        /* same prefix  */
        if (marker && pkg_eq_name_prefix(marker, pkg)) {
            scores[i] += 1;
            trace(indent, "- %d. %s (pts=%d)", i, pkg_id(pkg),scores[i]);
            if (pkg_cmp_evr(marker, pkg) == 0) /* same prefix && evr */
                scores[i] += 2;
            
            else if (pkg_cmp_ver(marker, pkg) == 0) /* same prefix && ver */
                scores[i] += 1;
            trace(indent, "- %d. %s (pts=%d)", i, pkg_id(pkg),scores[i]);
        }

        /* same color or arch */
        if (marker && poldek_conf_MULTILIB) {
            if (pkg_is_colored_like(pkg, marker))
                scores[i] += 2;
            else if (pkg_cmp_arch(pkg, marker) == 0)
                scores[i] += 1;

            // extra 100 points for arch compatible
            if (/*pkg_is_kind_of(pkg, marker) && */pkg_is_arch_compat(pkg, marker))
                        scores[i] += 100;
            trace(indent, "- %d. %s (pts=%d)", i, pkg_id(pkg),scores[i]);
        }

        //DBGF_F("xxx %s %d %d\n", pkg_id(pkg), pkg_arch_score(pkg), arch_scores[i]);
        scores[i] += satisfiability_score(marker, pkg);
        trace(indent, "- %d. %s (pts=%d)", i, pkg_id(pkg),scores[i]);
        
        if (i > 0) {
            struct pkg *prev = n_array_nth(candidates, i - 1);
            if (pkg_cmp_name_evr(pkg, prev) == 0)
                same_packages_different_arch++;
            else
                same_packages_different_arch--;
            
            trace(indent, "cmp %s %s -> %d, %d", pkg_id(pkg), pkg_id(prev),
                 pkg_cmp_name_evr(pkg, prev), same_packages_different_arch);
        }
        
        if (i3_is_pkg_installed(ictx->ts, pkg, &cmprc) && cmprc > 0) {
            if (!iset_has_kind_of_pkg(ictx->unset, pkg))
                scores[i] += 5; /* already installed and upgradeable - sweet */
        }

        trace(indent, "  %d %d %d\n", i, scores[i], conflicts[i]);
        if (pkg->cnflpkgs != NULL)
            for (j = 0; j < n_array_size(pkg->cnflpkgs); j++) {
                struct reqpkg *cpkg = n_array_nth(pkg->cnflpkgs, j);
                if (i3_is_marked(ictx, cpkg->pkg)) {
                    conflicts[i] += 1;
                    scores[i] -= 5;
                    trace(indent, "conflicts:  %d %d %d %d\n", i, scores[i], conflicts[i], pkg_id(cpkg->pkg));
                }
            }
        
        
        if (min_nconflicts == -1)
            min_nconflicts = conflicts[i];
        else if (conflicts[i] < min_nconflicts)
            min_nconflicts = conflicts[i];
            
        if (i3_is_other_version_marked(ictx, pkg, NULL)) {
            trace(indent + 4, "%s: other version is already marked, skipped",
                  pkg_id(pkg));
            scores[i] -= 10;
        }
        trace(indent, "%d %d %d\n", i, scores[i], conflicts[i]);
    }

    /* marker noarch -> suggests architecture not */
    if (poldek_conf_MULTILIB && marker && n_str_eq(pkg_arch(marker), "noarch"))
        add_arch_scores(scores, candidates);
    
    max_score = scores[0];
    i_best = 0;

    for (i=0; i < n_array_size(candidates); i++) {
        struct pkg *pkg = n_array_nth(candidates, i);
        trace(indent, "- %d. %s -> score %d, conflicts %d", i, pkg_id(pkg),
              scores[i], conflicts[i]);

        if (scores[i] > max_score) {
            max_score = scores[i];
            i_best = i;
        }
    }
    
    if (same_packages_different_arch == npkgs - 1) /* choose now then */
        goto l_end;
    
    if (i_best == -1) 
        i_best = 0;
    
l_end:
    trace(indent, "RET %d. %s\n", i_best,
          pkg_id(n_array_nth(candidates, i_best)));
    return i_best;
}

int i3_select_best_pkg(int indent, struct i3ctx *ictx,
                        const struct pkg *marker, tn_array *candidates)
{
    tn_array *tmp = NULL;
    int i;

    DBGF("marker=%s, ncandiates=%d\n", pkg_id(marker), n_array_size(candidates));

    for (i=0; i < n_array_size(candidates); i++) {
        struct pkg *cand = n_array_nth(candidates, i);

        if (!pkg_isset_mf(ictx->processed, cand, PKGMARK_BLACK) && pkg_is_colored_like(cand, marker)) {
            if (tmp == NULL)
                tmp = n_array_clone(candidates);
            
            n_array_push(tmp, pkg_link(cand));
            DBGF("cand[%d of %d] %s\n", n_array_size(tmp),
                 n_array_size(candidates), pkg_id(cand));
        }
    }
    
    if (tmp == NULL)                 /* no packages */
        return -1;
    
    i = do_select_best_pkg(indent, ictx, marker, tmp);
    n_array_cfree(&tmp);
    return i;
}


static inline int any_is_marked(struct i3ctx *ictx, tn_array *pkgs)
{
    int i;
    
    for (i=0; i < n_array_size(pkgs); i++) 
        if (i3_is_marked(ictx, n_array_nth(pkgs, i)))
            return 1;
            
    return 0;
}

int i3_find_req(int indent, struct i3ctx *ictx,
                const struct pkg *pkg, const struct capreq *req,
                struct pkg **best_pkg, tn_array *candidates)
{
    tn_array *suspkgs = NULL, *tmpkgs;
    int found = 0, i;

    *best_pkg = NULL;
    found = pkgset_find_match_packages(ictx->ps, pkg, req, &suspkgs, 1);//ictx->strict);
    
    //trace(indent, "PROMOTE pkg test satisfied %d", pkg_satisfies_req(pkg,req,1));

    if (!found)
        return 0;

    if (suspkgs == NULL) 
        goto l_end;

    n_assert(n_array_size(suspkgs) > 0);
    
    /* remove marked for removal items from suspected packages, but do it only
       when POLDEK_TS_REINSTALL is not set, otherwise removing and installing
       the same package is possible */
    if (!poldek_ts_issetf(ictx->ts, POLDEK_TS_REINSTALL)) {
        tmpkgs = n_array_clone(suspkgs);
        while (n_array_size(suspkgs)) {
            struct pkg *suspkg = n_array_shift(suspkgs);

            /* possible when the same package exists in both available
               and already installed set */
            if (i3_is_marked_for_removal(ictx, suspkg) && !i3_is_marked(ictx, suspkg)) {
                pkg_free(suspkg);
                continue;
            }
        
            n_array_push(tmpkgs, suspkg);
        }
    
        n_array_free(suspkgs);
        suspkgs = tmpkgs;
    }

    //trace(indent, "after removed rmmarked -> %d package(s)",
    //      n_array_size(suspkgs));

    /* remove marked as BLACK from suspected packages, they have broken deps */
    for (i = 0; i < n_array_size(suspkgs); i++) {
	struct pkg *suspkg = n_array_nth(suspkgs, i);
	
	if (pkg_isset_mf(ictx->processed, suspkg, PKGMARK_BLACK)) {
	    trace(indent, "- marked as BLACK %s", pkg_id(suspkg));
	    n_array_remove_nth(suspkgs, i--);
	}
    }
        
    if (n_array_size(suspkgs) == 0) {
        found = 0;
        goto l_end;
    }

    /* return found and *best_pkg=NULL if any package is already marked */
    if (!any_is_marked(ictx, suspkgs)) {
        int best_i;
        best_i = do_select_best_pkg(indent, ictx, pkg, suspkgs);
            
        *best_pkg = n_array_nth(suspkgs, best_i);
        if (i3_is_other_version_marked(ictx, *best_pkg, NULL)) {
            found = 0;
            *best_pkg = NULL;
        }
    }
    
l_end:
    
    if (candidates && suspkgs && n_array_size(suspkgs) > 1) {
        while (n_array_size(suspkgs)) {
            struct pkg *tmp_pkg = n_array_shift(suspkgs);
            n_array_push(candidates, tmp_pkg);
        }
    }

    n_array_cfree(&suspkgs);

    if (capreq_is_rpmlib(req))
        return found;             /* do not trace rpmlib() caps */
    
    if (candidates == NULL) {
        tracef(indent, "%s %s (best=%s)", capreq_stra(req),
               found ? "found" : "not found", 
               *best_pkg ? pkg_id(*best_pkg): "none");
    } else {
        tracef(indent, "%s %s (%d candidate(s), best=%s)", capreq_stra(req),
               found ? "found" : "not found",
               n_array_size(candidates),
               *best_pkg ? pkg_id(*best_pkg): "none");
    }
    
    return found;
}

int i3_pkgdb_match_req(struct i3ctx *ictx, const struct capreq *req) 
{
    /* missing epoch in db package is not a problem, usually */
    unsigned ma_flags = ictx->ma_flags | POLDEK_MA_PROMOTE_CAPEPOCH; 
    
    return pkgdb_match_req(ictx->ts->db, req, ma_flags, 
                           iset_packages_by_recno(ictx->unset));
}

struct pkg *i3_choose_equiv(struct poldek_ts *ts,
                            const struct pkg *pkg, const struct capreq *cap,
                            tn_array *pkgs, struct pkg *hint) 
{
    tn_array *tmp = NULL;
    int n;
    
    n_assert(pkgs);
    n_assert(n_array_size(pkgs) > 0);

    if (hint == NULL)
        hint = n_array_nth(pkgs, 0);
    
    if (!ts->getop(ts, POLDEK_OP_EQPKG_ASKUSER))
        return hint;

    tmp = n_array_dup(pkgs, (tn_fn_dup)pkg_link);
    n_array_ctl_set_cmpfn(tmp, (tn_fn_cmp)pkg_cmp_name_evr_rev);
    n_array_sort(tmp);

    if (ts->getop(ts, POLDEK_OP_MULTILIB)) {
        int size = n_array_size(tmp);

        //pkgs_array_dump(pkgs, "BEFORE");
        n_array_uniq_ex(tmp, (tn_fn_cmp)pkg_cmp_name_evr);
        
        /* ops, same packages, different arch -> no choice */
        if (n_array_size(tmp) != size) {
            n_array_free(tmp);
            //DBGF("no choice ");
            //pkgs_array_dump(pkgs, "AFTER");
            return hint;
        }
    }

    n = poldek__choose_equiv(ts, pkg, capreq_stra(cap), tmp, hint);
    if (n == -1) 
        hint = NULL;
    else 
        hint = n_array_nth(tmp, n);
    
    n_array_free(tmp);
    return hint;
}

int i3_is_user_choosable_equiv(struct poldek_ts *ts) 
{
    return ts->getop(ts, POLDEK_OP_EQPKG_ASKUSER);
}

