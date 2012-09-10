/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "ictx.h"

static void mark_message(int indent, const struct i3pkg *i3pkg)
{
    if (i3pkg->byflag == I3PKGBY_GREEDY) {
        
        if (pkg_cmp_name(i3pkg->pkg, i3pkg->bypkg) != 0) {
            msgn_i(1, indent, _("greedy upgrade %s to %s (unresolved %s)"),
                   pkg_id(i3pkg->bypkg), pkg_id(i3pkg->pkg),
                   capreq_stra(i3pkg->byreq));
        } else {
            msgn_i(1, indent, _("greedy upgrade %s to %s-%s%s%s (unresolved %s)"),
                   pkg_id(i3pkg->bypkg), i3pkg->pkg->ver, i3pkg->pkg->rel,
                   poldek_conf_MULTILIB ? "." : "",
                   poldek_conf_MULTILIB ? pkg_arch(i3pkg->pkg) : "",
                   capreq_stra(i3pkg->byreq));
        }
        
    } else {
        const char *r = capreq_is_cnfl(i3pkg->byreq) ? _("cnfl") : _("cap");
        const char *prefix = "";

        n_assert(i3pkg->byflag == I3PKGBY_REQ || i3pkg->byflag == I3PKGBY_ORPHAN);
    
        if (i3pkg->byflag == I3PKGBY_ORPHAN)
            prefix = _("orphaned ");
            
        msgn_i(1, indent, _("%s%s marks %s (%s %s)"), prefix,
               pkg_id(i3pkg->bypkg), pkg_id(i3pkg->pkg), r,
               capreq_stra(i3pkg->byreq));
    }
}

static int inc_indent(int indent)
{
    if (indent < 0)
        indent = 0;
    else
        indent++;
    return indent;
}

/* unmark package and forget its errors */
static void rollback_package(int indent, struct i3ctx *ictx, struct i3pkg *i3pkg)
{
    int i;

    if (i3_is_hand_marked(ictx, i3pkg->pkg))
        return;

    tracef(indent, "- rollbacking %s", pkg_id(i3pkg->pkg));
    iset_remove(ictx->inset, i3pkg->pkg);
    i3_forget_error(ictx, i3pkg->pkg);

    if (i3pkg->obsoletedby) {
        for (i=0; i < n_array_size(i3pkg->obsoletedby); i++) {
            struct pkg *pkg = n_array_nth(i3pkg->obsoletedby, i);
            
            trace(indent, " - unmark obsoleted %s", pkg_id(pkg));
            iset_remove(ictx->unset, pkg);
            i3_forget_error(ictx, pkg);
        }
    }

    /* this package may be used again and we have to process it (do not
     * stop on the first condition in i3_process_package()) to generate
     * new ->obsoletedby as we removed them here. */    
    pkg_clr_mf(ictx->processed, i3pkg->pkg, PKGMARK_GRAY);
    
    if (i3pkg->markedby) {
        indent = inc_indent(indent);
        
        for (i=0; i < n_array_size(i3pkg->markedby); i++)
            rollback_package(indent + 2, ictx, n_array_nth(i3pkg->markedby, i));
    }
}
    
static int do_process_package(int indent, struct i3ctx *ictx,
                              struct i3pkg *i3pkg, unsigned markflag)
{
    int rc = 1;
    
    trace(indent, "DOPROCESS %s as NEW", pkg_id(i3pkg->pkg));
    
    n_assert(!pkg_isset_mf(ictx->processed, i3pkg->pkg, PKGMARK_GRAY));
    pkg_set_mf(ictx->processed, i3pkg->pkg, PKGMARK_GRAY);

    if (markflag && !i3_mark_package(ictx, i3pkg->pkg, markflag))
        return 0;
    
    if (n_array_size(ictx->i3pkg_stack)) {
        struct i3pkg *marker = n_array_pop(ictx->i3pkg_stack);
        
        n_array_push(marker->markedby, i3pkg); /* i3pkg_link */
        n_array_push(ictx->i3pkg_stack, marker);
    }
    n_array_push(ictx->i3pkg_stack, i3pkg);
    
    indent = inc_indent(indent);
    i3_process_pkg_obsoletes(indent, ictx, i3pkg);
    
    if (i3pkg->pkg->reqs && !i3_process_pkg_requirements(indent, ictx, i3pkg)) {
        pkg_set_mf(ictx->processed, i3pkg->pkg, PKGMARK_BLACK);

        if ((i3pkg->flags & I3PKG_BACKTRACKABLE)) {
            trace(indent + 2, "backtracking (%s)", pkg_id(i3pkg->pkg));
            rollback_package(indent + 3, ictx, i3pkg);
            rc = -1;            /*  */
            goto l_end;
        }
    }

    i3_process_pkg_conflicts(indent, ictx, i3pkg);

l_end:
    tracef(indent, "END PROCESSING %s as NEW", pkg_id(i3pkg->pkg));
    n_array_pop(ictx->i3pkg_stack);

    return rc;
}

int i3_install_package(struct i3ctx *ictx, struct pkg *pkg)
{
    struct i3pkg *i3pkg = i3pkg_new(pkg, 0, NULL, NULL, I3PKGBY_HAND);

    i3_return_zero_if_stoppped(ictx);

    if (pkg_isset_mf(ictx->processed, pkg, PKGMARK_GRAY))
        return 1;

    trace(-1, "INSTALLING %s", pkg_id(pkg));
    
    n_assert(i3_is_hand_marked(ictx, pkg));
    return do_process_package(-1, ictx, i3pkg, 0) == 1;
}

int i3_process_package(int indent, struct i3ctx *ictx, struct i3pkg *i3pkg)
{
    unsigned   markflag = PKGMARK_DEP;
    struct pkg *pkg = i3pkg->pkg;
    int rc;

    i3_return_zero_if_stoppped(ictx);

    if (pkg_isset_mf(ictx->processed, pkg, PKGMARK_GRAY)) {
        tracef(indent, "DONOT PROCESSING %s as NEW", pkg_id(pkg));
        return 1;
    }
    
    trace(indent, "PROCESS %s as NEW", pkg_id(pkg));
    n_assert(!pkg_isset_mf(ictx->processed, pkg, PKGMARK_GRAY));
    
    // packages marked by hand but triggered by dependencies earlier
    if (i3_is_marked(ictx, pkg)) {
        markflag = 0;
        indent = -1;
        
    } else if (pkg_is_marked_i(ictx->ts->pms, pkg)) {
        markflag = PKGMARK_MARK;
        indent = -1;
    }
    
    if (markflag == PKGMARK_DEP)
        mark_message(indent, i3pkg);

    rc = do_process_package(indent, ictx, i3pkg, markflag);
    if (rc == -1) {
        /* XXX: yep, have inconsistency here, package is marked here, but
           in case of backtracking it's unmarked in rollback_package();
           TOFIX */
        n_assert(!i3_is_marked(ictx, pkg));
    }
    
    return rc;
}

int i3_process_orphan(int indent, struct i3ctx *ictx, struct orphan *o) 
{
    i3_return_zero_if_stoppped(ictx);

    indent += 2;
    trace(indent, "PROCESS %s as ORPHAN", pkg_id(o->pkg));
    n_assert(o->reqs);

    i3_process_orphan_requirements(indent, ictx, o->pkg, o->reqs);

    tracef(indent, "END PROCESSING %s as ORPHAN\n", pkg_id(o->pkg));
    return 1;
}
