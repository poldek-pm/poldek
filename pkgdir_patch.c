/* 
  Copyright (C) 2000 - 2002 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>

#include <vfile/vfile.h>

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "pkgdir.h"
#include "pkg.h"
#include "pkgroup.h"

#if 0
static void dump_arr(tn_array *pkgs)
{
    int i;
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        printf("%s, ", pkg_snprintf_s(pkg));
    }
    printf("\n");

}
#endif

static __inline__
void sort_for_diff(struct pkgdir *pkgdir) 
{
    n_array_sort_ex(pkgdir->pkgs, (tn_fn_cmp)pkg_deepstrcmp_name_evr);
}

static __inline__
struct pkg *search_for_diff(struct pkgdir *pkgdir, struct pkg *pkg) 
{
    return n_array_bsearch_ex(pkgdir->pkgs, pkg, (tn_fn_cmp)pkg_deepstrcmp_name_evr);
}


struct pkgdir *pkgdir_diff(struct pkgdir *pkgdir, struct pkgdir *pkgdir2) 
{
    struct pkg *pkg;
    tn_array *depdirs = NULL, *plus_pkgs, *minus_pkgs;
    struct pkgdir *diff;
    int i;

    sort_for_diff(pkgdir);
    sort_for_diff(pkgdir2);
    
    n_assert(pkgdir->flags & PKGDIR_UNIQED);
    n_assert(pkgdir2->flags & PKGDIR_UNIQED);
    
    plus_pkgs = pkgs_array_new(256);

    for (i=0; i < n_array_size(pkgdir2->pkgs); i++) {
        struct pkg *plus_pkg;
        
        pkg = n_array_nth(pkgdir2->pkgs, i);
        if ((plus_pkg = search_for_diff(pkgdir, pkg)) == NULL) {
            n_array_push(plus_pkgs, pkg);
            msg(2, "+ %s %p\n", pkg_snprintf_s(pkg), plus_pkg);
        }
    }

    minus_pkgs = pkgs_array_new(256);
    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        struct pkg *minus_pkg;
        
        pkg = n_array_nth(pkgdir->pkgs, i);

        if ((minus_pkg = search_for_diff(pkgdir2, pkg)) == NULL) {
            n_array_push(minus_pkgs, pkg);
            msg(2, "- %s\n", pkg_snprintf_s(pkg));
        }
    }
    
    if (pkgdir2->depdirs) {
        depdirs = n_array_new(64, NULL, (tn_fn_cmp)strcmp);
        for (i=0; i < n_array_size(pkgdir2->depdirs); i++)
            n_array_push(depdirs, n_strdup(n_array_nth(pkgdir2->depdirs, i)));
    }

    if (n_array_size(plus_pkgs) == 0) {
        n_array_free(plus_pkgs);
        plus_pkgs = NULL;
    }

    if (n_array_size(minus_pkgs) == 0) {
        n_array_free(minus_pkgs);
        minus_pkgs = NULL;
    }

    n_array_sort(pkgdir->pkgs);
    n_array_sort(pkgdir2->pkgs);

    if (minus_pkgs == NULL && plus_pkgs == NULL) { /* equal */
        if (depdirs)
            n_array_free(depdirs);
        return NULL;
    }

    diff = pkgdir_malloc();
    diff->pkgs = plus_pkgs;
    diff->removed_pkgs = minus_pkgs;
    diff->name = n_strdup("DIFF");
    diff->path = NULL;
    diff->idxpath = NULL;
    diff->depdirs = depdirs;
    diff->foreign_depdirs = NULL;
    diff->pkgs = plus_pkgs;
    diff->pkgroups = pkgroup_idx_link(pkgdir2->pkgroups);
    diff->flags = PKGDIR_DIFF;
    diff->ts_orig = pkgdir->ts;
    diff->mdd_orig = n_strdup(pkgdir->pdg->mdd);
    diff->vf = NULL;
    
    return diff;
}


struct pkgdir *pkgdir_patch(struct pkgdir *pkgdir, struct pkgdir *patch) 
{
    struct pkg *pkg;
    int i;

    n_assert((pkgdir->flags & PKGDIR_DIFF) == 0);
    
    n_assert(patch->flags & PKGDIR_DIFF);
    n_assert(patch->ts > pkgdir->ts);
    n_assert(patch->ts_orig >= pkgdir->ts);

    pkgdir->ts = patch->ts;
    pkgdir->flags |= PKGDIR_PATCHED;
    
    if (patch->removed_pkgs)
	for (i=0; i < n_array_size(patch->removed_pkgs); i++) {
            pkg = n_array_nth(patch->removed_pkgs, i);
            msg(2, "- %s\n", pkg_snprintf_s(pkg));
            n_array_remove(pkgdir->pkgs, pkg);
        }

    if (patch->pkgroups) {
        for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
            pkg = n_array_nth(pkgdir->pkgs, i);
            pkg->groupid = pkgroup_idx_remap_groupid(patch->pkgroups,
                                                     pkgdir->pkgroups,
                                                     pkg->groupid);
        }
        
        pkgroup_idx_free(pkgdir->pkgroups);
        pkgdir->pkgroups = pkgroup_idx_link(patch->pkgroups);
    }
    
    if (patch->pkgs)
        for (i=0; i < n_array_size(patch->pkgs); i++) {
            pkg = n_array_nth(patch->pkgs, i);
            msg(2, "+ %s\n", pkg_snprintf_s(pkg));
            n_array_push(pkgdir->pkgs, pkg_link(pkg));
        }

    if (pkgdir->depdirs) {
        n_array_free(pkgdir->depdirs);
        pkgdir->depdirs = NULL;
    }
    
    if (patch->depdirs) {
        pkgdir->depdirs = n_array_clone(patch->depdirs);
        for (i=0; i < n_array_size(patch->depdirs); i++) 
            n_array_push(pkgdir->depdirs, n_strdup(n_array_nth(patch->depdirs, i)));
        
        n_array_sort(pkgdir->depdirs);
    }
    
    return pkgdir;
}

