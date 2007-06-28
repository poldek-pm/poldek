/* $Id$ */
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

#define OPT_GID_BASE 500
#define OPT_GID_OP_MAKEIDX    (200  + OPT_GID_BASE)
#define OPT_GID_OP_SOURCE     (400  + OPT_GID_BASE)
#define OPT_GID_OP_PACKAGES   (600  + OPT_GID_BASE)
#define OPT_GID_OP_INSTALL    (800  + OPT_GID_BASE)
#define OPT_GID_OP_UNINSTALL  (1000 + OPT_GID_BASE)
#define OPT_GID_OP_VERIFY     (1200 + OPT_GID_BASE)
#define OPT_GID_OP_SPLIT      (1400 + OPT_GID_BASE)
#define OPT_GID_OP_OTHER      (1600 + OPT_GID_BASE)

int poclidek_load_aliases(struct poclidek_ctx *cctx, const char *path);

void poclidek_apply_iinf(struct poclidek_ctx *cctx, struct poldek_ts *ts);

int poclidek_save_installedcache(struct poclidek_ctx *cctx,
                                 struct pkgdir *pkgdir);
int poclidek_load_installed(struct poclidek_ctx *cctx, int reload);


int poclidek_argv_is_help(int argc, const char **argv);

#endif 
