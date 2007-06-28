/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/nlist.h>
#include <trurl/nmalloc.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>

#include <sigint/sigint.h>

#define ENABLE_TRACE 0
#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "pkgmisc.h"
#include "misc.h"
#include "pkgset-req.h"
#include "poldek.h"
#include "poldek_intern.h"
#include "pm/pm.h"
#include "install/install.h"

int process_pkg(const struct pkg *dbpkg, struct poldek_ts *ts,
                tn_hash *marked_h, int *nmarked)
{
    struct pkg *pkg = NULL, *tmpkg;
    char pkgkey[256];
    int i, cmprc;

    i = n_array_bsearch_idx_ex(ts->ctx->ps->pkgs, dbpkg, (tn_fn_cmp)pkg_cmp_name); 
    if (i < 0) {
        msgn(3, "%-32s not found in repository", pkg_id(dbpkg));
        return 1;
    }

    while (i < n_array_size(ts->ctx->ps->pkgs)) {
        pkg = n_array_nth(ts->ctx->ps->pkgs, i);
        
        if (!ts->getop(ts, POLDEK_OP_MULTILIB))
            break;
            
        if (pkg_is_kind_of(pkg, dbpkg))
            break;

        i++;
        pkg = NULL;
    }
    
    if (pkg == NULL) {
        msgn(3, "%-32s not found in repository", pkg_id(dbpkg));
        return 1;
    }
    
    cmprc = pkg_cmp_evr(pkg, dbpkg);
    if (poldek_VERBOSE > 1) {
        if (cmprc == 0) 
            msg(3, "%-32s up to date\n", pkg_id(dbpkg));
        
        else if (cmprc < 0)
            msg(3, "%-32s newer than repository one\n", pkg_id(dbpkg));
        
        else 
            msg(2, "%-32s -> %-30s\n", pkg_id(dbpkg), pkg_id(pkg));
    }

    if (ts->getop(ts, POLDEK_OP_MULTILIB))
        n_snprintf(pkgkey, 250, "%s.%s", dbpkg->name, pkg_arch(dbpkg));
    else
        n_snprintf(pkgkey, sizeof(pkgkey), "%s", dbpkg->name);

        
    if ((tmpkg = n_hash_get(marked_h, pkgkey))) {
        if (pkg_is_marked(ts->pms, tmpkg)) {
            logn(LOGWARN, _("%s: multiple instances installed, skipped"),
                 pkg_id(dbpkg));
            pkg_unmark(ts->pms, tmpkg);        /* display above once */
            (*nmarked)--;
        }
        return 0;
    }

    if (cmprc > 0) {
        if (pkg_is_scored(pkg, PKG_HELD) && ts->getop(ts, POLDEK_OP_HOLD)) {
            msgn(1, _("%s: skip held package"), pkg_id(pkg));
            
        } else {
            n_hash_insert(marked_h, pkgkey, pkg);
            pkg_hand_mark(ts->pms, pkg);
            (*nmarked)++;
        }
    }
    
    return 1;
}


int do_poldek_ts_upgrade_dist(struct poldek_ts *ts) 
{
    //map_s.avpkgs = ts->ctx->ps->pkgs;
    struct pkgdb_it       it;
    const struct pm_dbrec *dbrec;
    tn_hash               *marked_h;
    int                   nmarked = 0;
    
    marked_h = n_hash_new(1024, NULL);
    msgn(1, _("Looking up packages for upgrade..."));

    pkgdb_it_init(ts->db, &it, PMTAG_RECNO, NULL);
    while ((dbrec = pkgdb_it_get(&it))) {
        struct pkg t;
        char *arch;
        
        if (dbrec->hdr == NULL)
            continue;
        
        if (pm_dbrec_nevr(dbrec, &t.name, &t.epoch,
                          &t.ver, &t.rel, &arch, &t.color)) {
            struct pkg *pkg;
            
            pkg = pkg_new(t.name, t.epoch, t.ver, t.rel, arch, NULL);
            pkg->color = t.color;
                                      
            if (process_pkg(pkg, ts, marked_h, &nmarked) < 0) {
                pkg_free(pkg);
                break;
            }
            
            pkg_free(pkg);
        }
        
        if (sigint_reached()) {
            nmarked = 0;
            break;
        }
    }
    
    pkgdb_it_destroy(&it);
    n_hash_free(marked_h);

    if (nmarked == 0)
        msgn(1, _("Nothing to do"));

    return in_do_poldek_ts_install(ts);
}
