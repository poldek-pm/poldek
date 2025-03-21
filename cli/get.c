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

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/param.h>

#include <trurl/trurl.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "pkgmisc.h"
#include "poldek_util.h"
#include "cli.h"

static error_t parse_opt(int key, char *arg, struct argp_state *state);
static int get(struct cmdctx *cmdctx);


#define OPT_GET_VERBOSE      (1 << 0) /* cmd_state->flags */
#define OPT_GET_DIR          (1 << 1) /* cmd_state->flags */

static struct argp_option options[] = {
 { "dir", 'd', "DIR", 0,
   N_("Download to directory DIR instead to current one"), 1},
 { 0, 0, 0, 0, 0, 0 },
};

struct poclidek_cmd command_get = {
    COMMAND_NEEDAVAIL,
    "get", N_("PACKAGE..."), N_("Download packages"),
    options, parse_opt,
    NULL, get, NULL, NULL, NULL, NULL, 0, 5, 0,
    "fetch"
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

    pkgs = poclidek_resolve_packages(NULL, cctx, cmdctx->ts, 0, 0);
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

    if (!packages_fetch(poldek_get_pmctx(cmdctx->ts->ctx), pkgs, destdirp, 1))
        err++;

 l_end:
    if (pkgs)
        n_array_free(pkgs);

    n_cfree(&cmdctx->_data);

    return err == 0;
}
