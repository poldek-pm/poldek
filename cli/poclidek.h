/* $Id$ */
#ifndef  POCLIDEK_H
#define  POCLIDEK_H

#include <trurl/narray.h>
#include <trurl/nbuf.h>
#include "poldek.h"

struct poclidek_ctx;
struct poldek_ts;

struct poclidek_ctx *poclidek_new(struct poldek_ctx *ctx);
void poclidek_free(struct poclidek_ctx *cctx);
int poclidek_load_packages(struct poclidek_ctx *cctx);

int poclidek_exec(struct poclidek_ctx *cctx, struct poldek_ts *ts, 
                  int argc, const char **argv);

int poclidek_execline(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                      const char *cmdline);

struct poclidek_rcmd {
    struct poclidek_ctx *_cctx;
    struct poldek_ts *_ts;
    
    tn_array *rpkgs;
    tn_buf   *rbuf;
    int      rc;
};

struct poclidek_rcmd *poclidek_rcmd_new(struct poclidek_ctx *cctx,
                                        struct poldek_ts *ts);

void poclidek_rcmd_free(struct poclidek_rcmd *rcmd);
int poclidek_rcmd_exec(struct poclidek_rcmd *rcmd, int argc, const char **argv);
int poclidek_rcmd_execline(struct poclidek_rcmd *rcmd, const char *cmdline);

#endif
