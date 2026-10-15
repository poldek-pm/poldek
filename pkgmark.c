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
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <trurl/trurl.h>
#include <trurl/nmalloc.h>

#include "compiler.h"
#include "capreq.h"
#include "booldep.h"
#include "pkg.h"
#include "pkgset.h"
#include "pkgmisc.h"

#include "i18n.h"
#include "log.h"

struct pkgmark_set {
    unsigned flags;

    struct pkgset *ps;
    tn_hash *unreqh;

    tn_hash *ht;
    tn_alloc *na;
};

struct pkg_mark {
    struct pkg *pkg;
    uint32_t flags;
};

static inline
const char *package_id(char *buf, int size, const struct pkgmark_set *pms,
                       const struct pkg *pkg)
{
    if (pms->flags & PKGMARK_SET_IDNEVR)
        return pkg_id(pkg);

    n_snprintf(buf, size, "%p", pkg);
    return buf;
}


static void pkg_mark_free(struct pkg_mark *m)
{
    pkg_free(m->pkg);
}


struct pkgmark_set *pkgmark_set_new(struct pkgset *ps, int size, unsigned flags)
{
    struct pkgmark_set *pms;
    tn_alloc *na;

    if (flags == 0)
        flags |= PKGMARK_SET_IDNEVR; /* default */

    na = n_alloc_new(8, TN_ALLOC_OBSTACK);
    pms = na->na_malloc(na, sizeof(*na));

    pms->flags = flags;
    pms->ht = n_hash_new_na(na, size > 256 ? size : 256,
                              (tn_fn_free)pkg_mark_free);
    pms->na = na;
    pms->ps = ps;
    pms->unreqh = NULL;

    return pms;
}

void pkgmark_set_free(struct pkgmark_set *pms)
{
    n_hash_free(pms->ht);
    if (pms->unreqh) {
        n_hash_free(pms->unreqh);
    }

    n_alloc_free(pms->na);
}

tn_array *pkgmark_get_packages(struct pkgmark_set *pms, uint32_t flag)
{
    tn_hash_it it;
    tn_array *pkgs;

    if (n_hash_size(pms->ht) == 0)
        return NULL;

    n_hash_it_init(&it, pms->ht);
    pkgs = pkgs_array_new(n_hash_size(pms->ht));

    const struct pkg_mark *m;
    while ((m = n_hash_it_get(&it, NULL)) != NULL) {
        if (m->flags & flag)
            n_array_push(pkgs, pkg_link(m->pkg));
    }

    if (n_array_size(pkgs) == 0) {
        n_array_free(pkgs);
        pkgs = NULL;
    }

    return pkgs;
}


int pkgmark_set(struct pkgmark_set *pms, struct pkg *pkg,
                int set, uint32_t flag)
{
    struct pkg_mark *mark;
    char idbuf[512];
    const char *id;

    id = package_id(idbuf, sizeof(idbuf), pms, pkg);
    mark = n_hash_get(pms->ht, id);
    if (mark == NULL) {
        if (!set)
            return 1;

        mark = pms->na->na_malloc(pms->na, sizeof(*mark));
        mark->pkg = pkg_link(pkg);
        mark->flags = 0;
        n_hash_insert(pms->ht, id, mark);
    }

    if (set)
        mark->flags |= flag;
    else
        mark->flags &= ~flag;

    return 1;
}

int pkgmark_isset(const struct pkgmark_set *pms, const struct pkg *pkg,
                  uint32_t flag)
{
    struct pkg_mark *pkg_mark;
    char idbuf[512];
    const char *id;

    id = package_id(idbuf, sizeof(idbuf), pms, pkg);
    n_assert(id);

    if ((pkg_mark = n_hash_get(pms->ht, id)))
        return pkg_mark->flags & flag;

    return 0;
}

int pkgmark_pkg_drags(struct pkg *pkg, struct pkgmark_set *pms, int deep)
{
    int i, ndragged = 0;

    if (deep <= 0)
        return 0;

    n_assert(pms->ps);
    tn_array *reqpkgs = pkgset_get_required_packages(deep, pms->ps, pkg);
    if (reqpkgs == NULL)
        return 0;

    for (i=0; i < n_array_size(reqpkgs); i++) {
        struct reqpkg *rpkg = n_array_nth(reqpkgs, i);
        int markit = 1;

        if (pms && pkg_is_marked(pms, rpkg->pkg))
            continue;

        if (pms)
            n_assert(!pkg_is_dep_marked(pms, rpkg->pkg));

        if (rpkg->flags & REQPKG_MULTI) {
            struct reqpkg *eqpkg;
            int n;

            n = 1;
            eqpkg = rpkg->adds[0];
            while (eqpkg) {
                if (pms && pkg_is_marked(pms, eqpkg->pkg)) {
                    markit = 0;
                    break;
                }
                eqpkg = rpkg->adds[n++];
            }
        }

        if (markit) {
            ndragged += pkgmark_pkg_drags(rpkg->pkg, pms, deep - 1);
            ndragged++;
        }
    }

    n_array_cfree(&reqpkgs);

    return ndragged;
}

static
void visit_mark_reqs(struct pkg *parent_pkg,
                     struct capreq *req, struct pkg *pkg,
                     struct pkgmark_set *pms, int deep)
{
    int i;

    if (pkg_is_dep_marked(pms, pkg))
        return;

    if (pkg_isnot_marked(pms, pkg)) {
        n_assert(parent_pkg != NULL);
        n_assert(req != NULL);
        n_assert(deep >= 0);
        msgn_i(1, deep, _("%s marks %s (cap %s)"), pkg_snprintf_s(parent_pkg),
               pkg_snprintf_s0(pkg), capreq_snprintf_s(req));
        pkg_dep_mark(pms, pkg);
    }

    n_assert(pms->ps);
    tn_array *reqpkgs = pkgset_get_required_packages_x(deep, pms->ps, pkg, &pms->unreqh);
    if (reqpkgs == NULL)
        return;

    DBGF("%s (%s)\n", pkg_snprintf_s0(pkg),
         req ? capreq_snprintf_s(req) : "null");

    for (i=0; i < n_array_size(reqpkgs); i++) {
        struct reqpkg *rpkg = n_array_nth(reqpkgs, i);
        tn_array *equivalents = NULL;
        struct pkg *tomark = NULL;
        int markit = 1;


        if (pkg_is_marked(pms, rpkg->pkg))
            continue;

        n_assert(!pkg_is_dep_marked(pms, rpkg->pkg));

        tomark = rpkg->pkg;
        if (rpkg->flags & REQPKG_MULTI) {
            struct reqpkg *eqpkg;
            int n = 0;

            while ((eqpkg = rpkg->adds[n++])) {
                if (pkg_is_marked(pms, eqpkg->pkg)) {
                    markit = 0;
                    break;

                } else {
                    if (equivalents == NULL) {
                        equivalents = n_array_new(32, NULL, NULL);
                        n_array_push(equivalents, rpkg->pkg);
                    }
                    n_array_push(equivalents, eqpkg->pkg);
                }
            }
        }

        tomark = rpkg->pkg;
        if (markit && equivalents) {
            int j, ndragged_min = INT_MAX - 1;
            for (j=0; j < n_array_size(equivalents); j++) {
                struct pkg *p = n_array_nth(equivalents, j);
                int ndragged = pkgmark_pkg_drags(p, pms, 2);

                DBGF("- %s %d\n", pkg_snprintf_s0(p), ndragged);
                if (ndragged < ndragged_min) {
                    ndragged_min = ndragged;
                    tomark = p;
                }
            }
        }

        if (equivalents) {
            n_array_free(equivalents);
            equivalents = NULL;
        }

        if (markit) {
            n_assert(tomark);
            DBGF("-> %s\n", pkg_snprintf_s0(tomark));
            visit_mark_reqs(pkg, rpkg->req, tomark, pms, deep + 2);
        }
    }

    n_array_cfree(&reqpkgs);
}

static
int depmark_packages(struct pkgmark_set *pms,
                     const tn_array *tomark, int withdeps)
{
    int i, rc = 1;

    for (i=0; i < n_array_size(tomark); i++) {
        struct pkg *pkg = n_array_nth(tomark, i);
        pkg_hand_mark(pms, pkg);
    }

    if (withdeps) {
        for (i=0; i < n_array_size(tomark); i++) {
            struct pkg *pkg = n_array_nth(tomark, i);

            n_assert(pkg_is_hand_marked(pms, pkg));
            visit_mark_reqs(NULL, NULL, pkg, pms, -2);
        }

        if (pms->unreqh && n_hash_size(pms->unreqh) > 0) {
            rc = 0;
        }
    }

    return rc;
}

int packages_mark(struct pkgmark_set *pms, const tn_array *pkgs, struct pkgset *ps, int withdeps)
{
    if (withdeps && ps) {
        n_assert(pms->ps == NULL || pms->ps == ps);
        pms->ps = ps;
    }

    int re = depmark_packages(pms, pkgs, withdeps);

    //if (withdeps && ps) {
    //    pms->ps = NULL;
    //}

    return re;
}

void pkgmark_massset(struct pkgmark_set *pms, int set, uint32_t flag)
{
    if (n_hash_size(pms->ht) == 0)
        return;

    tn_hash_it it;
    n_hash_it_init(&it, pms->ht);

    struct pkg_mark *m;
    while ((m = n_hash_it_get(&it, NULL)) != NULL) {
        if (set)
            m->flags |= flag;
        else
            m->flags &= ~flag;
    }
}

int pkgmark_log_unsatisfied_dependecies(struct pkgmark_set *pms)
{
    tn_hash_it it;
    int nerr = 0;

    if (pms->unreqh == NULL)
        return 0;

    n_hash_it_init(&it, pms->ht);
    struct pkg_mark *m;
    while ((m = n_hash_it_get(&it, NULL)) != NULL) {
        if ((m->flags & PKGMARK_ANY) == 0)
            continue;

        const tn_array *errs = n_hash_get(pms->unreqh, pkg_id(m->pkg));
        if (!errs)
            continue;

        for (int i=0; i < n_array_size(errs); i++) {
            struct pkg_unreq *unreq = n_array_nth(errs, i);
            logn(LOGERR, _("%s.%s: req %s %s"),
                 pkg_snprintf_s(m->pkg), pkg_arch(m->pkg), unreq->req,
                 unreq->mismatch ? _("version mismatch") : _("not found"));
            nerr++;
        }
    }

    if (nerr)
        msgn(0, _("%d unsatisfied dependencies found"), nerr);
    else
        msgn(0, _("No unsatisfied dependencies found"));

    return nerr;
}

int pkgmark_verify_package_conflicts(struct pkgmark_set *pms)
{
    int nerr = 0;

    n_assert(pms->ps);

    tn_hash_it it;
    n_hash_it_init(&it, pms->ht);

    struct pkg_mark *m;
    while ((m = n_hash_it_get(&it, NULL)) != NULL) {
        if ((m->flags & PKGMARK_ANY) == 0)
            continue;

        n_assert(pkg_is_marked(pms, m->pkg));

        tn_array *cnflpkgs = pkgset_get_conflicted_packages(0, pms->ps, m->pkg);
        if (cnflpkgs == NULL)
            continue;

        for (int i=0; i < n_array_size(cnflpkgs); i++) {
            struct reqpkg *cpkg = n_array_nth(cnflpkgs, i);
            if (pkg_is_marked(pms, cpkg->pkg)) {
                logn(LOGERR, _("%s: conflicts with %s (dep %s)"), pkg_snprintf_s(m->pkg),
                     pkg_snprintf_s0(cpkg->pkg), capreq_stra(cpkg->req));
                nerr++;
            }
        }
        n_array_cfree(&cnflpkgs);
    }

    if (nerr)
        msgn(0, _("%d conflicts found"), nerr);
    else
        msgn(0, _("No conflicts found"));

    return nerr == 0;
}

/* pkg provides req: by name or Provides, or a path as a file of its whole list */
static int marked_provides(const struct pkg *pkg, const struct capreq *req)
{
    struct pkgflist_it *it;
    const char *path;
    int re = 0;

    if (pkg_match_req(pkg, req, 1))
        return 1;

    if (!capreq_is_file(req) || (it = pkg_get_flist_it(pkg)) == NULL)
        return 0;

    while (!re && (path = pkgflist_it_get(it, NULL)) != NULL)
        re = n_str_eq(path, capreq_name(req));

    pkgflist_it_free(it);
    return re;
}

struct marked_ctx {
    struct pkgset *ps;
    tn_array      *pkgs;
};

/* booldep_eval() cost over the marked packages only; what the package
   manager provides itself (rpmlib()) is met with no package */
static int marked_req_cost(const struct capreq *req, tn_array **providers,
                           void *ptr)
{
    struct marked_ctx *mc = ptr;
    tn_array *found = NULL;

    for (int i = 0; i < n_array_size(mc->pkgs); i++) {
        struct pkg *pkg = n_array_nth(mc->pkgs, i);

        if (!marked_provides(pkg, req))
            continue;

        if (providers == NULL)
            return 0;

        if (found == NULL)
            found = pkgs_array_new(4);
        n_array_push(found, pkg_link(pkg));
    }

    if (providers)
        *providers = found;

    if (found)
        return 0;

    return pkgset_pm_satisfies(mc->ps, req) ? 0 : -1;
}

int pkgmark_verify_package_boolreqs(struct pkgmark_set *pms)
{
    tn_array *pkgs = pkgmark_get_packages(pms, PKGMARK_ANY);
    struct marked_ctx mc = { pms->ps, pkgs };
    struct booldep_eval_ctx ectx = { &mc, marked_req_cost, NULL };
    int nerr = 0;

    for (int i = 0; pkgs && i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        for (int j = 0; pkg->reqs && j < n_array_size(pkg->reqs); j++) {
            struct capreq *req = n_array_nth(pkg->reqs, j);
            struct booldep *dep;
            tn_array *conjs = NULL;

            if (!booldep_is_boolean_req(req))
                continue;

            if ((dep = booldep_parse_capreq(req))) {
                conjs = booldep_eval_providers(dep, &ectx);
                booldep_free(dep);
            }

            if (conjs == NULL) {
                logn(LOGERR, _("%s: req %s is not met by the packages to install"),
                     pkg_id(pkg), capreq_stra(req));
                nerr++;
            }
            n_array_cfree(&conjs);
        }
    }

    n_array_cfree(&pkgs);
    return nerr == 0;
}

int pkgmark_verify_package_order(struct pkgmark_set *pms)
{
    n_assert(pms->ps);

    msgn(0, _("Verifying packages ordering..."));

    tn_array *pkgs = pkgmark_get_packages(pms, PKGMARK_ANY);
    tn_array *ordered = NULL;
    int nloops = pkgset_order_ex(pms->ps, pkgs, &ordered, PKGORDER_INSTALL, 2);

    if (nloops) {
        logn(LOGERR, ngettext("%d prerequirement loop detected",
                              "%d prerequirement loops detected",
                              nloops), nloops);

    } else {
        msgn(0, _("No loops -- OK"));
    }

    msgn(1, "Installation order:");
    for (int i=0; i < n_array_size(ordered); i++) {
        struct pkg *pkg = n_array_nth(ordered, i);
        msgn(1, "%d. %s", i+1, pkg_id(pkg));
    }
    n_array_free(ordered);
    n_array_free(pkgs);

    return nloops == 0;
}
