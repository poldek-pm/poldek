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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <fnmatch.h>

#include <trurl/nmalloc.h>
#include <trurl/nassert.h>


#include "vfile/vfile.h"
#include "sigint/sigint.h"

#include "pkgdir/source.h"
#include "pkgset.h"
#include "pkgmisc.h"
#include "conf.h"
#include "log.h"
#include "misc.h"
#include "i18n.h"
#include "poldek.h"
#include "split.h"

int poldek_load_sources__internal(struct poldek_ctx *ctx, int load_dbdepdirs)
{
    struct pkgset *ps;
    struct poldek_ts *ts;
    unsigned ps_flags = 0;

    
    n_assert(ctx->ps == NULL);

    ts = ctx->ts;
    
    
    if ((ps = pkgset_new()) == NULL)
        return 0;
        
    if (load_dbdepdirs) {
        if (rpmdb_get_depdirs(ctx->ts->rootdir, ps->depdirs) >= 0)
            ps->flags |= PSET_DBDIRS_LOADED;
    }
        
    if (!pkgset_load(ps, 0, ctx->sources)) {
        logn(LOGWARN, _("no packages loaded"));
        //pkgset_free(ps);
        //ps = NULL;
    }
    mem_info(1, "MEM after load");

    if (ps == NULL)
        return 0;
    
    if (ctx->ts->getop(ctx->ts, POLDEK_OP_HOLD))
        packages_score(ps->pkgs, ctx->ts->hold_patterns, PKG_HELD);

    if (ctx->ts->getop(ctx->ts, POLDEK_OP_IGNORE))
        packages_score(ps->pkgs, ctx->ts->ign_patterns, PKG_IGNORED);
    
    ctx->pkgdirs = n_ref(ps->pkgdirs);

    if (ts->getop(ts, POLDEK_OP_UNIQN))
        ps_flags |= PSET_UNIQ_PKGNAME;

    if (ts->getop(ts, POLDEK_OP_VRFY_DEPS))
        ps_flags |= PSET_VERIFY_DEPS;

    if (ts->getop(ts, POLDEK_OP_VRFY_CNFLS))
        ps_flags |= PSET_VERIFY_CNFLS;

    if (ts->getop(ts, POLDEK_OP_VRFY_FILECNFLS))
        ps_flags |= PSET_VERIFY_FILECNFLS;
        
    pkgset_setup(ps, ps_flags);
    
    if (ctx->ts->prifile) 
        packages_set_priorities(ps->pkgs, ctx->ts->prifile);

    ctx->ps = ps;
    return 1;
}


tn_array *poldek_get_avail_packages(struct poldek_ctx *ctx)
{
    if (!poldek_load_sources(ctx))
        return NULL;

    return n_ref(ctx->ps->pkgs);
}

tn_array *poldek_get_avail_packages_bynvr(struct poldek_ctx *ctx) 
{
    if (!poldek_load_sources(ctx))
        return NULL;

    return pkgset_get_packages_bynvr(ctx->ps);
}
