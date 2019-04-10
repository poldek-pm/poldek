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

static
int find_replacement(struct i3ctx *ictx, struct pkg *pkg,
                     const struct capreq *cnfl, struct pkg **rpkg)
{
    tn_array *pkgs;
    struct pkgset *ps;
    int i;

    ps = ictx->ts->ctx->ps;
    *rpkg = NULL;

    /* simply higest version */
    if ((pkgs = pkgset_search(ps, PS_SEARCH_NAME, pkg->name))) {
        for (i=0; i < n_array_size(pkgs); i++) {
            struct pkg *p = n_array_nth(pkgs, i);
            if (!pkg_is_kind_of(p, pkg))
                continue;

            /* still conflicted */
            if (p->cnfls && n_array_bsearch(p->cnfls, cnfl))
                continue;

            if (pkg_cmp_evr(p, pkg) > 0) {
                *rpkg = p;
                break;
            }
        }
    }

    if (*rpkg == NULL && (pkgs = pkgset_search(ps, PS_SEARCH_OBSL, pkg->name))) {
        for (i=0; i < n_array_size(pkgs); i++) {
            struct pkg *p = n_array_nth(pkgs, i);

            if (!pkg_is_kind_of(p, pkg))
                continue;

            /* still conflicted */
            if (p->cnfls && n_array_bsearch(p->cnfls, cnfl))
                continue;

            if (pkg_caps_obsoletes_pkg_caps(p, pkg) &&
                pkg_cmp_name_evr(p, pkg) > 0) { /* XXX bug propably, testit */
                *rpkg = p;
                break;
            }
        }
        n_array_free(pkgs);
    }

    if (*rpkg && i3_is_marked(ictx, *rpkg)) {
        *rpkg = NULL;
        return 1;
    }

    return (*rpkg != NULL);
}

/* trying to resolve conflict by package upgrade */
static int resolve_conflict(int indent, struct i3ctx *ictx,
                            struct pkg *pkg, const struct capreq *cnfl,
                            struct pkg *dbpkg)
{
    struct capreq *req = NULL;
    struct pkg *tomark = NULL;
    tn_array   *candidates = NULL;
    int found = 0, by_replacement = 0;

    if (!ictx->ts->getop(ictx->ts, POLDEK_OP_FOLLOW))
        return 0;

    if (!capreq_versioned(cnfl))
        return 0;

    req = capreq_clone(NULL, cnfl);

#if 0                           /* debug */
    printf("B %s -> ", capreq_stra(req));
    capreq_revrel(req);
    printf("%s -> ", capreq_stra(req));
    capreq_revrel(req);
    printf("%s\n", capreq_stra(req));
#endif

    DBGF("find_req %s %s\n", pkg_id(pkg), capreq_stra(req));
    capreq_revrel(req);
    DBGF("find_req %s %s\n", pkg_id(pkg), capreq_stra(req));

    if (i3_is_user_choosable_equiv(ictx->ts))
        candidates = pkgs_array_new(8);

    found = i3_find_req(indent, ictx, pkg, req, &tomark, candidates);
    capreq_revrel(req);

    if (!found) {
        found = find_replacement(ictx, dbpkg, cnfl, &tomark);
        by_replacement = 1;

    } else {
        struct pkg *real_tomark = tomark;

        /* tomark == NULL ? req satsfied by already installed set */
        if (tomark && candidates && n_array_size(candidates) > 1) {
            real_tomark = i3_choose_equiv(ictx->ts, pkg, req, candidates, tomark);
            n_array_cfree(&candidates);
            if (real_tomark == NULL) { /* user aborts */
                ictx->abort = 1;
                found = 0;
            }
        }
        tomark = real_tomark;
    }

    if (!found)
        goto l_end;

    if (tomark == NULL)   /* already in inset */
        goto l_end;

    found = 0;
    if (by_replacement || pkg_obsoletes_pkg(tomark, dbpkg)) {
        struct i3pkg *i3tomark;

        found = 1;
        i3tomark = i3pkg_new(tomark, 0, pkg, req, I3PKGBY_REQ);
        i3_process_package(indent, ictx, i3tomark);
    }

l_end:
    n_array_cfree(&candidates);
    capreq_free(req);
    return found;
}


/* check if cnfl conflicts with db */
static
int find_db_conflicts_cnfl_with_db(int indent, struct i3ctx *ictx,
                                   struct pkg *pkg, const struct capreq *cnfl)
{
    int i, ncnfl = 0;
    tn_hash *ht = NULL;
    tn_array *dbpkgs = NULL;

    pkgdb_search(ictx->ts->db, &dbpkgs, PMTAG_CAP, capreq_name(cnfl),
                 iset_packages_by_recno(ictx->unset), PKG_LDWHOLE_FLDEPDIRS);

    if (dbpkgs == NULL)
        return 0;

    msgn_i(4, indent, "Processing conflict %s:%s...", pkg_id(pkg),
           capreq_stra(cnfl));

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
                       capreq_stra(cnfl), pkg_id(dbpkg));
                n_hash_insert(ht, dbpkg->name, pkg);
            }
        }
    }

    for (i=0; i < n_array_size(dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(dbpkgs, i);

        msg_i(6, indent, "%d. %s (%s) <-> %s ?\n", i, pkg_id(pkg),
              capreq_stra(cnfl), pkg_id(dbpkg));

        if (ht && n_hash_exists(ht, dbpkg->name))
            continue;

        if (!pkg_is_colored_like(pkg, dbpkg))
            continue;

        if (pkg_match_req(dbpkg, cnfl, 1)) {
            if (!resolve_conflict(indent, ictx, pkg, cnfl, dbpkg)) {
                i3_error(ictx, pkg, I3ERR_DBCONFLICT,
                         _("%s (cnfl %s) conflicts with installed %s"),
                         pkg_id(pkg), capreq_stra(cnfl), pkg_id(dbpkg));

                logn(LOGERR, _("%s (cnfl %s) conflicts with installed %s"),
                     pkg_id(pkg), capreq_stra(cnfl), pkg_id(dbpkg));
                ncnfl++;
            }
        }
    }

    if (ht)
        n_hash_free(ht);

    n_array_free(dbpkgs);

    return ncnfl;
}

/* check if db conflicts with cap */
static
int find_db_conflicts_dbcnfl_with_cap(int indent, struct i3ctx *ictx,
                                      struct pkg *pkg, const struct capreq *cap)
{
    int i, j, ncnfl = 0;
    tn_array *dbpkgs = NULL;

    pkgdb_search(ictx->ts->db, &dbpkgs, PMTAG_CNFL, capreq_name(cap),
                 iset_packages_by_recno(ictx->unset), PKG_LDWHOLE_FLDEPDIRS);

    if (dbpkgs == NULL)
        return 0;

    for (i = 0; i < n_array_size(dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(dbpkgs, i);

        msg(6, "%s (%s) <-> %s ?\n", pkg_id(pkg),
            capreq_stra(cap), pkg_id(dbpkg));

        for (j = 0; j < n_array_size(dbpkg->cnfls); j++) {
            struct capreq *cnfl = n_array_nth(dbpkg->cnfls, j);
            if (cap_match_req(cap, cnfl, 1)) {
                if (resolve_conflict(indent, ictx, pkg, cnfl, dbpkg))
                    continue;

                i3_error(ictx, pkg, I3ERR_DBCONFLICT,
                         _("%s (cap %s) conflicts with installed %s (%s)"),
                         pkg_id(pkg), capreq_stra(cap),
                         pkg_id(dbpkg), capreq_stra(cnfl));
                ncnfl++;
            }
        }
    }

    n_array_free(dbpkgs);
    return ncnfl;
}

int i3_process_pkg_conflicts(int indent, struct i3ctx *ictx, struct i3pkg *i3pkg)
{
    struct pkg *pkg = i3pkg->pkg;
    int i, n, ncnfl = 0;

    if (!ictx->ts->getop(ictx->ts, POLDEK_OP_CONFLICTS))
        return 1;

    /* conflicts in install set */
    if (pkg->cnflpkgs != NULL)
        for (i = 0; i < n_array_size(pkg->cnflpkgs); i++) {
            struct reqpkg *cpkg = n_array_nth(pkg->cnflpkgs, i);

            if ((cpkg->flags & REQPKG_CONFLICT) == 0)
                continue;

            if (i3_is_marked(ictx, cpkg->pkg)) {
                i3_error(ictx, pkg, I3ERR_CONFLICT, _("%s conflicts with %s"),
                         pkg_id(pkg), pkg_id(cpkg->pkg));
                ncnfl++;
            }
        }


    /* conflicts with db packages */

    for (i = 0; i < n_array_size(pkg->caps); i++) {
        struct capreq *cap = n_array_nth(pkg->caps, i);

        if ((ictx->ps->flags & PSET_VRFY_MERCY) && capreq_is_bastard(cap))
            continue;

        msg_i(3, indent, "cap %s\n", capreq_stra(cap));
        n = find_db_conflicts_dbcnfl_with_cap(indent, ictx, pkg, cap);
        ncnfl += n;
    }


    if (pkg->cnfls != NULL)
        for (i = 0; i < n_array_size(pkg->cnfls); i++) {
            struct capreq *cnfl = n_array_nth(pkg->cnfls, i);

            if (capreq_is_obsl(cnfl))
                continue;

            msg_i(3, indent, "cnfl %s\n", capreq_stra(cnfl));

            n = find_db_conflicts_cnfl_with_db(indent, ictx, pkg, cnfl);
            ncnfl += n;
        }

    /* XXX: find file-based conflicts should be here, but it is too slow;
       rpmlib checks conflicts in special way and it is not available via API */

    return ncnfl == 0;
}
