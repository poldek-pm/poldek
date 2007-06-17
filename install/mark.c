/*
  Copyright (C) 2000 - 2005 Pawel A. Gajda <mis@k2.net.pl>

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
int in_is_other_version_marked(struct install_ctx *ictx, struct pkg *pkg,
                               struct capreq *req)
{
    int i;

    n_array_sort(ictx->avpkgs);
    i = n_array_bsearch_idx_ex(ictx->avpkgs, pkg, (tn_fn_cmp)pkg_cmp_name); 
    if (i < 0)
        return 0;

    DBGF("%s, found (req is %s)\n", pkg_id(pkg), req ? capreq_snprintf_s(req) : "null");
    for (; i < n_array_size(ictx->avpkgs); i++) {
        struct pkg *p = n_array_nth(ictx->avpkgs, i);

        if (pkg_cmp_name(p, pkg) != 0)
            break;
        
        if (p != pkg && pkg_is_marked(ictx->ts->pms, p)) {
            if (req == NULL || pkg_satisfies_req(p, req, 0)) {
                DBGF("%s -> YES, %s%s%s\n", pkg_id(pkg), pkg_id(p),
                     req ? " and satisfies " : "",
                     req ? capreq_snprintf_s(req) : "null");
                return 1;
            }
        }
    }
    DBGF("NO\n");
    return 0;
}


int in_is_marked_for_removal(struct install_ctx *ictx, struct pkg *pkg)
{
    /* XXX: todo separate PTR-pms for removed packages */
    if (pkg_is_marked(ictx->ts->pms, pkg)) 
        return 0;
    
    if (pkg_is_rm_marked(ictx->ts->pms, pkg))
        return 1;
    
    if (dbpkg_set_has_pkg(ictx->uninst_set, pkg)) {
        pkg_rm_mark(ictx->ts->pms, pkg);
        return 1;
    }
    return 0;
}

int in_is_marked_for_removal_by_req(struct install_ctx *ictx,
                                    struct pkg *pkg, const struct capreq *req)
{
    const struct pkg *ppkg;
    int rc;

    if (pkg_is_rm_marked(ictx->ts->pms, pkg))
        return 1;
    
    rc = ((ppkg = dbpkg_set_provides(ictx->uninst_set, req)) &&
           pkg_cmp_name_evr(ppkg, pkg) == 0);
    
    if (rc)
        pkg_rm_mark(ictx->ts->pms, pkg);
    
    return rc;
}


static int do_mark_package(struct install_ctx *ictx, struct pkg *pkg,
                           unsigned mark)
{
    int rc;
    
#if 0
    static struct pkg *rpm = NULL;

    if (strcmp(pkg->name, "rpm") == 0 && strcmp(pkg->ver, "4.1") == 0) {
        rpm = pkg;
    }
    if (rpm)
        log(LOGNOTICE, "DUPA %s(%p): %d\n", pkg_id(rpm),
            rpm, pkg_is_marked(rpm));
#endif    
    
    n_assert(pkg_is_marked(ictx->ts->pms, pkg) == 0);
    rc = in_is_pkg_installable(ictx, pkg, pkg_is_marked_i(ictx->ts->pms, pkg));
    if (rc <= 0) {
        ictx->nerr_fatal++; 
        return 0;
    }
    
    DBGF("%s, is_installable = %d\n", pkg_id(pkg), rc);
    pkg_unmark_i(ictx->ts->pms, pkg);

    n_assert(!pkg_has_unmetdeps(ictx->unmetpms, pkg));

    if (rc > 0) {
        if (mark == PKGMARK_MARK) {
            pkg_hand_mark(ictx->ts->pms, pkg);
            
        } else {
            pkg_dep_mark(ictx->ts->pms, pkg);
            ictx->ndep++;
        }
        n_array_push(ictx->install_pkgs, pkg);
    }

    return rc >= 0;
}

int in_mark_package(struct install_ctx *ictx, struct pkg *pkg) 
{
    return do_mark_package(ictx, pkg, PKGMARK_MARK);
}

static
void message_depmark(int indent, const struct pkg *pkg, 
                     const struct pkg *marker, const struct capreq *marker_req,
                     int process_as)
{
    const char *reqstr = _("cap");
    const char *marker_prefix = "";

    
    if (process_as == PROCESS_AS_ORPHAN)
        marker_prefix = _("orphaned ");

    if (capreq_is_cnfl(marker_req))
        reqstr = _("cnfl");
            
    msgn_i(1, indent, _("%s%s marks %s (%s %s)"), marker_prefix, 
          pkg_id(marker), pkg_id(pkg),
          reqstr, capreq_snprintf_s(marker_req));
}

static
int do_dep_mark_package(struct install_ctx *ictx, struct pkg *pkg,
                        struct pkg *bypkg, struct capreq *byreq)
{
    if (pkg_has_unmetdeps(ictx->unmetpms, pkg)) {
        logn(LOGERR, _("%s: skip follow %s cause it's dependency errors"),
             pkg_id(bypkg), pkg_id(pkg));
        
        pkg_set_unmetdeps(ictx->unmetpms, bypkg);
        ictx->nerr_dep++;
        return 0;
    }

    if (in_is_marked_for_removal_by_req(ictx, pkg, byreq)) {
        logn(LOGERR, _("%s: dependency loop - "
                       "package already marked for removal"), pkg_id(pkg));
        ictx->nerr_fatal++; 
        return 0;
    }
    
    return do_mark_package(ictx, pkg, PKGMARK_DEP);
}

int in_dep_mark_package(int indent, struct install_ctx *ictx, struct pkg *pkg,
                        struct pkg *bypkg, struct capreq *byreq,
                        int process_as)
{
    message_depmark(indent, pkg, bypkg, byreq, process_as);
    return do_dep_mark_package(ictx, pkg, bypkg, byreq);
}


int in_greedy_mark_package(int indent, struct install_ctx *ictx,
                           struct pkg *pkg, struct pkg *oldpkg,
                           struct capreq *unresolved_req)
{
    
    n_assert(!pkg_is_marked(ictx->ts->pms, pkg));
    
    if (pkg_cmp_evr(pkg, oldpkg) <= 0)
        return 0;
    
    msgn_i(1, indent, _("greedy upgrade %s to %s-%s%s%s (unresolved %s)"),
           pkg_id(oldpkg), pkg->ver, pkg->rel,
           poldek_conf_MULTILIB ? "." : "",
           poldek_conf_MULTILIB ? pkg_arch(pkg) : "",
           capreq_snprintf_s(unresolved_req));

    return do_dep_mark_package(ictx, pkg, NULL, unresolved_req);
    //DUPA process_pkg_deps(indent, ictx, pkg, PROCESS_AS_NEW);
    //return 1;
}

int in_mark_namegroup(struct install_ctx *ictx,
                      struct pkg *pkg, tn_array *pkgs)
{
    struct pkg tmpkg;
    int n, i, len, nmarked = 0;
    char *p, prefix[512];


    n_array_sort(pkgs);
    
    len = n_snprintf(prefix, sizeof(prefix), "%s", pkg->name);
    if ((p = strchr(prefix, '-')))
        *p = '\0';
    
    tmpkg.name = prefix;

    //*p = '-';
    n = n_array_bsearch_idx_ex(pkgs, &tmpkg, (tn_fn_cmp)pkg_ncmp_name);
    
    
    //if (n < 0 && p) {
    //    n = n_array_bsearch_idx_ex(pkgs, &tmpkg, (tn_fn_cmp)pkg_cmp_name);
    // }

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
        
        if (pkg->pkgdir != p->pkgdir)
            continue;

        if (!pkg_is_marked(ictx->ts->pms, p) && in_mark_package(ictx, p))
            nmarked++;
    }
    
    return nmarked;
}


