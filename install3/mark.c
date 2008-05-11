/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

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

#include "ictx.h"

/* is there marked instance of pkg which satisfies req? */
int i3_is_other_version_marked(struct i3ctx *ictx, struct pkg *pkg,
                                struct capreq *req)
{
    int i;
    tn_array *avpkgs = ictx->ps->pkgs;

    n_array_sort(avpkgs);
    i = n_array_bsearch_idx_ex(avpkgs, pkg, (tn_fn_cmp)pkg_cmp_name); 
    if (i < 0)
        return 0;

    DBGF("%s, found (req is %s)\n", pkg_id(pkg), req ? capreq_stra(req):"null");
    for (; i < n_array_size(avpkgs); i++) {
        struct pkg *p = n_array_nth(avpkgs, i);

        if (pkg_cmp_name(p, pkg) != 0)
            break;
        
        if (p != pkg && i3_is_marked(ictx, p)) {
            if (req == NULL || pkg_satisfies_req(p, req, 0)) {
                DBGF("%s -> YES, %s%s%s\n", pkg_id(pkg), pkg_id(p),
                     req ? " and satisfies " : "",
                     req ? capreq_stra(req) : "null");
                return 1;
            }
        }
    }
    DBGF("NO\n");
    return 0;
}


int i3_unmark_package(struct i3ctx *ictx, struct pkg *pkg) 
{
    if (iset_ismarkedf(ictx->inset, pkg, PKGMARK_INTERNAL))
        pkg_mark_i(ictx->ts->pms, pkg);
    
    if (!iset_remove(ictx->inset, pkg))
        n_assert(0);
    
    return 1;
}

int i3_mark_package(struct i3ctx *ictx, struct pkg *pkg, unsigned mark)
{
    int rc;

    n_assert(!i3_is_marked(ictx, pkg));
    
    rc = i3_is_pkg_installable(ictx->ts, pkg,
                               pkg_is_marked_i(ictx->ts->pms, pkg));
    if (rc <= 0) {
        i3_stop_processing(ictx, 1);
        return 0;
    }
    
    DBGF("%s, is_installable = %d\n", pkg_id(pkg), rc);

    n_assert(rc > 0);
    
    if (pkg_is_marked_i(ictx->ts->pms, pkg)) {
        pkg_unmark_i(ictx->ts->pms, pkg);
        mark |= PKGMARK_INTERNAL; /* keep INTERNAL in inset */
    }
    
    iset_add(ictx->inset, pkg, mark);
    return 1;
}


int i3_mark_namegroup(struct i3ctx *ictx, struct pkg *pkg, tn_array *pkgs)
{
    struct pkg tmpkg;
    int n, i, len, nmarked = 0;
    char *p, prefix[512];


    n_array_sort(pkgs);
    
    len = n_snprintf(prefix, sizeof(prefix), "%s", pkg->name);
    if ((p = strchr(prefix, '-')))
        *p = '\0';
    
    tmpkg.name = prefix;

    n = n_array_bsearch_idx_ex(pkgs, &tmpkg, (tn_fn_cmp)pkg_ncmp_name);
    if (n < 0)
        return 0;
    
    len = strlen(prefix);
    
    for (i = n; i < n_array_size(pkgs); i++) {
        struct pkg *p = n_array_nth(pkgs, i);
        int pkg_name_len;

        if ((pkg_name_len = strlen(pkg->name)) < len)
            break;
        
        if (strncmp(p->name, prefix, len) != 0) 
            break;

        if (!pkg_is_marked_i(ictx->ts->pms, p)) 
            continue;
        
        if (!i3_is_marked(ictx, p)) {
            DBGF("mark %s\n", pkg_id(p));
            i3_mark_package(ictx, p, PKGMARK_MARK);
            nmarked++;
        }
    }
    
    return nmarked;
}


