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

#include <sys/param.h>          /* for PATH_MAX */
#include <trurl/trurl.h>

#include "sigint/sigint.h"
#include "i18n.h"
#include "cli.h"

static int cd(struct cmdctx *cmdctx);
static int pwd(struct cmdctx *cmdctx);
static error_t parse_opt(int key, char *arg, struct argp_state *state);


struct poclidek_cmd command_cd = {
    COMMAND_SELFARGS | COMMAND_EMPTYARGS | COMMAND_NOOPTS, 
    "cd", N_("[PATH]"), N_("Change current package directory"), 
    NULL, parse_opt, NULL, cd,
    NULL, NULL, NULL, NULL, NULL, 0, 0
};

struct poclidek_cmd command_pwd = {
    COMMAND_NOARGS | COMMAND_NOOPTS, 
    "pwd", NULL, N_("Print name of current directory"), 
    NULL, NULL, NULL, pwd,
    NULL, NULL, NULL, NULL, NULL, 0, 0
};


static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdctx *cmdctx = state->input;

    DBGF("cd.arg %s\n", arg);
    if (key == ARGP_KEY_ARG) {
        if (cmdctx->_data == NULL)
            cmdctx->_data = n_strdup(arg);
        else 
            argp_usage (state);
        cmdctx->rtflags |= CMDCTX_GOTARGS;
    }

    return 0;
}


static int cd(struct cmdctx *cmdctx) 
{
    const char *path;
    char path_buf[PATH_MAX];
    int rc;

    path = cmdctx->_data;

    poclidek_load_packages(cmdctx->cctx, POCLIDEK_LOAD_ALL);
    if (path == NULL) {
        if (cmdctx->cctx->homedir)
            path = cmdctx->cctx->homedir->name;
        else
            path = POCLIDEK_AVAILDIR;
    }
    
    if (!(rc = poclidek_chdir(cmdctx->cctx, path)) && path) {
        n_snprintf(path_buf, sizeof(path_buf), "/%s", path);
        rc = poclidek_chdir(cmdctx->cctx, path_buf);
    }

    if (!rc)
        logn(LOGERR, "%s: no such directory", path);
    
    //printf("pwd = %s\n", cmdctx->cctx->currdir->name);
    return rc;
}


static int pwd(struct cmdctx *cmdctx) 
{
    char path[PATH_MAX];
    cmdctx_printf(cmdctx, "%s\n", poclidek_pwd(cmdctx->cctx, path, sizeof(path)));
    return 1;
}


