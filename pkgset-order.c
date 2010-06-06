/* 
  Copyright (C) 2000-2007 Pawel A. Gajda (mis@pld-linux.org)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

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
#include "pkgset-req.h"

/*
 * Ordering: sort packages topologically
 */
struct visit_install_order_s {
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
    if (pkg->reqpkgs == NULL || n_array_size(pkg->reqpkgs) == 0) {
        msg(verb, "_\n");
        msg_i(verb, deep, "_ visit %s  (NO REQS)", pkg->name);
        goto l_end;
    }

    n_array_push(vs->stack, pkg);
    last_stack_i = n_array_size(vs->stack) - 1;
    
    msg(verb, "_\n");
    msg_i(verb, deep, "_ visit %s -> (", pkg->name);
    for (i=0; i < n_array_size(pkg->reqpkgs); i++) {
        struct reqpkg *rp;	
        
        rp = n_array_nth(pkg->reqpkgs, i);
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
    
    for (i=0; i < n_array_size(pkg->reqpkgs); i++) {
        struct reqpkg *rpkg, *rp;
        int n;

        n = 0;
        rpkg = rp = n_array_nth(pkg->reqpkgs, i);
        
        while (rp != NULL) {
            if (pkg_is_color(rp->pkg, PKG_COLOR_WHITE)) {
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
                    int j, n = 0, nprereqs = 0;
                    
                    for (j=n_array_size(vs->stack)-1; j >= 0; j--) {
                        struct pkg *p = n_array_nth(vs->stack, j);

                        if (p == rp->pkg)
                            break;

                        n++;
                        if (!pkg_is_prereqed(p))
                            break;

                        nprereqs++;
                    }
                    
                    if (n > 0 && n == nprereqs)
                        is_loop = 1;
                }
                
                if (is_loop) {
                    vs->nerrors++;
                    
                    if (verb > 2) {
                        msg(verb, "\n");
                        msg_i(verb, deep, "   cycle   %s -> %s", pkg->name,
                              rp->pkg->name);

                    } else {
                        int i, size, n = 0;
                        char *error;

                        size = n_array_size(vs->stack) * 128;
                        error = alloca(size);

                        n = 0;
                        n += n_snprintf(error, size, _("Requires(pre) loop: "));
                        n += n_snprintf(&error[n], size - n, "%s", rp->pkg->name);
                        for (i=n_array_size(vs->stack)-1; i >= 0; i--) {
                            struct pkg *p = n_array_nth(vs->stack, i);
                            n += n_snprintf(&error[n], size - n, " <- %s", p->name);
                        }
                        log(LOGERR, "%s\n", error);
                    }
                    
                } else {
                    msg(verb, "\n");
                    msg_i(verb, deep, "   fakecycle   %s -> %s", pkg->name,
                          rp->pkg->name);
                }
                
            } else 
                n_assert(0);
            
            if (rpkg->flags & REQPKG_MULTI)
                rp = rpkg->adds[n++];
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
    n_array_push(vs->ordered_pkgs, pkg);
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

static int do_order(tn_array *pkgs, tn_array **ordered_pkgs,
                    unsigned reqpkg_flag, int verbose_level)
{
    struct pkg *pkg;
    struct visit_install_order_s vs;
    int i;
    
    vs.ordered_pkgs = n_array_new(n_array_size(pkgs), NULL, NULL);
//                                  (tn_fn_free)pkg_free, NULL);
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
static int do_packages_order(tn_array *pkgs, tn_array **ordered_pkgs, int ordertype,
                             int verbose_level)
{
    tn_array *preordered = NULL;
    unsigned reqpkg_flag = 0;
    int nloops;
    
    n_assert(n_array_ctl_get_cmpfn(pkgs) == (tn_fn_cmp)pkg_cmp_name_evr_rev);
    /* insertion sort - assuming pkgs is already sorted
       by pkg_cmp_pri_name_evr_rev() */
    n_array_isort_ex(pkgs, (tn_fn_cmp)pkg_cmp_pri_name_evr_rev);

    /* Preordering packages using Requires: */
    msgn(verbose_level + 2, "Preordering packages...");
    do_order(pkgs, &preordered, 0, verbose_level + 2);
    
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
    nloops = do_order(preordered, ordered_pkgs, reqpkg_flag, verbose_level + 1);
    
    n_array_free(preordered);
    n_array_isort(pkgs);
    
    return nloops;
}

int packages_order(tn_array *pkgs, tn_array **ordered, int ordertype)
{
    return do_packages_order(pkgs, ordered, ordertype, 3);
}

int packages_order_and_verify(tn_array *pkgs, tn_array **ordered, int ordertype,
                              int verbose_level)
{
    int nloops, i;

    msgn(verbose_level, _("Verifying packages ordering..."));

    nloops = do_packages_order(pkgs, ordered, ordertype, verbose_level);
    
    if (nloops) {
		logn(LOGERR, ngettext("%d prerequirement loop detected",
                              "%d prerequirement loops detected",
                              nloops), nloops);
		
    } else {
        msgn(verbose_level, _("No loops -- OK"));
    }
    
            
    msgn(verbose_level, "Installation order:");
    for (i=0; i < n_array_size(*ordered); i++) {
        struct pkg *pkg = n_array_nth(*ordered, i);
        msgn(verbose_level, "%d. %s", i, pkg_id(pkg));
    }
    msg(verbose_level, "\n");
    return nloops == 0;
}
