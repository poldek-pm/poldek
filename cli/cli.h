/* $Id$ */
#ifndef  POCLIDEK_CLI_H
#define  POCLIDEK_CLI_H

#include <argp.h>
#include <trurl/narray.h>
#include "poldek.h"
#include "pkg.h"
#include "log.h"
#include "poldek_term.h"

#define POCLIDEK_ITSELF 
#include "poclidek.h"
#include "dent.h"
#include "cmd.h"

int poclidek_load_aliases(struct poclidek_ctx *cctx, const char *path);

struct poldek_iinf;
void poclidek_apply_iinf(struct poclidek_ctx *cctx, struct poldek_iinf *iinf);

int poclidek_save_installedcache(struct poclidek_ctx *cctx,
                                 struct pkgdir *pkgdir);
int poclidek_load_installed(struct poclidek_ctx *cctx, int reload);

#endif 
