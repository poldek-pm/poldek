/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef  POLDEK_POCLIDEK_H
#define  POLDEK_POCLIDEK_H

#include <trurl/narray.h>
#include <trurl/nbuf.h>
#ifndef POCLIDEK_ITSELF
# include <poldek/poldek.h>
#endif

#ifndef EXPORT
# define EXPORT extern
#endif

struct poclidek_ctx;
struct poldek_ts;

EXPORT struct poclidek_ctx *poclidek_new(struct poldek_ctx *ctx);
EXPORT void poclidek_free(struct poclidek_ctx *cctx);

EXPORT int poclidek_setup(struct poclidek_ctx *cctx);

#define POCLIDEK_LOAD_AVAILABLE       (1 << 0)
#define POCLIDEK_LOAD_INSTALLED       (1 << 1)

#define POCLIDEK_LOAD_ALL       ((1 << 0) | (1 << 1))
#define POCLIDEK_LOAD_RELOAD    (1  << 5)

EXPORT int poclidek_load_packages(struct poclidek_ctx *cctx, unsigned flags);

EXPORT int poclidek_exec(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                  int argc, const char **argv);

EXPORT int poclidek_execline(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                      const char *cmdline);

EXPORT struct poclidek_rcmd *poclidek_rcmd_new(struct poclidek_ctx *cctx,
                                        struct poldek_ts *ts);

EXPORT void poclidek_rcmd_free(struct poclidek_rcmd *rcmd);
EXPORT int poclidek_rcmd_exec(struct poclidek_rcmd *rcmd, int argc, const char **argv);
EXPORT int poclidek_rcmd_execline(struct poclidek_rcmd *rcmd, const char *cmdline);

EXPORT tn_array *poclidek_rcmd_get_packages(struct poclidek_rcmd *rcmd);
EXPORT tn_buf *poclidek_rcmd_get_buf(struct poclidek_rcmd *rcmd);
EXPORT const char *poclidek_rcmd_get_output(struct poclidek_rcmd *rcmd);

/* library internals */
#include "dent.h"
#include "cmd.h"
#include "op.h"

#endif
