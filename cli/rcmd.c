/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

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

#include <trurl/trurl.h>

#include "log.h"
#include "cli.h"
#include "cmd_pipe.h"

int do_poclidek_execline(struct poclidek_ctx *cctx, struct poldek_ts *ts,
                         const char *cmdline, struct cmd_pipe *cmd_pipe);

int do_poclidek_exec(struct poclidek_ctx *cctx, struct poldek_ts *ts, int argc,
                     const char **argv, struct cmd_pipe *pipe);

struct poclidek_rcmd {
    unsigned            _flags;
    struct poclidek_ctx *_cctx;
    struct poldek_ts    *_ts;
    
    tn_array *rpkgs;
    tn_buf   *rbuf;
    int      rc;
};


#define POCLIDEK_RCMD_EXECUTED (1 << 0)

struct poclidek_rcmd *poclidek_rcmd_new(struct poclidek_ctx *cctx,
                                        struct poldek_ts *ts)
{
    struct poclidek_rcmd *rcmd = n_malloc(sizeof(*rcmd));
    rcmd->_flags = 0;
    rcmd->_cctx = cctx;
    rcmd->_ts = ts;
    rcmd->rpkgs = NULL;
    rcmd->rbuf = NULL;
    rcmd->rc = -1;
    return rcmd;
}

void poclidek_rcmd_free(struct poclidek_rcmd *rcmd)
{
    if (rcmd->rpkgs)
        n_array_free(rcmd->rpkgs);

    if (rcmd->rbuf)
        n_buf_free(rcmd->rbuf);

    memset(rcmd, 0, sizeof(*rcmd));
    free(rcmd);
}


int poclidek_rcmd_exec(struct poclidek_rcmd *rcmd, int argc, const char **argv)
{
    struct cmd_pipe *pipe = cmd_pipe_new();
    rcmd->rc = do_poclidek_exec(rcmd->_cctx, rcmd->_ts, argc, argv, pipe);
    rcmd->_flags |= POCLIDEK_RCMD_EXECUTED;
    rcmd->rpkgs = n_ref(pipe->pkgs);
    rcmd->rbuf = n_ref(pipe->nbuf);
    cmd_pipe_free(pipe);
    return rcmd->rc;
}

int poclidek_rcmd_execline(struct poclidek_rcmd *rcmd, const char *cmdline)
{
    struct cmd_pipe *pipe = cmd_pipe_new();
    rcmd->rc = do_poclidek_execline(rcmd->_cctx, rcmd->_ts, cmdline, pipe);
    rcmd->_flags |= POCLIDEK_RCMD_EXECUTED;
    rcmd->rpkgs = n_ref(pipe->pkgs);
    rcmd->rbuf = n_ref(pipe->nbuf);
    cmd_pipe_free(pipe);
    return rcmd->rc;
}

tn_array *poclidek_rcmd_get_packages(struct poclidek_rcmd *rcmd)
{
    n_assert(rcmd->_flags & POCLIDEK_RCMD_EXECUTED);
    return n_ref(rcmd->rpkgs);
}

tn_buf *poclidek_rcmd_get_buf(struct poclidek_rcmd *rcmd) 
{
    n_assert(rcmd->_flags & POCLIDEK_RCMD_EXECUTED);
    return n_ref(rcmd->rbuf);
}

const char *poclidek_rcmd_get_output(struct poclidek_rcmd *rcmd) 
{
    n_assert(rcmd->_flags & POCLIDEK_RCMD_EXECUTED);
    return n_buf_ptr(rcmd->rbuf);
}
