/*
  $Id$
*/
/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@pld.org.pl>

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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <trurl/trurl.h>
#include <trurl/nmalloc.h>

#include "capreq.h"
#include "pkg.h"
#include "pkgset-req.h"         /* for struct reqpkg */
#include "pkgset.h"
#include "pkgmisc.h"

#include "i18n.h"
#include "log.h"

struct pkgmark_set {
    tn_hash *ht;
    tn_alloc *na;
};

struct pkg_mark {
    struct pkg *pkg;
    uint32_t flags;
};

static void pkg_mark_free(struct pkg_mark *m)
{
    pkg_free(m->pkg);
}


struct pkgmark_set *pkgmark_set_new(int size) 
{
    struct pkgmark_set *pmark;
    tn_alloc *na;
    
    na = n_alloc_new(8, TN_ALLOC_OBSTACK);
    pmark = na->na_malloc(na, sizeof(*na));
    pmark->ht = n_hash_new_na(na, size > 256 ? size : 256,
                              (tn_fn_free)pkg_mark_free);
    pmark->na = na;
    return pmark;
}

void pkgmark_set_free(struct pkgmark_set *pmark) 
{
    n_hash_free(pmark->ht); 
    n_alloc_free(pmark->na);
}

tn_array *pkgmark_get_packages(struct pkgmark_set *pmark, uint32_t flag)
{
    tn_array *pmarks, *pkgs;
    int i;

    
    if (n_hash_size(pmark->ht) == 0)
        return NULL;

    pmarks = n_hash_values(pmark->ht);
    pkgs = pkgs_array_new(n_array_size(pmarks));
    for (i=0; i < n_array_size(pmarks); i++) {
        struct pkg_mark *pkg_mark = n_array_nth(pmarks, i);
        if (pkg_mark->flags & flag)
            n_array_push(pkgs, pkg_link(pkg_mark->pkg));
    }
    
    if (n_array_size(pkgs) == 0) {
        n_array_free(pkgs);
        pkgs = NULL;
    }

    n_array_free(pmarks);
    return pkgs;
}


int pkgmark_set(struct pkgmark_set *pmark, struct pkg *pkg,
                int set, uint32_t flag)
{
    struct pkg_mark *pkg_mark;

    pkg_mark = n_hash_get(pmark->ht, pkg->nvr);
    if (pkg_mark == NULL) {
        if (!set)
            return 1;

        pkg_mark = pmark->na->na_malloc(pmark->na, sizeof(*pkg_mark));
        pkg_mark->pkg = pkg_link(pkg);
        pkg_mark->flags = 0;
        n_hash_insert(pmark->ht, pkg->nvr, pkg_mark);
    }
    
    if (set)
        pkg_mark->flags |= flag;
    else
        pkg_mark->flags &= ~flag;
    
    return 1;
}

int pkgmark_isset(struct pkgmark_set *pmark, struct pkg *pkg, uint32_t flag)
{
    struct pkg_mark *pkg_mark;

    if ((pkg_mark = n_hash_get(pmark->ht, pkg->nvr)))
        return pkg_mark->flags & flag;

    return 0;
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
        msgn_i(1, deep, _("%s marks %s (cap %s)"), pkg_snprintf_s(parent_pkg),
               pkg_snprintf_s0(pkg), capreq_snprintf_s(req));
        pkg_dep_mark(pms, pkg);
    }
    
    deep += 2;
    if (pkg->reqpkgs) {
        for (i=0; i<n_array_size(pkg->reqpkgs); i++) {
            struct reqpkg *rpkg = n_array_nth(pkg->reqpkgs, i);

            if (pkg_is_marked(pms, rpkg->pkg))
                continue;
            
            if (!pkg_is_dep_marked(pms, rpkg->pkg)) {
                int markit = 1;
                
                if (rpkg->flags & REQPKG_MULTI) {
                    struct reqpkg *eqpkg;
                    int n;

                    n = 1;
                    eqpkg = rpkg->adds[0];
                    while (eqpkg) {
                        if (pkg_is_marked(pms, eqpkg->pkg)) {
                            markit = 0;
                            break;
                        }
                        eqpkg = rpkg->adds[n++];
                    }
                }

                if (markit)
                    visit_mark_reqs(pkg, rpkg->req, rpkg->pkg, pms, deep);
            }
        }
    }
}

static
int depmark_packages(struct pkgmark_set *pms,
                     const tn_array *tomark, int withdeps)
{
    int i, nerr = 0;
    
    for (i=0; i < n_array_size(tomark); i++) {
        struct pkg *pkg = n_array_nth(tomark, i);
        pkg_hand_mark(pms, pkg);
    }
    
    if (withdeps) {
        for (i=0; i < n_array_size(tomark); i++) {
            struct pkg *pkg = n_array_nth(tomark, i);
    
            n_assert(pkg_is_hand_marked(pms, pkg));
            visit_mark_reqs(NULL, NULL, pkg, pms, 0);
        }
    }

    return nerr == 0;
}


int packages_mark(struct pkgmark_set *pms, const tn_array *pkgs, int withdeps)

{
    return depmark_packages(pms, pkgs, withdeps);
}



void pkgmark_massset(struct pkgmark_set *pmark, int set, uint32_t flag)
{
    tn_array *pmarks;
    int i;

    if (n_hash_size(pmark->ht) == 0)
        return;

    pmarks = n_hash_values(pmark->ht);
    for (i=0; i < n_array_size(pmarks); i++) {
        struct pkg_mark *pkg_mark = n_array_nth(pmarks, i);
        if (set)
            pkg_mark->flags |= flag;
        else
            pkg_mark->flags &= ~flag;
    } 

    n_array_free(pmarks);
}


int pkgmark_verify_set_conflicts(struct pkgmark_set *pms)
{
    tn_array *marked;
    int i, j, nerr = 0;
    
    marked = pkgmark_get_packages(pms, PKGMARK_MARK | PKGMARK_DEP);
    for (i=0; i < n_array_size(marked); i++) {
        struct pkg *pkg = n_array_nth(marked, i);

        n_assert(pkg_is_marked(pms, pkg));

        if (pkg->cnflpkgs == NULL)
            continue;

        for (j=0; j < n_array_size(pkg->cnflpkgs); j++) {
            struct reqpkg *cpkg = n_array_nth(pkg->cnflpkgs, j);
            if (pkg_is_marked(pms, cpkg->pkg)) {
                logn(LOGERR, _("%s: conflicts with %s"), pkg_snprintf_s(pkg),
                     pkg_snprintf_s0(cpkg->pkg));
                nerr++;
            }
        }
    }
    
    if (nerr)
        msgn(0, _("%d conflicts found"), nerr);
    n_array_free(marked);
    return nerr == 0;
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
        msgn(0, _("%d unsatisfied dependencies"), nerr);
    return nerr == 0;
}
