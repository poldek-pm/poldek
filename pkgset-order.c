/*
  Copyright (C) 2000-2007 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/n_snprintf.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#define POLDEK_PKG_DAG_COLOURS 1  /* for pkg_*color */
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"

/*
 * Ordering: sort packages topologically
 */
struct visit_install_order_s {
    struct pkgset *ps;
    tn_array *ordered_pkgs;
    tn_array *stack;
    int nerrors;
    int verbose_level;
};


static
int visit_install_order(struct visit_install_order_s *vs, struct pkg *pkg,
                        unsigned reqpkg_flag, int deep)
{
    int i, last_stack_i = -1;
    int verb = vs->verbose_level;

    deep += 2;

    pkg_set_color(pkg, PKG_COLOR_GRAY);

    tn_array *reqpkgs = pkgset_get_required_packages(deep, vs->ps, pkg);

    if (reqpkgs == NULL || n_array_size(reqpkgs) == 0) {
        msg(verb, "_\n");
        msg_i(verb, deep, "_ visit %s  (NO REQS)", pkg->name);
        goto l_end;
    }

    n_array_push(vs->stack, pkg);
    last_stack_i = n_array_size(vs->stack) - 1;

    msg(verb, "_\n");
    msg_i(verb, deep, "_ visit %s -> (", pkg->name);
    for (i=0; i < n_array_size(reqpkgs); i++) {
        struct reqpkg *rp = n_array_nth(reqpkgs, i);

        msg(verb, "_%s%s, ", (rp->flags & reqpkg_flag) ? "*" : "",
            rp->pkg->name);

        if (rp->flags & REQPKG_MULTI) {
            int n = 0;
            while (rp->adds[n]) {
                msg(verb, "_%s%s, ",
                    (rp->adds[n]->flags & reqpkg_flag) ? "*" : "",
                    rp->adds[n]->pkg->name);
                n++;
            }
        }
    }
    msg(verb, "_) {");

    for (i=0; i < n_array_size(reqpkgs); i++) {
        struct reqpkg *rpkg, *rp;
        int np = 0;

        rpkg = rp = n_array_nth(reqpkgs, i);

        while (rp != NULL) {
            if (pkg_is_color(rp->pkg, PKG_ALL_COLORS) == 0) { // skip, it is not in our pool
                (void)0;

            } else if (pkg_is_color(rp->pkg, PKG_COLOR_WHITE)) {
                if (rp->flags & reqpkg_flag)
                    pkg_set_prereqed(rp->pkg);
                else
                    pkg_clr_prereqed(rp->pkg);

                if (reqpkg_flag == 0 || (rp->flags & reqpkg_flag))
                    visit_install_order(vs, rp->pkg, reqpkg_flag, deep);

            } else if (pkg_is_color(rp->pkg, PKG_COLOR_BLACK)) {
                msg(verb, "_\n");
                msg_i(verb, deep, "_   visited %s", rp->pkg->name);

            } else if (pkg_is_color(rp->pkg, PKG_COLOR_GRAY)) { /* cycle  */
                int is_loop = 0;

                if (rp->flags & reqpkg_flag) {
                    int j, nn = 0, nprereqs = 0;

                    for (j=n_array_size(vs->stack)-1; j >= 0; j--) {
                        struct pkg *p = n_array_nth(vs->stack, j);

                        if (p == rp->pkg)
                            break;

                        nn++;
                        if (!pkg_is_prereqed(p))
                            break;

                        nprereqs++;
                    }

                    if (nn > 0 && nn == nprereqs)
                        is_loop = 1;
                }

                if (is_loop) {
                    vs->nerrors++;

                    if (verb > 2) {
                        msg(verb, "\n");
                        msg_i(verb, deep, "   cycle   %s -> %s", pkg->name,
                              rp->pkg->name);

                    } else {
                        char *error;
                        int size, ne = 0;

                        size = n_array_size(vs->stack) * 128;
                        error = alloca(size);

                        ne += n_snprintf(error, size, _("Requires(pre) loop: "));
                        ne += n_snprintf(&error[ne], size - ne, "%s", rp->pkg->name);
                        for (int ii=n_array_size(vs->stack)-1; ii >= 0; ii--) {
                            struct pkg *p = n_array_nth(vs->stack, ii);
                            ne += n_snprintf(&error[ne], size - ne, " <- %s", p->name);
                        }
                        log(LOGERR, "%s\n", error);
                    }

                } else {
                    msg(verb, "\n");
                    msg_i(verb, deep, "   fakecycle   %s -> %s", pkg->name,
                          rp->pkg->name);
                }

            } else {
                n_assert(0);
            }

            if (rpkg->flags & REQPKG_MULTI)
                rp = rpkg->adds[np++];
            else
                rp = NULL;
        }
    }

    msg(verb, "\n");
    msg_i(verb, deep, "_ } ");
    msg(verb, "_%s",  pkg->name);
    for (i=n_array_size(vs->stack)-2; i >= 0; i--) {
        struct pkg *p = n_array_nth(vs->stack, i);
        if (p != pkg)
            msg(verb, "_ <- %s", p->name);
    }
    msg(verb, "_\n");

 l_end:
    pkg_set_color(pkg, PKG_COLOR_BLACK);
    pkg_clr_prereqed(pkg);
    msgn(verb, "push %s", pkg_snprintf_s(pkg));
    n_array_push(vs->ordered_pkgs, pkg_link(pkg));

    n_array_cfree(&reqpkgs);

    if (last_stack_i != -1)
        for (i=last_stack_i; i < n_array_size(vs->stack); i++) {
            pkg = n_array_pop(vs->stack);
            pkg_clr_prereqed(pkg);
        }

    return 0;
}

static void mapfn_clean_pkg_color(struct pkg *pkg)
{
    pkg_set_color(pkg, PKG_COLOR_WHITE);
    pkg_clr_prereqed(pkg);
}

static int do_order(struct pkgset *ps, const tn_array *pkgs, tn_array **ordered_pkgs,
                    unsigned reqpkg_flag, int verbose_level)
{
    struct pkg *pkg;
    struct visit_install_order_s vs;
    int i;

    vs.ps = ps;
    vs.ordered_pkgs = n_array_new(n_array_size(pkgs), (tn_fn_free)pkg_free, NULL);
    vs.nerrors = 0;
    vs.stack = n_array_new(128, NULL, NULL);
    vs.verbose_level = verbose_level;

    n_array_map(pkgs, (tn_fn_map1)mapfn_clean_pkg_color);

    for (i=0; i<n_array_size(pkgs); i++) {
        pkg = n_array_nth(pkgs, i);
        if (pkg_is_color(pkg, PKG_COLOR_WHITE)) {
            visit_install_order(&vs, pkg, reqpkg_flag, reqpkg_flag ? 1 : 0);
            n_array_clean(vs.stack);
        }
    }

    n_assert(n_array_size(vs.ordered_pkgs) == n_array_size(pkgs));

    n_assert(*ordered_pkgs == NULL);
    n_array_free(vs.stack);
    *ordered_pkgs = vs.ordered_pkgs;
    return vs.nerrors;
}


/* RET: number of detected loops  */
static int do_packages_order(struct pkgset *ps, const tn_array *pkgs,
                             tn_array **ordered_pkgs, int ordertype,
                             int verbose_level)
{
    tn_array *preordered = NULL;
    unsigned reqpkg_flag = 0;
    int nloops;

    n_assert(n_array_ctl_get_cmpfn(pkgs) == (tn_fn_cmp)pkg_cmp_name_evr_rev);
    n_assert(n_array_size(pkgs) > 0);

    tn_array *inpkgs = n_array_dup(pkgs, (tn_fn_dup)pkg_link);
    /* insertion sort - assuming pkgs is already sorted
       by pkg_cmp_pri_name_evr_rev() */
    n_array_isort_ex(inpkgs, (tn_fn_cmp)pkg_cmp_pri_name_evr_rev);

    /* Preordering packages using Requires: */
    msgn(verbose_level + 2, "Preordering packages...");
    do_order(ps, inpkgs, &preordered, 0, verbose_level + 2);

    switch (ordertype) {
        case PKGORDER_INSTALL:
            reqpkg_flag = REQPKG_PREREQ;
            break;

        case PKGORDER_UNINSTALL:
            reqpkg_flag = REQPKG_PREREQ_UN;
            break;

        default:
            n_assert(0);
    }
    msgn(verbose_level + 2, "Ordering packages...");
    *ordered_pkgs = NULL;
    nloops = do_order(ps, preordered, ordered_pkgs, reqpkg_flag, verbose_level + 1);

    n_array_free(preordered);
    n_array_free(inpkgs);

    return nloops;
}

int pkgset_order_ex(struct pkgset *ps, const tn_array *pkgs, tn_array **ordered, int ordertype, int verbose_level)
{
    return do_packages_order(ps, pkgs, ordered, ordertype, verbose_level);
}

int pkgset_order(struct pkgset *ps, const tn_array *pkgs, tn_array **ordered, int ordertype) {
    return pkgset_order_ex(ps, pkgs, ordered, ordertype, 3);
}
