/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/param.h>          /* for PATH_MAX */
#include <trurl/trurl.h>

#include "compiler.h"
#include "sigint/sigint.h"
#include "i18n.h"
#include "cli.h"

static int cd(struct cmdctx *cmdctx);
static int pwd(struct cmdctx *cmdctx);
static error_t parse_opt(int key, char *arg, struct argp_state *state);


struct poclidek_cmd command_cd = {
    COMMAND_SELFARGS | COMMAND_EMPTYARGS | COMMAND_NOOPTS | COMMAND_INTERACTIVE,
    "cd", N_("[PATH]"), N_("Change current package directory"),
    NULL, parse_opt, NULL, cd,
    NULL, NULL, NULL, NULL, NULL, 0, 0,
    NULL
};

struct poclidek_cmd command_pwd = {
    COMMAND_NOARGS | COMMAND_NOOPTS | COMMAND_INTERACTIVE,
    "pwd", NULL, N_("Print name of current directory"),
    NULL, NULL, NULL, pwd,
    NULL, NULL, NULL, NULL, NULL, 0, 0,
    NULL
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

    if (path == NULL) {
        n_assert(cmdctx->cctx->homedir);
        path = cmdctx->cctx->homedir->name;
    }

    if (!(rc = poclidek_chdir(cmdctx->cctx, path)) && path) {
        n_snprintf(path_buf, sizeof(path_buf), "/%s", path);
        rc = poclidek_chdir(cmdctx->cctx, path_buf);
    }

    if (!rc)
        logn(LOGERR, "%s: no such directory", path);

    n_cfree(&cmdctx->_data);

    //printf("pwd = %s\n", cmdctx->cctx->currdir->name);
    return rc;
}


static int pwd(struct cmdctx *cmdctx)
{
    cmdctx_printf(cmdctx, "%s\n", poclidek_pwd(cmdctx->cctx));
    return 1;
}
