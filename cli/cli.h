/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef  POCLIDEK_CLI_H
#define  POCLIDEK_CLI_H

#include "poldek.h"
#include "pkg.h"
#include "log.h"
#include "poldek_term.h"

#define POCLIDEK_ITSELF

#include "poclidek.h"
#include "dent.h"
#include "cmd.h"

#ifndef EXPORT
# define EXPORT extern
#endif

#define OPT_GID_BASE 500
#define OPT_GID_OP_MAKEIDX    (200  + OPT_GID_BASE)
#define OPT_GID_OP_SOURCE     (400  + OPT_GID_BASE)
#define OPT_GID_OP_PACKAGES   (600  + OPT_GID_BASE)
#define OPT_GID_OP_INSTALL    (800  + OPT_GID_BASE)
#define OPT_GID_OP_UNINSTALL  (1000 + OPT_GID_BASE)
#define OPT_GID_OP_VERIFY     (1200 + OPT_GID_BASE)
#define OPT_GID_OP_SPLIT      (1400 + OPT_GID_BASE)
#define OPT_GID_OP_OTHER      (1600 + OPT_GID_BASE)

EXPORT int poclidek__load_aliases(struct poclidek_ctx *cctx);
EXPORT int poclidek__add_aliases(struct poclidek_ctx *cctx, tn_hash *htcnf);


EXPORT void poclidek_apply_iinf(struct poclidek_ctx *cctx, struct poldek_ts *ts);

EXPORT int poclidek_save_installedcache(struct poclidek_ctx *cctx,
                                 struct pkgdir *pkgdir);
EXPORT int poclidek__load_installed(struct poclidek_ctx *cctx, int reload);


EXPORT int poclidek_argv_is_help(int argc, const char **argv);

#endif
