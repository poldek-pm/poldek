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
#include "conf.h"
#include "log.h"
#include "misc.h"
#include "i18n.h"
#include "poldek.h"
#include "split.h"

int poldek_load_sources__internal(struct poldek_ctx *ctx, int load_dbdepdirs)
{
    struct pkgset *ps;

    n_assert(ctx->ps == NULL);
    
    if ((ps = pkgset_new(ctx->ps_flags)) == NULL)
        return 0;

    if (load_dbdepdirs) {
        if (rpmdb_get_depdirs(ctx->ts->rootdir, ps->depdirs) >= 0)
            ps->flags |= PSDBDIRS_LOADED;
    }

    if (!pkgset_load(ps, 0, ctx->sources)) {
        logn(LOGWARN, _("no packages loaded"));
        //pkgset_free(ps);
        //ps = NULL;
    }
    mem_info(1, "MEM after load");

    if (ps == NULL)
        return 0;
    
    
    if ((ctx->ts->flags & POLDEK_TS_NOHOLD) == 0)
        packages_score(ps->pkgs, ctx->ts->hold_patterns, PKG_HELD);

    if ((ctx->ts->flags & POLDEK_TS_NOIGNORE) == 0)
        packages_score(ps->pkgs, ctx->ts->ign_patterns, PKG_IGNORED);

    ctx->pkgdirs = n_ref(ps->pkgdirs);
    //exit(0);    
    pkgset_setup(ps, ctx->ps_setup_flags);
    
    if (ctx->ts->prifile) 
        packages_set_priorities(ps->pkgs, ctx->ts->prifile);

    ctx->ps = ps;
    //ctx->pkgs = n_ref(ps->pkgs);
    return 1;
}


tn_array *poldek_get_avpkgs(struct poldek_ctx *ctx)
{
    if (!poldek_load_sources(ctx))
        return NULL;

    return n_ref(ctx->ps->pkgs);
}

tn_array *poldek_get_avpkgs_bynvr(struct poldek_ctx *ctx) 
{
    if (!poldek_load_sources(ctx))
        return NULL;

    return n_ref(ctx->ps->pkgs_bynvr);
}
