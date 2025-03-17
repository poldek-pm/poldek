/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>
#include <trurl/nmalloc.h>

#include <vfile/vfile.h>

#include "compiler.h"
#include "misc.h"
#include "i18n.h"
#include "log.h"
#include "pkgdir.h"
#include "pkgdir_intern.h"
#include "pkg.h"
#include "pkgu.h"
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
    return n_array_bsearch_ex(pkgdir->pkgs, pkg,
                              (tn_fn_cmp)pkg_deepstrcmp_name_evr);
}


static void setup_diff_langs(struct pkgdir *diff, struct pkgdir *orig)
{
    tn_array *orig_avlangs;
    int i;

    orig_avlangs = n_hash_keys(orig->avlangs_h);

    for (i=0; i < n_array_size(diff->pkgs); i++) {
        struct pkg      *pkg;
        tn_array        *pkg_langs;
        struct pkguinf  *pkgu;


        pkg = n_array_nth(diff->pkgs, i);

        if ((pkgu = pkg_xuinf(pkg, orig_avlangs)) == NULL)
            continue;

        if ((pkg_langs = pkguinf_langs(pkgu))) {
            int j;
            for (j=0; j < n_array_size(pkg_langs); j++)
                pkgdir__update_avlangs(diff, n_array_nth(pkg_langs, j), 1);
        }

        pkguinf_free(pkgu);
    }

    n_array_free(orig_avlangs);
}


struct pkgdir *pkgdir_diff(struct pkgdir *pkgdir, struct pkgdir *pkgdir2)
{
    tn_array *depdirs = NULL, *plus_pkgs, *minus_pkgs;
    struct pkgdir *diff;
    struct pkg *pkg;
    const char *idxpath = NULL;
    int i;

    sort_for_diff(pkgdir);
    sort_for_diff(pkgdir2);

    n_assert(pkgdir->flags & PKGDIR_UNIQED);
    //n_assert(pkgdir2->flags & PKGDIR_UNIQED);

    plus_pkgs = pkgs_array_new(256);

    for (i=0; i < n_array_size(pkgdir2->pkgs); i++) {
        struct pkg *plus_pkg;

        pkg = n_array_nth(pkgdir2->pkgs, i);
        if ((plus_pkg = search_for_diff(pkgdir, pkg)) == NULL) {
            n_array_push(plus_pkgs, pkg_link(pkg));
            msg(2, "++ %s\n", pkg_snprintf_s(pkg));
        }
    }

    minus_pkgs = pkgs_array_new(256);
    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        struct pkg *minus_pkg;

        pkg = n_array_nth(pkgdir->pkgs, i);

        if ((minus_pkg = search_for_diff(pkgdir2, pkg)) == NULL) {
            n_array_push(minus_pkgs, pkg_link(pkg));
            msg(2, "-- %s\n", pkg_snprintf_s(pkg));
        }
    }

    if (pkgdir2->depdirs) {
        depdirs = n_array_new(64, free, (tn_fn_cmp)strcmp);
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
        n_array_free(depdirs);
        return NULL;
    }

    diff = pkgdir_malloc();
    /* type is a reference to mod->type, no need to be malloced */
    diff->type = pkgdir->type;
    diff->mod = pkgdir->mod;
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
    diff->orig_ts = pkgdir->ts;
    diff->avlangs_h = pkgdir__avlangs_new();

    idxpath = pkgdir_localidxpath(pkgdir);
    diff->orig_idxpath = idxpath ? n_strdup(idxpath) : NULL;
    diff->ts = time(NULL);
    DBGF("diff: orig_ts=%s, ts=%s; pdir=%s\n", strtime_(diff->orig_ts), strtime_(diff->ts),
           strtime_(pkgdir2->ts));
    n_assert(diff->orig_idxpath);

    if (diff->pkgs)
        setup_diff_langs(diff, pkgdir2);
    pkgdir__setup_langs(pkgdir);

    if (diff->mod->posthook_diff)
        diff->mod->posthook_diff(pkgdir, pkgdir2, diff);

    return diff;
}


struct pkgdir *pkgdir_patch(struct pkgdir *pkgdir, struct pkgdir *patch)
{
    struct pkg *pkg;
    int i;

    n_assert((pkgdir->flags & PKGDIR_DIFF) == 0);

    n_assert(patch->flags & PKGDIR_DIFF);
    n_assert(patch->ts > pkgdir->ts);
    DBGF("orig=%s, ts=%s, pdir=%s\n", strtime_(patch->orig_ts), strtime_(patch->ts),
           strtime_(pkgdir->ts));
    n_assert(patch->orig_ts >= pkgdir->ts);

    pkgdir->ts = patch->ts;

    if (patch->removed_pkgs)
        for (i=0; i < n_array_size(patch->removed_pkgs); i++) {
            pkg = n_array_nth(patch->removed_pkgs, i);
            msg(2, "-- %s\n", pkg_snprintf_s(pkg));
            n_array_remove(pkgdir->pkgs, pkg);
        }

    if (patch->pkgroups) {
        for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
            pkg = n_array_nth(pkgdir->pkgs, i);
            pkg->groupid = pkgroup_idx_remap_groupid(patch->pkgroups,
                                                     pkgdir->pkgroups,
                                                     pkg->groupid, 1);
        }

        pkgroup_idx_free(pkgdir->pkgroups);
        pkgdir->pkgroups = pkgroup_idx_link(patch->pkgroups);
    }

    if (patch->pkgs) {
        tn_array *langs;

        for (i=0; i < n_array_size(patch->pkgs); i++) {
            pkg = n_array_nth(patch->pkgs, i);
            msg(2, "++ %s\n", pkg_snprintf_s(pkg));
            n_array_push(pkgdir->pkgs, pkg_link(pkg));
        }


        /* assume that diff packages have all languages, not true but it
           just for estimation */
        langs = n_hash_keys(patch->avlangs_h);
        for (i=0; i < n_array_size(langs); i++) /*  */
            pkgdir__update_avlangs(pkgdir, n_array_nth(langs, i),
                                   n_array_size(patch->pkgs));
        n_array_free(langs);
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
    pkgdir->flags |= PKGDIR_PATCHED;
    return pkgdir;
}
