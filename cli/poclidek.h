/* $Id$ */
#ifndef  POLDEK_POCLIDEK_H
#define  POLDEK_POCLIDEK_H

#include <trurl/narray.h>
#include <trurl/nbuf.h>
#ifndef POCLIDEK_ITSELF 
# include <poldek/poldek.h>
#endif

struct poclidek_ctx;
struct poldek_ts;

struct poclidek_ctx *poclidek_new(struct poldek_ctx *ctx);
void poclidek_free(struct poclidek_ctx *cctx);
int poclidek_load_packages(struct poclidek_ctx *cctx, int skip_installed);

int poclidek_exec(struct poclidek_ctx *cctx, struct poldek_ts *ts, 
                  int argc, const char **argv);

int poclidek_execline(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                      const char *cmdline);

struct poclidek_rcmd *poclidek_rcmd_new(struct poclidek_ctx *cctx,
                                        struct poldek_ts *ts);

void poclidek_rcmd_free(struct poclidek_rcmd *rcmd);
int poclidek_rcmd_exec(struct poclidek_rcmd *rcmd, int argc, const char **argv);
int poclidek_rcmd_execline(struct poclidek_rcmd *rcmd, const char *cmdline);

tn_array *poclidek_rcmd_get_packages(struct poclidek_rcmd *rcmd);
tn_buf *poclidek_rcmd_get_buf(struct poclidek_rcmd *rcmd);
const char *poclidek_rcmd_get_str(struct poclidek_rcmd *rcmd);

/* library internals */
#include "dent.h"
#include "cmd.h"
#include "op.h"

#endif
