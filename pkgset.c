/*
  Copyright (C) 2000 - 2004 Pawel A. Gajda <mis@k2.net.pl>

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
#include <fnmatch.h>

#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/nstr.h>

#include <vfile/vfile.h>

#include "i18n.h"
#include "log.h"
#include "capreq.h"
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"
#include "pkgset-req.h"
#include "split.h"
#include "poldek_term.h"
#include "pm/pm.h"
#include "pkgdir/pkgdir.h"
#include "fileindex.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define _PKGSET_INDEXES_INIT      (1 << 20) /* internal flag  */

/* prototypes from ask.c */
int ask_yn(int default_a, const char *fmt, ...);
int ask_pkg(const char *capname, struct pkg **pkgs, struct pkg *deflt);

static
int do_pkgset_add_package(struct pkgset *ps, struct pkg *pkg, int rt);

struct pkgset *pkgset_new(struct pm_ctx *pmctx)
{
    struct pkgset *ps;
    
    ps = n_malloc(sizeof(*ps));
    memset(ps, 0, sizeof(*ps));
    ps->pkgs = pkgs_array_new(2048);
    ps->_pm_nevr_pkgs = NULL;
    ps->_pm_nevr_pkgs = NULL;
    ps->ordered_pkgs = NULL;
    
    /* just merge pkgdirs->depdirs */
    ps->depdirs = n_array_new(64, NULL, (tn_fn_cmp)strcmp);
    n_array_ctl(ps->depdirs, TN_ARRAY_AUTOSORTED);
    
    ps->pkgdirs = n_array_new(4, (tn_fn_free)pkgdir_free, NULL);
    ps->flags = 0;
    
    if (pmctx) {
        ps->pmcaps = pm_get_pmcaps(pmctx);
        ps->pmctx = pmctx;
    }
    return ps;
}

tn_array *pkgset_get_unsatisfied_reqs(struct pkgset *ps, struct pkg *pkg)
{
    if (ps->_vrfy_unreqs == NULL)
        return NULL;
    return n_hash_get(ps->_vrfy_unreqs, pkg_id(pkg));
}


void pkgset_free(struct pkgset *ps) 
{
    if (ps->flags & _PKGSET_INDEXES_INIT) {
        capreq_idx_destroy(&ps->cap_idx);
        capreq_idx_destroy(&ps->req_idx);
        capreq_idx_destroy(&ps->obs_idx);
        capreq_idx_destroy(&ps->cnfl_idx);
        file_index_free(ps->file_idx);
        ps->flags &= (unsigned)~_PKGSET_INDEXES_INIT;
    }
    
    if (ps->_vrfy_unreqs) {
        n_hash_free(ps->_vrfy_unreqs);
        ps->_vrfy_unreqs = NULL;
    }

    if (ps->_vrfy_file_conflicts)
        n_array_cfree(&ps->_vrfy_file_conflicts);
    
    n_array_cfree(&ps->pkgs);

    if (ps->ordered_pkgs)
        n_array_cfree(&ps->ordered_pkgs);
    
    if (ps->depdirs)
        n_array_cfree(&ps->depdirs);
    
    if (ps->pkgdirs)
        n_array_cfree(&ps->pkgdirs);

    if (ps->pmcaps)
        n_array_cfree(&ps->pmcaps);
    
    memset(ps, 0, sizeof(*ps));
    free(ps);
}


int pkgset_pm_satisfies(const struct pkgset *ps, const struct capreq *req)
{
    struct capreq *cap;

    if (ps->pmcaps == NULL)
        return 0;               /* no caps -> assume NO */

    /* internal caps have names like name(feature) */
    if (!capreq_is_rpmlib(req) && strstr(capreq_name(req), "(") == NULL)
        return 0;

    cap = n_array_bsearch_ex(ps->pmcaps, req,
                             (tn_fn_cmp)capreq_cmp_name);
    
    if (cap && cap_match_req(cap, req, 1))
        return 1;

    if (ps->pmctx)
        return pm_satisfies(ps->pmctx, req);
    
    return 0;
}

int pkgset_has_errors(struct pkgset *ps) 
{
    int rc;

    rc = ps->nerrors;
    ps->nerrors = 0;
    return rc;
}


static void sort_pkg_caps(struct pkg *pkg) 
{
    if (pkg->caps)
        n_array_sort(pkg->caps);
}

static void add_self_cap(struct pkgset *ps) 
{
    n_assert(ps->pkgs);
    n_array_map(ps->pkgs, (tn_fn_map1)pkg_add_selfcap);
}

static int pkgfl2fidx(const struct pkg *pkg, struct file_index *fidx, int setup)
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
        if (setup)
            file_index_setup_idxdir(fidx_dir);
    }
    return 1;
}


static int pkgset_index(struct pkgset *ps) 
{
    int i;
    if (ps->flags & _PKGSET_INDEXES_INIT)
        return 1;
    
    msg(2, "Indexing...\n");
    add_self_cap(ps);
    n_array_map(ps->pkgs, (tn_fn_map1)sort_pkg_caps);
    MEMINF("after index[selfcap]");
    
    /* build indexes */
    capreq_idx_init(&ps->cap_idx,  CAPREQ_IDX_CAP, 4 * n_array_size(ps->pkgs));
    capreq_idx_init(&ps->req_idx,  CAPREQ_IDX_REQ, 4 * n_array_size(ps->pkgs));
    capreq_idx_init(&ps->obs_idx,  CAPREQ_IDX_REQ, n_array_size(ps->pkgs)/5 + 4);
    capreq_idx_init(&ps->cnfl_idx, CAPREQ_IDX_REQ, n_array_size(ps->pkgs)/5 + 4);
    ps->file_idx = file_index_new(512);
    ps->flags |= _PKGSET_INDEXES_INIT;

    for (i=0; i < n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
        
        if (i % 200 == 0) 
            msg(3, " %d..\n", i);

        do_pkgset_add_package(ps, pkg, 0);
    }
    MEMINF("after index");
    
#if 0
    capreq_idx_stats("cap", &ps->cap_idx);
    capreq_idx_stats("req", &ps->req_idx);
    capreq_idx_stats("obs", &ps->obs_idx);
#endif    
    file_index_setup(ps->file_idx);
    msg(3, " ..%d done\n", i);
    
    return 0;
}

static
int do_pkg_cmp_uniq_nevr(const struct pkg *p1, struct pkg *p2) 
{
    register int rc;
    
    if ((rc = pkg_cmp_uniq_name_evr(p1, p2)) == 0)
        pkg_score(p2, PKG_IGNORED_UNIQ);
    return rc;
}

static
int do_pkg_cmp_uniq_n(const struct pkg *p1, struct pkg *p2) 
{
    register int rc;
    
    if ((rc = pkg_cmp_uniq_name(p1, p2)) == 0)
        pkg_score(p2, PKG_IGNORED_UNIQ);
    return rc;
}


int pkgset_setup(struct pkgset *ps, unsigned flags) 
{
    int n;
    int strict;
    int v = poldek_VERBOSE;

    MEMINF("before setup");
    ps->flags |= flags;
    strict = ps->flags & PSET_VRFY_MERCY ? 0 : 1;

    n = n_array_size(ps->pkgs);
    n_array_sort(ps->pkgs);
    
    if (flags & PSET_UNIQ_PKGNAME) {
        //n_array_isort_ex(ps->pkgs, (tn_fn_cmp)pkg_cmp_name_srcpri);
        // <=  0.18.3 behaviour
        n_array_isort_ex(ps->pkgs, (tn_fn_cmp)pkg_cmp_name_evr_arch_rev_srcpri);
        n_array_uniq_ex(ps->pkgs, (tn_fn_cmp)do_pkg_cmp_uniq_n);
            
    } else {
        n_array_isort_ex(ps->pkgs, (tn_fn_cmp)pkg_cmp_name_evr_arch_rev_srcpri);
        n_array_uniq_ex(ps->pkgs, (tn_fn_cmp)do_pkg_cmp_uniq_nevr);
    }
        
    if (n != n_array_size(ps->pkgs)) {
        n -= n_array_size(ps->pkgs);
        msgn(1, ngettext(
                 "Removed %d duplicate package from available set",
                 "Removed %d duplicate packages from available set", n), n);
    }

    MEMINF("before index");
    pkgset_index(ps);
    MEMINF("after index");
    v = poldek_VERBOSE;
    poldek_VERBOSE = -1;
    file_index_find_conflicts(ps->file_idx, strict);
    poldek_VERBOSE = v;
 
    pkgset_verify_deps(ps, strict);
    MEMINF("after verify deps");

    pkgset_verify_conflicts(ps, strict);
    
    MEMINF("MEM before order");
    if ((flags & PSET_NOORDER) == 0)
        pkgset_order(ps, flags & PSET_VERIFY_ORDER);
    MEMINF("after setup[END]");

    if (n_array_size(ps->pkgs) > 10) { /* sanity check */
        int i = n_array_size(ps->pkgs) / 2;
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
        n = n_array_bsearch_idx_ex(ps->pkgs, pkg, (tn_fn_cmp)pkg_cmp_name);
        n_assert(n >= 0);
    }

    return ps->nerrors == 0;
}

static
int do_pkgset_add_package(struct pkgset *ps, struct pkg *pkg, int rt)
{
    int j;

    if (rt) {
        if (n_array_bsearch(ps->pkgs, pkg))
            return 0;

        n_array_push(ps->pkgs, pkg_link(pkg));
    }
    
    if (pkg->caps)
        for (j=0; j < n_array_size(pkg->caps); j++) {
            struct capreq *cap = n_array_nth(pkg->caps, j);
            capreq_idx_add(&ps->cap_idx, capreq_name(cap), pkg);
        }

    if (pkg->reqs)
        for (j=0; j < n_array_size(pkg->reqs); j++) {
            struct capreq *req = n_array_nth(pkg->reqs, j);
            if (capreq_is_rpmlib(req)) /* rpm caps are too expensive */
                continue;
            capreq_idx_add(&ps->req_idx, capreq_name(req), pkg);
        }
    
    if (pkg->cnfls)
        for (j=0; j < n_array_size(pkg->cnfls); j++) {
            struct capreq *cnfl = n_array_nth(pkg->cnfls, j);
            if (capreq_is_obsl(cnfl))
                capreq_idx_add(&ps->obs_idx, capreq_name(cnfl), pkg);
            else
                capreq_idx_add(&ps->cnfl_idx, capreq_name(cnfl), pkg);
        }
    
    pkgfl2fidx(pkg, ps->file_idx, rt);
    return 1;
}

int pkgset_add_package(struct pkgset *ps, struct pkg *pkg)
{
    if ((ps->flags & _PKGSET_INDEXES_INIT) == 0)
        pkgset_index(ps);
    
    return do_pkgset_add_package(ps, pkg, 1);
}

int pkgset_remove_package(struct pkgset *ps, struct pkg *pkg) 
{
    int i, j, nth;
    
    if ((nth = n_array_bsearch_idx(ps->pkgs, pkg)) == -1)
        return 0;
    pkg = n_array_nth(ps->pkgs, nth);

    if (pkg->caps)
        for (j=0; j < n_array_size(pkg->caps); j++) {
            struct capreq *cap = n_array_nth(pkg->caps, j);
            capreq_idx_remove(&ps->cap_idx, capreq_name(cap), pkg);
        }

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
    
    if (pkg->fl)
        for (i=0; i < n_tuple_size(pkg->fl); i++) {
            struct pkgfl_ent *flent = n_tuple_nth(pkg->fl, i);
            
            for (j=0; j < flent->items; j++)
                file_index_remove(ps->file_idx, flent->dirname,
                                  flent->files[j]->basename, pkg);
        }

    n_array_remove_nth(ps->pkgs, nth);
    return 1;
}


int pkgset_order(struct pkgset *ps, int verb) 
{
    int nloops;
                   
    if (verb)
        msgn(1, _("\nVerifying (pre)requirements..."));

    if (ps->ordered_pkgs != NULL)
        n_array_free(ps->ordered_pkgs);
    ps->ordered_pkgs = NULL;
    
    nloops = packages_order(ps->pkgs, &ps->ordered_pkgs, PKGORDER_INSTALL);
    
    if (nloops) {
        ps->nerrors += nloops;
		msgn(1, ngettext("%d prerequirement loop detected",
						 "%d prerequirement loops detected",
						 nloops), nloops);
		
    } else if (verb) {
        msgn(1, _("No loops -- OK"));
    }
        	
    
    if (verb && poldek_VERBOSE > 2) {
        int i;
            
        msg(2, "Installation order:\n");
        for (i=0; i < n_array_size(ps->ordered_pkgs); i++) {
            struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
            msg(2, "%d. %s\n", i, pkg->name);
        }
        msg(2, "\n");
    }
    
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

struct pkg *pkgset_lookup_1package(struct pkgset *ps, const char *name) 
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


static
tn_array *find_capreq(struct pkgset *ps, tn_array *pkgs,
                      enum pkgset_search_tag tag,
                      const char *name)
{
    const struct capreq_idx_ent *ent = NULL;
    
    switch (tag) {
        case PS_SEARCH_CAP:
            ent = capreq_idx_lookup(&ps->cap_idx, name);
            break;

        case PS_SEARCH_REQ:
            ent = capreq_idx_lookup(&ps->req_idx, name);
            break;
            
        case PS_SEARCH_OBSL:
            ent = capreq_idx_lookup(&ps->obs_idx, name);
            break;
            
        case PS_SEARCH_CNFL:
            ent = capreq_idx_lookup(&ps->cnfl_idx, name);
            break;

        default:
            n_assert(0);
            break;
    }
    
    
    if (ent && ent->items > 0) {
        int i;
        for (i=0; i < ent->items; i++)
            n_array_push(pkgs, pkg_link(ent->crent_pkgs[i]));
        
    }

    return pkgs;
}

tn_array *pkgset_search_reqdir(struct pkgset *ps, tn_array *pkgs,
                               const char *dir)
{
    tn_array *tmp = pkgs_array_new(32);
    int i, pkgs_passsed = 1;
    
        
    for (i=0; i < n_array_size(ps->pkgdirs); i++) {
        struct pkgdir *pkgdir = n_array_nth(ps->pkgdirs, i);
        
        if (pkgdir->dirindex == NULL)
            continue;

        pkgdir_dirindex_get(pkgdir->dirindex, tmp, dir);
    }

    if (pkgs == NULL) {
        pkgs = n_array_clone(tmp);
        pkgs_passsed = 0;
    }
    
    for (i=0; i < n_array_size(tmp); i++) {
        struct pkg *tmpkg, *pkg = n_array_nth(tmp, i);
        
        if (pkg_is_scored(pkg, (PKG_IGNORED | PKG_IGNORED_UNIQ)))
            continue;
        
        if ((tmpkg = n_array_bsearch(ps->pkgs, pkg)) && tmpkg == pkg)
            n_array_push(pkgs, pkg_link(pkg));
    }
    n_array_free(tmp);

    if (pkgs_passsed && n_array_size(pkgs) == 0)
        n_array_cfree(&pkgs);
    
    return pkgs;
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
                                /* no break => we lookup into files too */

        case PS_SEARCH_FILE:
            n_assert(value);
            if (*value != '/')
                break;
            else {
                struct pkg *buf[1024];
                int i, n;
                n = file_index_lookup(ps->file_idx, value, 0, buf, 1024);
                n_assert(n >= 0);
                
                if (n) {
                    for (i=0; i < n; i++)
                        n_array_push(pkgs, pkg_link(buf[i]));

                } else {
                    pkgset_search_reqdir(ps, pkgs, value);
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


void pkgset_report_fileconflicts(struct pkgset *ps, tn_array *pkgs)
{
    file_index_report_conflicts(ps->file_idx, pkgs);
}

int packages_verify_dependecies(tn_array *pkgs, struct pkgset *ps)
{
    int i, j, nerr = 0;
    
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        tn_array *errs;
        
        if ((errs = pkgset_get_unsatisfied_reqs(ps, pkg))) {
            for (j=0; j < n_array_size(errs); j++) {
                struct pkg_unreq *unreq = n_array_nth(errs, j);
                logn(LOGERR, _("%s: req %s %s"),
                     pkg_snprintf_s(pkg), unreq->req,
                     unreq->mismatch ? _("version mismatch") : _("not found"));
                nerr++;
            }
        }
    }
    
    if (nerr)
        msgn(0, _("%d unsatisfied dependencies found"), nerr);
    else
        msgn(0, _("No unsatisfied dependencies found"));
    return nerr == 0;
}
