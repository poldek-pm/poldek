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

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fnmatch.h>

#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/nstr.h>

#include <vfile/vfile.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "capreq.h"
#include "pkg.h"
#include "pkgmisc.h"
#include "pkgset.h"
#include "misc.h"
#include "poldek_term.h"
#include "pm/pm.h"
#include "pkgdir/pkgdir.h"
#include "fileindex.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

struct pkgset *pkgset_new(struct pm_ctx *pmctx)
{
    struct pkgset *ps;

    ps = n_malloc(sizeof(*ps));
    memset(ps, 0, sizeof(*ps));
    ps->pkgs = pkgs_array_new(4096);

    /* just merge pkgdirs->depdirs */
    ps->depdirs = n_array_new(64, free, (tn_fn_cmp)strcmp);
    n_array_ctl(ps->depdirs, TN_ARRAY_AUTOSORTED);

    ps->pkgdirs = n_array_new(4, (tn_fn_free)pkgdir_free, NULL);

    if (pmctx)
        ps->pmctx = pmctx;

    return ps;
}

void pkgset_free(struct pkgset *ps)
{
    if (ps->cap_idx.na != NULL)
        capreq_idx_destroy(&ps->cap_idx);

    if (ps->req_idx.na != NULL)
        capreq_idx_destroy(&ps->req_idx);

    if (ps->obs_idx.na != NULL)
        capreq_idx_destroy(&ps->obs_idx);

    if (ps->cnfl_idx.na != NULL)
        capreq_idx_destroy(&ps->cnfl_idx);

    if (ps->file_idx)
        file_index_free(ps->file_idx);

    if (ps->_depinfocache) {
        n_hash_free(ps->_depinfocache);
        ps->_depinfocache = NULL;
    }

    n_array_cfree(&ps->pkgs);
    n_array_cfree(&ps->depdirs);
    n_array_cfree(&ps->pkgdirs);

    memset(ps, 0, sizeof(*ps));
    free(ps);
}

int pkgset_pm_satisfies(const struct pkgset *ps, const struct capreq *req)
{
    if (ps->pmctx)
        return pm_satisfies(ps->pmctx, req);

    return 0;
}

static void sort_pkg_caps(struct pkg *pkg)
{
    if (pkg->caps)
        n_array_sort(pkg->caps);
}

inline static void package_add_self_cap(void *pkg)
{
    pkg_add_selfcap(pkg);
}

static void add_self_cap(struct pkgset *ps)
{
    n_assert(ps->pkgs);
    n_array_map(ps->pkgs, package_add_self_cap);
}

static int pkgfl2fidx(const struct pkg *pkg, struct file_index *fidx)
{
    int i, j;

    if (pkg->fl == NULL)
        return 1;

    for (i=0; i<n_tuple_size(pkg->fl); i++) {
        struct pkgfl_ent *flent = n_tuple_nth(pkg->fl, i);
        void *fidx_dir;

        fidx_dir = file_index_add_dirname(fidx, flent->dirname);
        for (j=0; j < flent->items; j++) {
            file_index_add_basename(fidx, fidx_dir,
                                    flent->files[j], (struct pkg*)pkg);
        }
        // XXX not needed (probably)
        //if (setup)
        //    file_index_setup_idxdir(fidx_dir);
    }
    return 1;
}

static int index_package_caps(struct pkgset *ps, const struct pkg *pkg)
{
    if (pkg->caps)
        for (int i=0; i < n_array_size(pkg->caps); i++) {
            struct capreq *cap = n_array_nth(pkg->caps, i);
            capreq_idx_add(&ps->cap_idx, capreq_name(cap), capreq_name_len(cap), pkg);
        }

    pkgfl2fidx(pkg, ps->file_idx);
    return 1;
}


static int index_package_reqs(struct pkgset *ps, const struct pkg *pkg)
{
    if (pkg->reqs)
        for (int i=0; i < n_array_size(pkg->reqs); i++) {
            struct capreq *req = n_array_nth(pkg->reqs, i);
            if (capreq_is_rpmlib(req)) /* rpm caps are too expensive */
                continue;
            capreq_idx_add(&ps->req_idx, capreq_name(req), capreq_name_len(req), pkg);
        }

    if (pkg->cnfls)
        for (int i=0; i < n_array_size(pkg->cnfls); i++) {
            struct capreq *cnfl = n_array_nth(pkg->cnfls, i);
            if (capreq_is_obsl(cnfl))
                capreq_idx_add(&ps->obs_idx, capreq_name(cnfl), capreq_name_len(cnfl), pkg);
            else
                capreq_idx_add(&ps->cnfl_idx, capreq_name(cnfl), capreq_name_len(cnfl), pkg);
        }

    return 1;
}

int pkgset__index_caps(struct pkgset *ps)
{
    void *t = timethis_begin();

    if (ps->cap_idx.na != NULL)
        return 1;

    add_self_cap(ps);
    n_array_map(ps->pkgs, (tn_fn_map1)sort_pkg_caps);

    capreq_idx_init(&ps->cap_idx,  CAPREQ_IDX_CAP, 4 * n_array_size(ps->pkgs));

    n_assert(ps->file_idx == NULL);
    ps->file_idx = file_index_new(512);

    for (int i=0; i < n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
        index_package_caps(ps, pkg);
    }

    timethis_end(4, t, "ps.index");

#if ENABLE_TRACE
    extern void capreq_idx_stats(const char *prefix, struct capreq_idx *idx);
    capreq_idx_stats("cap", &ps->cap_idx);
#endif

    return 1;
}

int pkgset__index_reqs(struct pkgset *ps)
{
    if (ps->req_idx.na != NULL)
        return 1;

    capreq_idx_init(&ps->req_idx,  CAPREQ_IDX_REQ, 8 * n_array_size(ps->pkgs));
    capreq_idx_init(&ps->obs_idx,  CAPREQ_IDX_REQ, n_array_size(ps->pkgs)/5 + 4);
    capreq_idx_init(&ps->cnfl_idx, CAPREQ_IDX_REQ, n_array_size(ps->pkgs)/5 + 4);
    for (int i=0; i < n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
        index_package_reqs(ps, pkg);
    }

    return 1;
}

/*
int OLD_pkgset_verify(struct pkgset *ps)
{
    //int strict = ps->flags & PSET_VRFY_MERCY ? 0 : 1;
    int v = poldek_VERBOSE;
    void *t = timethis_begin();

    return 1;
    msgn(2, "Preparing package set dependencies...");

    //if ((ps->flags & PSET_RT_DEPS_PROCESSED) == 0) {
    //    ps->flags |= PSET_RT_DEPS_PROCESSED;

        msgn(3, " a). detecting file conflicts...");
        v = poldek_set_verbose(-1);
        //file_index_find_conflicts(ps->file_idx, strict);
        poldek_set_verbose(v);

        msgn(3, " b). dependencies...");
        //pkgset_verify_deps(ps, strict);
        MEMINF("after verify deps");
        //pkgset_verify_conflicts(ps, strict);
        //}

    timethis_end(1, t, "ps.deps");

    return 1;
}
*/

int pkgset_add_package(struct pkgset *ps, struct pkg *pkg)
{
    if (n_array_bsearch(ps->pkgs, pkg))
        return 0;

    n_array_push(ps->pkgs, pkg_link(pkg));

    if (ps->cap_idx.na != NULL) /* already indexed caps */
        index_package_caps(ps, pkg);

    if (ps->req_idx.na != NULL) /* already indexed reqs */
        index_package_reqs(ps, pkg);

    return 1;
}

int pkgset_remove_package(struct pkgset *ps, struct pkg *pkg)
{
    int i, j, nth;

    if ((nth = n_array_bsearch_idx(ps->pkgs, pkg)) == -1)
        return 0;

    pkg = n_array_nth(ps->pkgs, nth);

    if (ps->cap_idx.na != NULL) {
        if (pkg->caps)
            for (j=0; j < n_array_size(pkg->caps); j++) {
                struct capreq *cap = n_array_nth(pkg->caps, j);
                capreq_idx_remove(&ps->cap_idx, capreq_name(cap), pkg);
            }

        if (pkg->fl)
            for (i=0; i < n_tuple_size(pkg->fl); i++) {
                struct pkgfl_ent *flent = n_tuple_nth(pkg->fl, i);

                for (j=0; j < flent->items; j++)
                    file_index_remove(ps->file_idx, flent->dirname,
                                      flent->files[j]->basename, pkg);
        }
    }

    if (ps->req_idx.na != NULL) {
        if (pkg->reqs)
            for (j=0; j < n_array_size(pkg->reqs); j++) {
                struct capreq *req = n_array_nth(pkg->reqs, j);
                capreq_idx_remove(&ps->req_idx, capreq_name(req), pkg);
            }

        if (pkg->cnfls)
            for (j=0; j < n_array_size(pkg->cnfls); j++) {
                struct capreq *cnfl = n_array_nth(pkg->cnfls, j);
                if (capreq_is_obsl(cnfl))
                    capreq_idx_remove(&ps->obs_idx, capreq_name(cnfl), pkg);
                else
                    capreq_idx_remove(&ps->cnfl_idx, capreq_name(cnfl), pkg);
            }
    }

    n_array_remove_nth(ps->pkgs, nth);
    return 1;
}

static
tn_array *find_package(struct pkgset *ps, tn_array *pkgs, const char *name)
{
    struct pkg tmpkg, *pkg;
    int i;

    tmpkg.name = (char*)name;

    n_array_sort(ps->pkgs);
    i = n_array_bsearch_idx_ex(ps->pkgs, &tmpkg, (tn_fn_cmp)pkg_cmp_name);
    if (i < 0)
        return NULL;

    pkg = n_array_nth(ps->pkgs, i);
    while (strcmp(name, pkg->name) == 0) {
        n_array_push(pkgs, pkg_link(pkg));
        i++;
        if (i == n_array_size(ps->pkgs))
            break;
        pkg = n_array_nth(ps->pkgs, i);
    }

    return pkgs;
}

static
tn_array *find_capreq(struct pkgset *ps, tn_array *pkgs,
                      enum pkgset_search_tag tag,
                      const char *name)
{
    const struct capreq_idx_ent *ent = NULL;
    int len = strlen(name);

    switch (tag) {
        case PS_SEARCH_CAP:
            pkgset__index_caps(ps);
            ent = capreq_idx_lookup(&ps->cap_idx, name, len);
            break;

        case PS_SEARCH_REQ:
            pkgset__index_reqs(ps);
            ent = capreq_idx_lookup(&ps->req_idx, name, len);
            break;

        case PS_SEARCH_OBSL:
            pkgset__index_reqs(ps);
            ent = capreq_idx_lookup(&ps->obs_idx, name, len);
            break;

        case PS_SEARCH_CNFL:
            pkgset__index_reqs(ps);
            ent = capreq_idx_lookup(&ps->cnfl_idx, name, len);
            break;

        default:
            n_assert(0);
            break;
    }


    if (ent && ent->items > 0) {
        for (unsigned i=0; i < ent->items; i++)
            n_array_push(pkgs, pkg_link(ent->pkgs[i]));

    }

    return pkgs;
}

static
tn_array *do_search_provdir(struct pkgset *ps, tn_array *pkgs, const char *dir)
{
    tn_array *tmp = pkgs_array_new(32);
    int i, pkgs_passsed = 1;

    for (i=0; i < n_array_size(ps->pkgdirs); i++) {
        struct pkgdir *pkgdir = n_array_nth(ps->pkgdirs, i);

        if (pkgdir->dirindex == NULL)
            continue;

        pkgdir_dirindex_get(pkgdir, tmp, dir);
    }

    if (pkgs == NULL) {
        pkgs = n_array_clone(tmp);
        pkgs_passsed = 0;
    }

    for (i=0; i < n_array_size(tmp); i++) {
        struct pkg *tmpkg, *pkg = n_array_nth(tmp, i);
        DBGF("  - considering %s\n", pkg_id(pkg));

        if (pkg_is_scored(pkg, (PKG_IGNORED | PKG_IGNORED_UNIQ)))
            continue;

        if ((tmpkg = n_array_bsearch(ps->pkgs, pkg)) && tmpkg == pkg)
            n_array_push(pkgs, pkg_link(pkg));
    }
    n_array_free(tmp);

    if (!pkgs_passsed && n_array_size(pkgs) == 0)
        n_array_cfree(&pkgs);

#if ENABLE_TRACE
    if (pkgs) {
        DBGF("%s ", dir);
        pkgs_array_dump(pkgs, "packages");
    }
#endif
    return pkgs;
}

tn_array *pkgset_search_provdir(struct pkgset *ps, const char *dir)
{
    return do_search_provdir(ps, NULL, dir);
}

tn_array *pkgset_search(struct pkgset *ps, enum pkgset_search_tag tag,
                        const char *value)
{
    tn_array *pkgs;

    n_array_sort(ps->pkgs);
    pkgs = pkgs_array_new_ex(4, pkg_cmp_name_evr_rev);

    switch (tag) {
        case PS_SEARCH_RECNO:
            if (value) {
                logn(LOGERR, "SEARCH_RECNO is not implemented");
                return NULL;
            }

            n_assert(value == NULL); /* not implemented */
            n_array_free(pkgs);
            pkgs = n_array_dup(ps->pkgs, (tn_fn_dup)pkg_link);
            n_array_ctl_set_cmpfn(pkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev);
            break;

        case PS_SEARCH_NAME:
            if (value)
                find_package(ps, pkgs, value);
            else {
                n_array_free(pkgs);
                pkgs = n_array_dup(ps->pkgs, (tn_fn_dup)pkg_link);
                n_array_ctl_set_cmpfn(pkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev);
            }
            break;

        case PS_SEARCH_PROVIDES:
            n_assert(value);
            find_capreq(ps, pkgs, PS_SEARCH_CAP, value);
            /* fallthru */
            /* no break => we lookup into files too */

        case PS_SEARCH_FILE:
            pkgset__index_caps(ps);
            n_assert(value);
            if (*value != '/')
                break;
            else {
                struct pkg *buf[1024];
                int i, n = 0;

                n = file_index_lookup(ps->file_idx, value, 0, buf, 1024);
                n_assert(n >= 0);

                if (n) {
                    for (i=0; i < n; i++)
                        n_array_push(pkgs, pkg_link(buf[i]));

                } else {
                    DBGF("s %s\n", value);
                    do_search_provdir(ps, pkgs, value);
                }
            }
            break;

        default:
            n_assert(value);
            find_capreq(ps, pkgs, tag, value);
            break;

    }

    if (n_array_size(pkgs) == 0) {
        n_array_free(pkgs);
        pkgs = NULL;
    }

    return pkgs;
}
