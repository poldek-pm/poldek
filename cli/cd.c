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

#include "sigint/sigint.h"
#include "i18n.h"
#include "misc.h"
#include "cli.h"

static int cd(struct cmdarg *cmdarg);
static int pwd(struct cmdarg *cmdarg);
static error_t parse_opt(int key, char *arg, struct argp_state *state);


static struct argp_option options[] = {};

struct poclidek_cmd command_cd = {
    COMMAND_EMPTYARGS | COMMAND_NOOPTS, 
    "cd", N_("PATH"), N_("Change current package directory"), 
    options, parse_opt, NULL, cd,
    NULL, NULL, NULL, NULL
};

struct poclidek_cmd command_pwd = {
    COMMAND_NOARGS | COMMAND_NOOPTS, 
    "pwd", NULL, N_("Print name of current directory"), 
    NULL, NULL, NULL, pwd,
    NULL, NULL, NULL, NULL
};


static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdarg *cmdarg = state->input;

    
    if (key == ARGP_KEY_ARG) {
        if (cmdarg->d == NULL)
            cmdarg->d = n_strdup(arg);
        else 
            argp_usage (state);
    }

    return 0;
}


static int cd(struct cmdarg *cmdarg) 
{
    char *path, path_buf[PATH_MAX];
    int rc;

    path = cmdarg->d;
    if (path == NULL)
        path = cmdarg->cctx->homedir->name;
    
    if (!(rc = poclidek_chdir(cmdarg->cctx, path))) {
        n_snprintf(path_buf, sizeof(path_buf), "/%s", path);
        rc = poclidek_chdir(cmdarg->cctx, path_buf);
    }

    if (!rc)
        logn(LOGERR, "%s: no such directory", path);

    //printf("pwd = %s\n", cmdarg->cctx->currdir->name);
    return rc;
}


static int pwd(struct cmdarg *cmdarg) 
{
    char path[PATH_MAX];
    printf("pwd = %s\n", cmdarg->cctx->currdir->name);
    printf("%s\n", poclidek_dent_dirpath(path, sizeof(path),
                                         cmdarg->cctx->currdir));
    return 1;
}

