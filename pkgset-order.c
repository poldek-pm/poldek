/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
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
#include <obstack.h>

#include <rpm/rpmlib.h>
#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>

#include "log.h"
#include "pkg.h"
#include "pkgset-def.h"
#include "pkgset.h"
#include "misc.h"
#include "usrset.h"
#include "rpmadds.h"
#include "pkgset-req.h"


#define ps_verify_mode(ps) ((ps)->flags & PSMODE_VERIFY)

static void mapfn_clean_pkg_color(struct pkg *pkg) 
{
    pkg_set_color(pkg, PKG_COLOR_WHITE);
}

/*
 * Ordering: sort packages topologically
 */
struct visit_order_s {
    tn_array *ordered_pkgs;
    tn_array *stack;
    int nerrors;
};


static
int visit_order(struct visit_order_s *vs, struct pkg *pkg, int deep) 
{
    int i, last_stack_i = -1;

    deep += 2;
    
    pkg_set_color(pkg, PKG_COLOR_GRAY);
    if (pkg->reqpkgs == NULL || n_array_size(pkg->reqpkgs) == 0) {
        msg(4, "_\n");
        msg_i(4, deep, "_ visit %s -> (NO REQS)", pkg->name);
        goto l_end;
    }

    n_array_push(vs->stack, pkg);
    last_stack_i = n_array_size(vs->stack) - 1;
    
    if (verbose > 2) {
        msg(4, "_\n");
        msg_i(4, deep, "_ visit %s -> (", pkg->name);
        for (i=0; i<n_array_size(pkg->reqpkgs); i++) {
            struct reqpkg *rp;	
            
            rp = n_array_nth(pkg->reqpkgs, i);
            msg(4, "_%s%s, ", (rp->flags & REQPKG_PREREQ) ? "*" : "",
                rp->pkg->name);
            
            if (rp->flags & REQPKG_MULTI) {
                int n = 0;
                while (rp->adds[n]) {
                    msg(4, "_%s%s, ",
                        (rp->adds[n]->flags & REQPKG_PREREQ) ? "*" : "",
                        rp->adds[n]->pkg->name);
                    n++;
                }
            }
        }
        msg(4, "_)\n");
        msg_i(4, deep, "_ {");
    }
    
    for (i=0; i<n_array_size(pkg->reqpkgs); i++) {
        struct reqpkg *rpkg, *rp;
        int n;

        n = 0;
        rpkg = rp = n_array_nth(pkg->reqpkgs, i);
        
        while (rp != NULL) {
            if (pkg_is_color(rp->pkg, PKG_COLOR_WHITE)) {
                visit_order(vs, rp->pkg, deep);
            
            } else if (pkg_is_color(rp->pkg, PKG_COLOR_BLACK)) {
                msg(4, "_\n");
                msg_i(4, deep, "_   visited %s", rp->pkg->name);
                
            } else if (pkg_is_color(rp->pkg, PKG_COLOR_GRAY)) { /* cycle  */
                if (rp->flags & REQPKG_PREREQ) {
                    vs->nerrors++;
                    
                    if (verbose > 2) {
                        msg(4, "\n");
                        msg_i(4, deep, "   cycle   %s -> %s", pkg->name,
                            rp->pkg->name);

                    } else {
                        int i;
                        log(LOGERR, "Prereq loop: ");
                        log(LOGERR, "_%s", rp->pkg->name);
                        for (i=n_array_size(vs->stack)-1; i >= 0; i--) {
                            struct pkg *p = n_array_nth(vs->stack, i);
                            log(LOGERR, "_ <- %s", p->name);
                        }
                        log(LOGERR, "_\n");
                    }
                    
                } else {
                    msg(4, "\n");
                    msg_i(4, deep, "   fakecycle   %s -> %s", pkg->name,
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
    
    if (verbose > 3) {
        msg(4, "\n");
        msg_i(4, deep, "_ } ");
        msg(4, "_%s",  pkg->name);
        for (i=n_array_size(vs->stack)-2; i >= 0; i--) {
            struct pkg *p = n_array_nth(vs->stack, i);
            if (p != pkg)
                msg(4, "_ <- %s", p->name);
        }
    }
    msg(4, "_\n");
    
 l_end:
    pkg_set_color(pkg, PKG_COLOR_BLACK);
    n_array_push(vs->ordered_pkgs, pkg);
    if (last_stack_i != -1) 
        for (i=last_stack_i; i < n_array_size(vs->stack); i++)
            n_array_pop(vs->stack);
    return 0;
}


int pkgset_order(struct pkgset *ps) 
{
    struct pkg *pkg;
    struct visit_order_s vs;
    int i;

    if (ps_verify_mode(ps))
        msg(1, "$Verifying (pre)requirements...\n");
    vs.ordered_pkgs = n_array_new(n_array_size(ps->pkgs), NULL, NULL);
    
    vs.nerrors = 0;
    vs.stack = n_array_new(128, NULL, NULL);
    
    n_array_map(ps->pkgs, (tn_fn_map1)mapfn_clean_pkg_color);

    for (i=0; i<n_array_size(ps->pkgs); i++) {
        pkg = n_array_nth(ps->pkgs, i);
        if (pkg_is_color(pkg, PKG_COLOR_WHITE)) {
	    visit_order(&vs, pkg, 1);
            n_array_clean(vs.stack);
        }
    }
    
    if (vs.nerrors) {
        ps->nerrors += vs.nerrors;
        msg(1, "%d prerequirement loops detected\n", vs.nerrors);
    } else if (ps_verify_mode(ps)) {
        msg(1, "No loops -- OK\n");
    }
        	
    
    if (verbose > 2 && ps_verify_mode(ps)) {
        msg(2, "Installation order:\n");
        for (i=0; i<n_array_size(vs.ordered_pkgs); i++) {
            pkg = n_array_nth(vs.ordered_pkgs, i);
            msg(2, "%d. %s\n", i, pkg->name);
        }
        msg(2, "\n");
    }

    n_assert(n_array_size(vs.ordered_pkgs) == n_array_size(ps->pkgs));

    if (ps->ordered_pkgs != NULL)
        n_array_free(ps->ordered_pkgs);
    
    n_array_free(vs.stack);
    ps->ordered_pkgs = vs.ordered_pkgs;
    return 1;
}


