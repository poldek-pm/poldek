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
#include "i18n.h"
#include "cli.h"

static int external(struct cmdarg *cmdarg);
static error_t parse_opt(int key, char *arg, struct argp_state *state);



static struct argp_option options[] = {};

struct poclidek_cmd command_external = {
    COMMAND_PIPEABLE_LEFT | COMMAND_PIPEABLE_RIGTH, 
    "!", N_("[COMMAND]"), N_("Execute external command"), 
    options, parse_opt, NULL, external,
    NULL, NULL, NULL, NULL
};


static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdarg *cmdarg = state->input;

    if (key == ARGP_KEY_ARG) {
        if (cmdarg->d == NULL)
            cmdarg->d = n_array_new(4, free, NULL);
        n_array_push(cmdarg->d, n_strdup(arg));
    }

    return 0;
}

static
int lookup_external_command(char *cmdpath, int size, const char *cmd);

static int external(struct cmdarg *cmdarg) 
{
    struct vopen3_st st;
    char cmd[PATH_MAX], **argv;
    int i, rc;
    
    n_assert(cmdarg->d);

    argv = alloca((n_array_size(cmdarg->d) + 1) * sizeof(*argv));
    for (i=0; i < n_array_size(cmdarg->d); i++) {
        argv[i] = n_array_nth(cmdarg->d, i);
    }
    argv[i] = NULL;

    if (!lookup_external_command(cmd, sizeof(cmd), argv[0])) {
        logn(LOGERR, _("%s: command not found"), cmd);
        rc = 0;
    } else {
        vopen3_init(&st, cmd, argv);
        vopen3_exec(&st, 0);
        rc = vopen3_close(&st) == 0;
    }
    
    return rc;
}

static
int lookup_external_command(char *cmdpath, int size, const char *cmd)
{
    char *path = getenv("PATH");
    const char **tl, **tl_save;
    int  found = 0;
        
    if ((path = getenv("PATH")) == NULL)
        path = "/bin:/usr/bin";

    tl = tl_save = n_str_tokl(path, ":");
    while (*tl) {
        snprintf(cmdpath, size, "%s/%s", *tl, cmd);
        if (access(cmdpath, R_OK | X_OK) == 0) {
            found = 1;
            break;
        }
        tl++;
    }
    n_str_tokl_free(tl_save);
    return found;
}

