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
    
    if ((ps = pkgset_new(ctx->inst->ps_flags)) == NULL)
        return 0;

    if (load_dbdepdirs) {
        if (rpmdb_get_depdirs(ctx->inst->rootdir, ps->depdirs) >= 0)
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

    
    if ((ctx->inst->flags & INSTS_NOHOLD) == 0) {
        packages_score(ps->pkgs, ctx->inst->hold_patterns, PKG_HELD);
        
        if (n_array_size(ctx->inst->hold_patterns) == 0) {
            n_array_free(ctx->inst->hold_patterns);
            ctx->inst->hold_patterns = NULL;
        }
    }

    if ((ctx->inst->flags & INSTS_NOIGNORE) == 0) {
        packages_score(ps->pkgs, ctx->inst->ign_patterns, PKG_IGNORED);
            n_array_free(ctx->inst->ign_patterns);
            ctx->inst->ign_patterns = NULL;
    }
    
    //exit(0);    
    pkgset_setup(ps, ctx->inst->ps_setup_flags);

    if (ctx->inst->prifile) 
        packages_set_priorities(ps->pkgs, ctx->inst->prifile);

    ctx->ps = ps;
    return 1;
}

int poldek_mark_usrset(struct poldek_ctx *ctx, struct usrpkgset *ups, int withdeps)
{
    return pkgset_mark_usrset(ctx->ps, ups, ctx->inst->flags, withdeps);
}
