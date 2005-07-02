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

extern
int do_poldek_ts_install(struct poldek_ts *ts, struct poldek_iinf *iinf);

struct map_s {
    tn_array *avpkgs;
    tn_hash *marked_h;
    struct poldek_ts *ts;
    int nmarked;
};

static
int mapfn_mark_newer_pkg(const char *n, uint32_t e,
                         const char *v, const char *r, void *_map_s) 
{
    struct map_s *map_s = _map_s;
    struct pkg  *pkg, tmpkg;
    int i, cmprc;

    memset(&tmpkg, 0, sizeof(tmpkg));
    tmpkg.name = (char*)n;
    tmpkg.epoch = e;
    tmpkg.ver = (char*)v;
    tmpkg.rel = (char*)r;
    
    i = n_array_bsearch_idx_ex(map_s->avpkgs, &tmpkg, (tn_fn_cmp)pkg_cmp_name); 
    if (i < 0) {
        msg(3, "%-32s not found in repository\n", pkg_snprintf_s(&tmpkg));
        return;
    }
    
    pkg = n_array_nth(map_s->avpkgs, i);
    cmprc = pkg_cmp_evr(pkg, &tmpkg);
    if (poldek_VERBOSE) {
        if (cmprc == 0) 
            msg(3, "%-32s up to date\n", pkg_snprintf_s(&tmpkg));
        
        else if (cmprc < 0)
            msg(3, "%-32s newer than repository one\n", pkg_snprintf_s(&tmpkg));
        
        else 
            msg(2, "%-32s -> %-30s\n", pkg_snprintf_s(&tmpkg),
                pkg_id(pkg));
    }

    if ((pkg = n_hash_get(map_s->marked_h, tmpkg.name))) {
        if (pkg_is_marked(map_s->ts->pms, pkg)) {
            logn(LOGWARN, _("%s: multiple instances installed, skipped"),
                 tmpkg.name);
            pkg_unmark(map_s->ts->pms, pkg);        /* display above once */
            map_s->nmarked--;
        }
        return 0;
    }

    pkg = n_array_nth(map_s->avpkgs, i);
    if (cmprc > 0) {
        if (pkg_is_scored(pkg, PKG_HELD) &&
            map_s->ts->getop(map_s->ts, POLDEK_OP_HOLD)) {
            msgn(1, _("%s: skip held package"), pkg_id(pkg));
            
        } else {
            n_hash_insert(map_s->marked_h, tmpkg.name, pkg);
            pkg_hand_mark(map_s->ts->pms, pkg);
            map_s->nmarked++;
        }
    }
    return 1;
}


int do_poldek_ts_upgrade_dist(struct poldek_ts *ts) 
{
    struct map_s map_s;

    map_s.ts = ts;
    map_s.avpkgs = ts->ctx->ps->pkgs;
    map_s.marked_h = n_hash_new(512, NULL);
    map_s.nmarked = 0;
    
    msgn(1, _("Looking up packages for upgrade..."));
    pkgdb_map_nevr(ts->db, mapfn_mark_newer_pkg, &map_s);
    n_hash_free(map_s.marked_h);
    
#if 0
    if (upg.ndberrs) {
        logn(LOGERR, _("There are database errors (?), give up"));
        destroy_upgrade_s(&upg);
        return 0;
    }
#endif
    
    if (sigint_reached()) 
        return 0;

    else if (map_s.nmarked == 0)
        msgn(1, _("Nothing to do"));
    
    return do_poldek_ts_install(ts, NULL);
}
