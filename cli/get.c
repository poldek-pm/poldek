/* 
  Copyright (C) 2000, 2001 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <trurl/trurl.h>

#include "i18n.h"
#include "log.h"
#include "cli.h"
#include "pkgmisc.h"
#include "misc.h"

static error_t parse_opt(int key, char *arg, struct argp_state *state);
static int get(struct cmdctx *cmdctx);


#define OPT_GET_VERBOSE      (1 << 0) /* cmd_state->flags */
#define OPT_GET_DIR          (1 << 1) /* cmd_state->flags */

static struct argp_option options[] = {
 { 0, 'v', 0, 0, N_("Be verbose"), 1},
 { "dir", 'd', "DIR", 0, N_("Download to directory DIR instead to current one"), 1},
 {NULL, 'h', 0, OPTION_HIDDEN, "", 1 },
 { 0, 0, 0, 0, 0, 0 },
};


struct poclidek_cmd command_get = {
    0, 
    "get", N_("PACKAGE..."), N_("Download packages"), 
    options, parse_opt,
    NULL, get, NULL, NULL, NULL, 0, 5
};



static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdctx *cmdctx = state->input;
    
    switch (key) {
        case 'd':
            if (arg) {
                if (!poldek_util_is_rwxdir(arg)) {
                    logn(LOGERR, _("%s: no such directory"), arg);
                    return EINVAL;
                }
                cmdctx->_data = n_strdup(arg);
            }
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int get(struct cmdctx *cmdctx)
{
    struct poclidek_ctx  *cctx;
    tn_array *pkgs = NULL;
    char destdir[PATH_MAX], *destdirp;
    int err = 0;
    
    cctx = cmdctx->cctx;

    pkgs = poclidek_resolve_packages(NULL, cctx, cmdctx->ts, 0);
    if (pkgs == NULL) {
        err++;
        goto l_end;
    }

    destdirp = cmdctx->_data;
    if (destdirp == NULL) {
        if (getcwd(destdir, sizeof(destdir)) == NULL) {
            logn(LOGERR, "getcwd: %m");
            err = 1;
            goto l_end;
        }
        destdirp = destdir;
    }
    
    if (!packages_fetch(cmdctx->ts->pmctx, pkgs, destdirp, 1))
        err++;
    
 l_end:
    if (pkgs)
        n_array_free(pkgs);

    return err == 0;
}


