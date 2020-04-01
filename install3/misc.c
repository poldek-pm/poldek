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

#include "ictx.h"

int i3_is_pkg_installed(struct poldek_ts *ts, const struct pkg *pkg, int *cmprc)
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
        for (int i=0; i < n_array_size(dbpkgs); i++) {
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
        if (n > 1) {
            /*
               order by E-V-R, DESC to compare with newest installed version
            */
            n_array_isort_ex(dbpkgs, (tn_fn_cmp)pkg_cmp_evr);
            n_array_reverse(dbpkgs);
        }

        struct pkg *dbpkg = n_array_nth(dbpkgs, 0);
        *cmprc = pkg_cmp_evr(pkg, dbpkg);
    }

    n_array_free(dbpkgs);
    return n;
}

/* RET: 0 - not installable,  1 - installable,  -1 - something wrong */
int i3_is_pkg_installable(struct poldek_ts *ts, const struct pkg *pkg,
                          int is_hand_marked)
{
    int cmprc = 0, npkgs, installable = 1, freshen = 0, force;

    freshen = ts->getop(ts, POLDEK_OP_FRESHEN);
    force = ts->getop(ts, POLDEK_OP_FORCE);
    npkgs = i3_is_pkg_installed(ts, pkg, &cmprc);

    /* if (npkgs > 1 && is_multiple)
           *is_multiple = 1;  */

    n_assert(npkgs >= 0);

    installable = 1;

    if (npkgs == 0) {
        if (is_hand_marked && freshen)
            installable = 0;

    } else if (cmprc != 0 && is_hand_marked && npkgs > 1 &&
               poldek_ts_issetf(ts, POLDEK_TS_UPGRADE) && force == 0) {
        if (ts->getop(ts, POLDEK_OP_MULTIINST)) {
            installable = 1;
        } else {
            logn(LOGERR, _("%s: multiple instances installed, give up"), pkg->name);
            installable = -1;
        }
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

static
bool req_satisfiable(int indent, struct i3ctx *ictx,
                     const struct pkg *pkg, const struct capreq *req) {

    if (pkgset_find_match_packages(ictx->ps, pkg, req, NULL, 1)) {
        tracef(indent, "%s %s => iset", pkg_id(pkg), capreq_stra(req));
        return true;
    }

    if (i3_pkgdb_match_req(ictx, req)) {
        tracef(indent, "%s %s => dbset", pkg_id(pkg), capreq_stra(req));
        return true;
    }

    tracef(indent, "%s %s => none", pkg_id(pkg), capreq_stra(req));
    return false;
}

/* i.e score how many marker's requirements are satisfied by pkg */
static
int satisfiability_score(int indent, struct i3ctx *ictx,
                         const struct pkg *marker, const struct pkg *pkg)
{
    struct pkg_req_iter  *it = NULL;
    const struct capreq  *req = NULL;
    unsigned itflags = PKG_ITER_REQIN | PKG_ITER_REQDIR | PKG_ITER_REQSUG;
    int nyes = 0, nno = 0, nunmet = 0;

    n_assert(marker->reqs);

    it = pkg_req_iter_new(marker, itflags);
    while ((req = pkg_req_iter_get(it))) {
        if (pkg_satisfies_req(pkg, req, 1)) {
            nyes++;
        } else {
            nno++;
        }
    }
    pkg_req_iter_free(it);

    it = pkg_req_iter_new(pkg, itflags);
    while ((req = pkg_req_iter_get(it))) {
        if (!req_satisfiable(indent, ictx, pkg, req))
            nunmet++;
    }
    pkg_req_iter_free(it);

    if (nyes > 2 && nno == 0)               /* all requirements satisfied */
        return 3;

    if (nunmet > 0)
        return -1;

    if (nyes > 1)
        return 2;

    return 0;
}

struct candidate_score {
    int score;                  /* overall score */
    int satscore;               /* satisfiability score */
    int conflicts;              /* no of conflicts */
    int prefix_evr;             /* similarity score (name and/or EVR */
    int color;                  /* "color" score (multilib) */
    int arch;                   /* raw pm_arch_score() result */
    bool upgrade;               /* is candidate being upgraded */
    bool oth;                   /* are oth instances of pkg being installed */
};

static void score_candidate(int indent, struct i3ctx *ictx,
                           const struct pkg *marker, const struct pkg *pkg,
                           struct candidate_score *sc)
{
    memset(sc, 0, sizeof(*sc));

    tracef(indent, "%s, marker=%s", pkg_id(pkg), marker ? pkg_id(marker) : "(null)");
    indent += 1;

    if (pkg_isset_mf(ictx->processed, pkg, PKGMARK_BLACK)) {
        trace(indent, "%s score=-999", pkg_id(pkg));
        sc->score = -999;
        return;
    }

    /* same prefix  */
    if (marker && pkg_eq_name_prefix(marker, pkg)) {
        sc->prefix_evr += 1;
        if (pkg_cmp_evr(marker, pkg) == 0) /* same prefix && evr */
            sc->prefix_evr += 2;
        else if (pkg_cmp_ver(marker, pkg) == 0) /* same prefix && ver */
            sc->prefix_evr += 1;

        trace(indent, "- %s (prefix_evr=%d)", pkg_id(pkg), sc->prefix_evr);
    }

    /* same color or arch */
    if (marker && poldek_conf_MULTILIB) {
        if (pkg_is_colored_like(pkg, marker))
            sc->color += 2;
        else if (pkg_cmp_arch(pkg, marker) == 0)
            sc->color += 1;

        // extra 100 points for arch compatible
        if (/*pkg_is_kind_of(pkg, marker) && */pkg_is_arch_compat(pkg, marker))
            sc->color += 100;

        trace(indent, "- %s (color=%d)", pkg_id(pkg), sc->color);

        /* noarch score is 0, take pm's arch score */
        if (n_str_eq(pkg_arch(marker), "noarch")) {
            sc->arch = pkg_arch_score(pkg);
        }
        trace(indent, "- %s (arch=%d)", pkg_id(pkg), sc->arch);
    }

    if (marker) {
        sc->satscore = satisfiability_score(indent, ictx, marker, pkg);
        trace(indent, "- %s (satscore=%d)", pkg_id(pkg), sc->satscore);
    }

    int cmprc = 0;
    if (i3_is_pkg_installed(ictx->ts, pkg, &cmprc) && cmprc > 0) {
        if (!iset_has_kind_of_pkg(ictx->unset, pkg)) {
            sc->upgrade = true; /* already installed and upgradeable - sweet */
        }
    }

    trace(indent, "- %s (upgrade=%d)", pkg_id(pkg), sc->upgrade);

    if (pkg->cnflpkgs != NULL) {
        for (int j = 0; j < n_array_size(pkg->cnflpkgs); j++) {
            struct reqpkg *cpkg = n_array_nth(pkg->cnflpkgs, j);
            if (i3_is_marked(ictx, cpkg->pkg)) {
                sc->conflicts++;
            }
        }

        if (sc->conflicts > 0)
            trace(indent, "- %s (conflicts=%d)", pkg_id(pkg), sc->conflicts);
    }

    if (i3_is_other_version_marked(ictx, pkg, NULL)) {
        trace(indent + 4, "%s: other version is already marked, skipped", pkg_id(pkg));
        sc->oth = true;
    }

    sc->score = sc->satscore - (5 * sc->conflicts) + sc->prefix_evr + sc->color +
               (sc->upgrade ? 5 : 0) + (sc->oth ? -10 : 0);

    trace(indent, "=> %s's score=%d", pkg_id(pkg), sc->score);
}


static int do_select_best_pkg(int indent, struct i3ctx *ictx,
                              const struct pkg *marker, tn_array *candidates)
{
    int npkgs, nsame = 1, min_arch_score = INT_MAX;
    struct candidate_score *scores;

    npkgs = n_array_size(candidates);
    tracef(indent, "marker is %s, ncandidates=%d",
           marker ? pkg_id(marker) : "(null)", npkgs);

    n_assert(npkgs > 0);
    if (npkgs == 1)
        return 0;

    scores = alloca(npkgs * sizeof(*scores));
    memset(scores, 0, npkgs * sizeof(*scores));

    for (int i=0; i < n_array_size(candidates); i++) {
        struct pkg *pkg = n_array_nth(candidates, i);
        struct candidate_score *sc = &scores[i];

        score_candidate(indent+2, ictx, marker, pkg, sc);
        trace(indent, "- %d. %s (marked=%d, satscore=%d, score=%d)", i, pkg_id(pkg),
              i3_is_marked(ictx, pkg), sc->satscore, sc->score);

        if (min_arch_score > sc->arch)
            min_arch_score = sc->arch;

        if (i > 0) {
            struct pkg *prev = n_array_nth(candidates, i - 1);
            if (pkg_cmp_name(pkg, prev) == 0 && pkg_cmp_same_arch(pkg, prev)) {
                nsame++;
                trace(indent, "- same %s %s", pkg_id(pkg), pkg_id(prev));
            }
        }
    }

    int best_score = scores[0].score;
    int best_satscore = scores[0].satscore, worst_satscore = INT_MAX;
    int best_conflicts = INT_MAX, worst_conflicts = 0;
    int i_best = 0, i_best_sat = 0;

    /* need second loop to apply min_arch_score bonus */
    for (int i=0; i < npkgs; i++) {
        struct candidate_score *sc = &scores[i];

        /* bump best architecture */
        if (sc->arch == min_arch_score)
            sc->score += 1;

        if (sc->score > best_score) {
            best_score = sc->score;
            i_best = i;
        }

        if (sc->satscore > best_satscore) {
            best_satscore = sc->satscore;
            i_best_sat = i;
        }

        if (sc->satscore < worst_satscore)
            worst_satscore = sc->satscore;

        if (sc->conflicts < best_conflicts)
            best_conflicts = sc->conflicts;

        if (sc->conflicts > worst_conflicts)
            worst_conflicts = sc->conflicts;
    }

    trace(indent, "satscore: best=%d, worst=%d", best_satscore, worst_satscore);
    trace(indent, "conflicts: best=%d, worst=%d", best_conflicts, worst_conflicts);
    trace(indent, "best: i=%d, score=%d, sat=%d", i_best, best_score, scores[i_best].satscore);
    trace(indent+1, "bestsat: i=%d, sat=%d",i_best_sat, best_satscore);

    struct pkg *best = n_array_nth(candidates, i_best);

    /* all are the same package with different version, so do not even ask user about */
    if (nsame == npkgs && (pkg_cmp_ver(best, marker) == 0 || i_best == i_best_sat)) {
        trace(indent, "- candidates are the same, just take best fit %s", pkg_id(best));
        best = pkg_link(best);
        n_array_clean(candidates);
        n_array_push(candidates, best);
        i_best = 0;
    } else {
        bool trim_satscore = false;
        bool trim_conflicts = false;

        /* are there are packages with reqs that cannot be met?  */
        if (worst_satscore < 0 && best_satscore >= 0)
            trim_satscore = true;

        if (worst_conflicts > 0 && best_conflicts == 0)
            trim_conflicts = true;

        if (trim_satscore || trim_conflicts) {
            tn_array *copy = n_array_clone(candidates);
            i_best = -1;
            for (int i = 0; i < npkgs; i++) {
                struct candidate_score *sc = &scores[i];
                if (trim_satscore && sc->satscore < 0) {
                    trace(indent, "removed %s with negative satscore",
                          pkg_id(n_array_nth(candidates, i)));
                } else if (trim_conflicts && sc->conflicts > 0) {
                    trace(indent, "removed %s with conflicts",
                          pkg_id(n_array_nth(candidates, i)));
                } else {
                    struct pkg *pkg = n_array_nth(candidates, i);
                    if (pkg == best)
                        i_best = n_array_size(copy);
                    n_array_push(copy, pkg_link(pkg));
                }
            }
            n_assert(i_best >= 0);
            n_array_clean(candidates);
            n_array_concat_ex(candidates, copy, (tn_fn_dup)pkg_link);
            n_array_free(copy);
        }
    }

    struct pkg *xbest = n_array_nth(candidates, i_best);
    n_assert(xbest == best);

    n_assert(i_best >= 0);
    n_assert(i_best < n_array_size(candidates));

    trace(indent, "RET %d. %s, candidates=%d\n", i_best, pkg_id(best),
          n_array_size(candidates));

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

        if (!pkg_isset_mf(ictx->processed, cand, PKGMARK_BLACK) &&
            pkg_is_colored_like(cand, marker)) {
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

    /* tests automation */
    if (poldek__is_in_testing_mode()) {
        for (int i=0; i < n_array_size(pkgs); i++) {
            msgn(0, "%%choose %s", pkg_id(n_array_nth(pkgs, i)));
        }
        return hint;
    }

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
