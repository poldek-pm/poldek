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
#include "usrset.h"
#include "pkgset-req.h"


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



static void visit_mark_reqs(struct pkg *parent_pkg, struct pkg *pkg, int deep) 
{
    int i;
    
    if (pkg_is_dep_marked(pkg))
        return;
    
    if (pkg_isnot_marked(pkg)) {
        n_assert(parent_pkg != NULL);
        msgn_i(1, deep, _("%s marks %s"), pkg_snprintf_s(parent_pkg),
               pkg_snprintf_s0(pkg));
        pkg_dep_mark(pkg);
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
                    visit_mark_reqs(pkg, rpkg->pkg, deep);
            }
        }
    }
}

static
int mark_dependencies(struct pkgset *ps, int withdeps) 
{
    int i, j;
    int nerr = 0;

    for (i=0; i < n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
    
        if (pkg_is_hand_marked(pkg)) 
            visit_mark_reqs(NULL, pkg, 0);
    }

    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);

        if (pkg_isnot_marked(pkg))
            continue;
        
        if (pkg_has_badreqs(pkg)) {
            logn(LOGERR, _("%s: broken dependencies"), pkg_snprintf_s(pkg));
            nerr++;
        }
        
        if (pkg->cnflpkgs == NULL)
            continue;

        for (j=0; j<n_array_size(pkg->cnflpkgs); j++) {
            struct cnflpkg *cpkg = n_array_nth(pkg->cnflpkgs, j);
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


inline static int mark_package(struct pkg *pkg, int withdeps)
{
    if (pkg_has_badreqs(pkg) && withdeps) {
        logn(LOGERR, _("mark: %s: broken dependencies"), pkg_snprintf_s(pkg));
        
    } else {
        pkg_hand_mark(pkg);
        msgn(1, _("mark %s"), pkg_snprintf_s(pkg));
    }
    return pkg_is_marked(pkg);
}

static
int pkgset_mark_pkgdef_exact(struct pkgset *ps, const struct pkgdef *pdef,
                             int withdeps) 
{
    int i, marked = 0, matched = 0;
    struct pkg *pkg, tmpkg, *findedpkg;
    
    
    n_assert(pdef->pkg != NULL);
    
    tmpkg.name = pdef->pkg->name;
    n_array_sort(ps->pkgs);

    i = n_array_bsearch_idx_ex(ps->pkgs, pdef->pkg, (tn_fn_cmp)pkg_cmp_name); 
    if (i < 0) {
        logn(LOGERR, _("mark: %s not found"), pdef->pkg->name);
        return 0;
    }
    
    findedpkg = pkg = n_array_nth(ps->pkgs, i);
    
    if (pkgdef_match_pkg(pdef, pkg)) {
        marked = mark_package(pkg, withdeps);
        matched = 1;
        
    } else {
        i++;
        while (i < n_array_size(ps->pkgs)) {
            pkg = n_array_nth(ps->pkgs, i++);
            
            if (strcmp(pkg->name, pdef->pkg->name) != 0) 
                break;

            if (pkgdef_match_pkg(pdef, pkg)) {
                marked = mark_package(pkg, withdeps);
                matched = 1;
                break;
            }
        }
    }

    if (!marked && !matched) 
        logn(LOGERR, _("mark: %s: versions not match"), pdef->pkg->name);
    
    return marked;
}


static
int pkgset_mark_pkgdefs_patterns(struct pkgset *ps, tn_array *pkgdefs,
                                 int withdeps) 
{
    int i, j, nerr = 0;
    int *matches;


    matches = alloca(n_array_size(pkgdefs) * sizeof(*matches));
    memset(matches, 0, n_array_size(pkgdefs) * sizeof(*matches));
    
    
    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
        
        for (j=0; j<n_array_size(pkgdefs); j++) {
            struct pkgdef *pdef = n_array_nth(pkgdefs, j);
            if (pdef->tflags != PKGDEF_PATTERN)
                continue;

            n_assert(pdef->pkg != NULL);
            if (fnmatch(pdef->pkg->name, pkg->name, 0) == 0)
                matches[j] += mark_package(pkg, withdeps);
        }
    }

    for (j=0; j<n_array_size(pkgdefs); j++) {
        struct pkgdef *pdef = n_array_nth(pkgdefs, j);
        if (pdef->tflags != PKGDEF_PATTERN)
            continue;
        
        if (matches[j] == 0) {
            logn(LOGERR, _("mark: %s: no such package found"), pdef->pkg->name);
            nerr++;
        }
    }
    
    return nerr;
}


int pkgset_mark_usrset(struct pkgset *ps, struct usrpkgset *ups,
                       int withdeps, int nodeps)
{
    int i, nerr = 0, npatterns = 0;

    packages_mark(ps->pkgs, 0, PKG_INDIRMARK | PKG_DIRMARK);

    for (i=0; i < n_array_size(ups->pkgdefs); i++) {
        struct pkgdef *pdef = n_array_nth(ups->pkgdefs, i);
        if (pdef->tflags & (PKGDEF_REGNAME | PKGDEF_PKGFILE)) { 
            if (!pkgset_mark_pkgdef_exact(ps, pdef, withdeps))
                nerr++;
            
        } else if (pdef->tflags & PKGDEF_PATTERN) {
            npatterns++;

        } else if (pdef->tflags & PKGDEF_VIRTUAL) { /* VIRTUAL implies OPTIONAL */
            tn_array *avpkgs;

#if 0            
            if (pdef->pkg == NULL) {
                logn(LOGERR, _("virtual '%s': default package expected"),
                    pdef->virtname);
                nerr++;
                    
            }
#endif 
            avpkgs = pkgset_lookup_cap(ps, pdef->virtname);
            if (avpkgs == NULL || n_array_size(avpkgs) == 0) {
                logn(LOGERR, _("virtual '%s' not found"), pdef->virtname);
                nerr++;
                    
            } else {
                struct pkg *pkg;
                int n = 0;
                
                pkg = n_array_nth(avpkgs, 0);
                n = n;
#if 0                           /* NFY */
                n = inst->askpkg_fn(pdef->virtname, avpkgs);
                pkg = n_array_nth(avpkgs, n);
#endif                
                    
                if (pdef->pkg == NULL) {
                    logn(LOGNOTICE, _("%s: missing default package, using %s"),
                        pdef->virtname, pkg->name);
                        
                } else {
                    int i, found = 0;
                        
                    for (i=0; i<n_array_size(avpkgs); i++) {
                        pkg = n_array_nth(avpkgs, i);
                        if (strcmp(pkg->name, pdef->pkg->name) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    
                    if (found == 0) {
                        pkg = n_array_nth(avpkgs, 0);
                        logn(LOGWARN, _("%s: default package %s not found, "
                                        "using %s"), pdef->virtname,
                             pdef->pkg->name, pkg->name);
                    }
                } 
                            
                if (!mark_package(pkg, withdeps))
                    nerr++;
            }
                
            if (avpkgs)
                n_array_free(avpkgs);
            
        } else if (pdef->tflags & PKGDEF_OPTIONAL) {
#if 0                        /* NFY */
            if ((inst->flags & INSTS_CONFIRM_INST) && inst->ask_fn &&
                inst->ask_fn(0, "Install %s? [y/N]", pdef->pkg->name));
#endif                
            if (!pkgset_mark_pkgdef_exact(ps, pdef, withdeps))
                nerr++;
            
        } else {
            n_assert(0);
        }
    }

    if (npatterns)
        nerr += pkgset_mark_pkgdefs_patterns(ps, ups->pkgdefs, withdeps);
    
    if (withdeps) {
        msgn(1, _("\nProcessing dependencies..."));
        if (!mark_dependencies(ps, withdeps))
            nerr++;
        
        if (nerr == 0)
            msgn(1, _("Package set OK"));
        
        else {
            if (nodeps)
                packages_mark(ps->pkgs, 0, PKG_INDIRMARK | PKG_DIRMARK);
            else 
                logn(LOGERR, _("Buggy package set."));
        }
    }
    
    return nerr == 0;
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

