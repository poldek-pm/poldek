/* 
  Copyright (C) 2000, 2001 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <string.h>
#include <time.h>

#include <trurl/trurl.h>


#include "vfile/vopen3.h"
#include "sigint/sigint.h"
#include "misc.h"
#include "i18n.h"
#include "cli.h"
#include "cmd_pipe.h"

static int external(struct cmdctx *cmdctx);
static error_t parse_opt(int key, char *arg, struct argp_state *state);

struct poclidek_cmd command_external = {
    COMMAND_PIPEABLE | COMMAND_HIDDEN, 
    "!", N_("COMMAND"), N_("Execute external command"), 
    NULL, parse_opt, NULL, external,
    NULL, NULL, NULL, NULL, 0
};


static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdctx *cmdctx = state->input;

    if (key == ARGP_KEY_ARG) {
        if (cmdctx->_data == NULL)
            cmdctx->_data = n_array_new(4, free, NULL);
        n_array_push(cmdctx->_data, n_strdup(arg));
    }

    return 0;
}

static int grabfunc(const char *buf, void *cmdctx)
{
    return cmdctx_printf(cmdctx, "%s", buf);
}

static int feed_process(void *cmdctx_)
{
    struct cmdctx *cmdctx = cmdctx_;
    
    n_assert(cmdctx->pipe_left);
    cmd_pipe_writeout_fd(cmdctx->pipe_left, STDOUT_FILENO);
    return 0;
}
    

static int external(struct cmdctx *cmdctx) 
{
    struct vopen3_st st_process, st_feed, *st;
    int stflags = 0;
    char cmd[PATH_MAX], **argv;
    int i, rc;
    
    n_assert(cmdctx->_data);

    argv = alloca((n_array_size(cmdctx->_data) + 1) * sizeof(*argv));
    for (i=0; i < n_array_size(cmdctx->_data); i++) {
        argv[i] = n_array_nth(cmdctx->_data, i);
        DBGF("argv[%d] = %s\n", i, argv[i]);
    }
    argv[i] = NULL;

    if (!poldek_lookup_external_command(cmd, sizeof(cmd), argv[0])) {
        logn(LOGERR, _("%s: external command not found"), argv[0]);
        return 0;
    }
    
    
    vopen3_init(&st_process, cmd, argv);
    if (cmdctx->pipe_right) {
        stflags = VOPEN3_PIPESTDOUT;
        vopen3_set_grabfn(&st_process, grabfunc, cmdctx);
    }

    st = &st_process;
    if (cmdctx->pipe_left) {
        vopen3_init_fn(&st_feed, feed_process, cmdctx);
        vopen3_chain(&st_feed, &st_process);
        st = &st_feed;
    }
                     
    vopen3_exec(st, stflags);
    DBGF("process..\n");
    vopen3_process(st, 0);
    DBGF("close..\n");
    rc = vopen3_close(st) == 0;
    DBGF("END\n");
    return rc;
}

