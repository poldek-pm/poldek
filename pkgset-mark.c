/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@k2.net.pl>

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

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <obstack.h>
#include <fnmatch.h>

#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/nstr.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"
#include "pkgset-req.h"
#include "capreq.h"


struct flags_s {
    unsigned flags_on;
    unsigned flags_off;
};


static void mapfn_mark2(struct pkg *pkg, struct flags_s *fs) 
{
    n_assert(fs);

    if (fs->flags_on)
        pkg->flags |= fs->flags_on;

    if (fs->flags_off)
        pkg->flags &= ~fs->flags_off;
    
}

void packages_mark(tn_array *pkgs, unsigned flags_on, unsigned flags_off) 
{
    struct flags_s fs;

    fs.flags_on = flags_on;
    fs.flags_off = flags_off;
    
    n_array_map_arg(pkgs, (tn_fn_map2) mapfn_mark2, &fs);
}



static
void visit_mark_reqs(struct pkg *parent_pkg, struct capreq *req, struct pkg *pkg, 
                     tn_array *marked, int deep) 
{
    int i;
    
    if (pkg_is_dep_marked(pkg))
        return;
    
    if (pkg_isnot_marked(pkg)) {
        n_assert(parent_pkg != NULL);
        n_assert(req != NULL);
        msgn_i(1, deep, _("%s marks %s (cap %s)"), pkg_snprintf_s(parent_pkg),
               pkg_snprintf_s0(pkg), capreq_snprintf_s(req));
        pkg_dep_mark(pkg);
        n_array_push(marked, pkg_link(pkg));
    }
    
    deep += 2;
    if (pkg->reqpkgs) {
        for (i=0; i<n_array_size(pkg->reqpkgs); i++) {
            struct reqpkg *rpkg = n_array_nth(pkg->reqpkgs, i);

            if (pkg_is_marked(rpkg->pkg))
                continue;
            
            if (!pkg_is_dep_marked(rpkg->pkg)) {
                int markit = 1;
                
                if (rpkg->flags & REQPKG_MULTI) {
                    struct reqpkg *eqpkg;
                    int n;

                    n = 1;
                    eqpkg = rpkg->adds[0];
                    while (eqpkg) {
                        if (pkg_is_marked(eqpkg->pkg)) {
                            markit = 0;
                            break;
                        }
                        eqpkg = rpkg->adds[n++];
                    }
                }

                if (markit)
                    visit_mark_reqs(pkg, rpkg->req, rpkg->pkg, marked, deep);
            }
        }
    }
}

static
int mark_dependencies(tn_array *pkgs, tn_array *marked, int withdeps) 
{
    int i, j;
    int nerr = 0;
    
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
    
        if (pkg_is_hand_marked(pkg)) 
            visit_mark_reqs(NULL, NULL, pkg, marked, 0);
    }

    for (i=0; i < n_array_size(marked); i++) {
        struct pkg *pkg = n_array_nth(marked, i);

        n_assert(pkg_is_marked(pkg));
        if (pkg_isnot_marked(pkg))
            continue;

        if (pkg_has_badreqs(pkg)) {
            //logn(LOGERR, _("%s: broken dependencies"), pkg_snprintf_s(pkg));
            nerr++;
        }
        
        if (pkg->cnflpkgs == NULL)
            continue;

        for (j=0; j < n_array_size(pkg->cnflpkgs); j++) {
            struct reqpkg *cpkg = n_array_nth(pkg->cnflpkgs, j);
            if (pkg_is_marked(cpkg->pkg)) {
                logn(LOGERR, _("conflict between %s and %s"), pkg->name,
                     cpkg->pkg->name);
                nerr++;
            }
        }
    }
    
    if (nerr && withdeps == 0)
        nerr = 0;
    
    return nerr == 0;
}

#if 0
inline static int mark_package(struct pkg *pkg, int withdeps)
{
    if (pkg_has_badreqs(pkg) && withdeps && 0) {
        logn(LOGERR, _("mark: %s: broken dependencies"), pkg_snprintf_s(pkg));
        
    } else {
        pkg_hand_mark(pkg);
        msgn(2, _("mark %s"), pkg_snprintf_s(pkg));
    }
    return pkg_is_marked(pkg);
}
#endif

static
int packages_depmark_packages(tn_array *pkgs, const tn_array *tomark,
                              tn_array *marked, int withdeps)
{
    int i, nerr = 0;
    
    packages_mark(pkgs, 0, PKG_INDIRMARK | PKG_DIRMARK);

    for (i=0; i < n_array_size(tomark); i++) {
        struct pkg *pkg = n_array_nth(tomark, i);

        if (pkg_has_badreqs(pkg))
            nerr++;
        pkg_hand_mark(pkg);
        n_array_push(marked, pkg_link(pkg));
        
#if 0                           /* old */
        if (mark_package(pkg, withdeps))
            n_array_push(marked, pkg_link(pkg));
#endif        
    }
    
    if (withdeps) {
        if (!mark_dependencies(pkgs, marked, withdeps))
            nerr++;
    }

    packages_mark(pkgs, 0, PKG_INDIRMARK | PKG_DIRMARK); /* clear tmp marks */
    return nerr == 0;
}


int pkgset_mark_packages(struct pkgset *ps, const tn_array *pkgs,
                         tn_array *marked, int withdeps)
{
    return packages_depmark_packages(ps->pkgs, pkgs, marked, withdeps);
}


struct pkg *pkgset_lookup_pkgn(struct pkgset *ps, const char *name) 
{
    struct pkg tmpkg;
    int i;
    
    tmpkg.name = (char*)name;

    n_array_sort(ps->pkgs);
    i = n_array_bsearch_idx_ex(ps->pkgs, &tmpkg, (tn_fn_cmp)pkg_cmp_name); 
    if (i < 0)
        return NULL;

    return n_array_nth(ps->pkgs, i);
}


tn_array *pkgset_lookup_cap(struct pkgset *ps, const char *capname)
{
    const struct capreq_idx_ent *ent;
    tn_array *pkgs = NULL;
    
    if ((ent = capreq_idx_lookup(&ps->cap_idx, capname))) {
        int i;
        
        pkgs = n_array_new(ent->items, NULL, (tn_fn_cmp)pkg_cmp_name_evr_rev);
        for (i=0; i<ent->items; i++) 
            n_array_push(pkgs, ent->pkgs[i]);

        if (n_array_size(pkgs) == 0) {
            n_array_free(pkgs);
            pkgs = NULL;
        }
    }

    return pkgs;
}

