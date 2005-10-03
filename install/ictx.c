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

#include <trurl/trurl.h>
#include <sigint/sigint.h>

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

#include "ictx.h"

#include "dbpkgset.h"
#include "dbdep.h"


void install_ctx_init(struct install_ctx *ictx, struct poldek_ts *ts)
{
    ictx->avpkgs = ts->ctx->ps->pkgs;
    ictx->install_pkgs = n_array_new(128, NULL, (tn_fn_cmp)pkg_nvr_strcmp);
    ictx->db_deps = db_deps_new();
    ictx->uninst_set = dbpkg_set_new();
    ictx->orphan_dbpkgs = pkgs_array_new_ex(128, pkg_cmp_recno);

    ictx->strict = ts->getop(ts, POLDEK_OP_VRFYMERCY);
    ictx->ndberrs = ictx->ndep = ictx->ninstall = 0;
    ictx->nerr_dep = ictx->nerr_cnfl = ictx->nerr_dbcnfl = ictx->nerr_fatal = 0;
    ictx->ts = ts;
    ictx->ps = ts->ctx->ps;
    ictx->pkg_stack = n_array_new(32, NULL, NULL);
    ictx->dbpms = pkgmark_set_new(0, 0);
    ictx->unmetpms = pkgmark_set_new(0, 0);
    ictx->deppms = pkgmark_set_new(0, PKGMARK_SET_IDPTR);
}

void install_ctx_destroy(struct install_ctx *ictx)
{
    ictx->avpkgs = NULL;
    n_array_free(ictx->install_pkgs);
    
    n_hash_free(ictx->db_deps);
    dbpkg_set_free(ictx->uninst_set);

    n_array_free(ictx->orphan_dbpkgs);
    ictx->ts = NULL;
    ictx->ps = NULL;
    pkgmark_set_free(ictx->dbpms);
    pkgmark_set_free(ictx->unmetpms);
    pkgmark_set_free(ictx->deppms);
    memset(ictx, 0, sizeof(*ictx));
}

void install_ctx_reset(struct install_ctx *ictx)
{
    n_array_clean(ictx->install_pkgs);
    
    n_hash_clean(ictx->db_deps);
    dbpkg_set_free(ictx->uninst_set);
    ictx->uninst_set = dbpkg_set_new();
    n_array_clean(ictx->orphan_dbpkgs);

    pkgmark_set_free(ictx->dbpms);
    ictx->dbpms = pkgmark_set_new(0, 0);

    pkgmark_set_free(ictx->unmetpms);
    ictx->unmetpms = pkgmark_set_new(0, 0);
    
    pkgmark_set_free(ictx->deppms);
    ictx->deppms = pkgmark_set_new(0, PKGMARK_SET_IDPTR);
    
    ictx->ndberrs = ictx->ndep = ictx->ninstall = 0;
    ictx->nerr_dep = ictx->nerr_cnfl = ictx->nerr_dbcnfl = ictx->nerr_fatal = 0;
}
