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

#include <trurl/nassert.h>
#include <trurl/narray.h>

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "pkgset.h"
#include "misc.h"
#include "cli.h"


static error_t parse_opt(int key, char *arg, struct argp_state *state);
static int get(struct cmdarg *cmdarg);


#define OPT_GET_VERBOSE      (1 << 0) /* cmd_state->flags */
#define OPT_GET_DIR          (1 << 1) /* cmd_state->flags */

static struct argp_option options[] = {
 { 0, 'v', 0, 0, N_("Be verbose"), 1},
 { "dir", 'd', "DIR", 0, N_("Download to directory DIR instead to current one"), 1},
 {NULL, 'h', 0, OPTION_HIDDEN, "", 1 },
 { 0, 0, 0, 0, 0, 0 },
};


struct command command_get = {
    0, 
    "get", N_("PACKAGE..."), N_("Download packages"), 
    options, parse_opt,
    NULL, get,
    NULL, NULL, NULL
};



static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdarg *cmdarg = state->input;
    struct poldekcli_ctx *cctx = cmdarg->cctx;
    
    switch (key) {
        case 'd':
            if (arg) {
                if (!is_rwxdir(arg)) {
                    logn(LOGERR, _("%s: no such directory"), arg);
                    return EINVAL;
                }
                
                poldek_configure(cctx->ctx, POLDEK_CONF_FETCHDIR, arg);
            }
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int get(struct cmdarg *cmdarg)
{
    struct poldekcli_ctx  *cctx;
    tn_array *pkgs = NULL, *av_pkgs, *get_pkgs;
    char destdir[PATH_MAX], *destdirp;
    int i, err = 0;

    
    cctx = cmdarg->cctx;
    if (n_array_size(cmdarg->pkgnames) == 0)
        return 0;
    
    av_pkgs = cmdarg->cctx->avpkgs;
    sh_resolve_packages(cmdarg->pkgnames, av_pkgs, &pkgs, 1);

    if (pkgs == NULL) 
        return 0;

    if (n_array_size(pkgs) == 0)
        logn(LOGERR, _("get: specify what packages you want to download"));

    /* build array if struct pkg */
    get_pkgs = n_array_new(n_array_size(pkgs), NULL, NULL);
    for (i=0; i<n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        n_assert(pkg->pkgdir);
        if (!pkgdir_isremote(pkg->pkgdir)) {
            logn(LOGWARN, _("get: %s/%s skipped local path"),
                 pkg->pkgdir->path, pkg_snprintf_s(pkg));
            continue;
        }
        n_array_push(get_pkgs, pkg);
    }
    
    if (n_array_size(get_pkgs) == 0)
        goto l_end;
    
    if (cmdarg->cctx->inst->fetchdir != NULL) {
        destdirp = (char*)cmdarg->cctx->inst->fetchdir;
        
    } else {
        if (getcwd(destdir, sizeof(destdir)) == NULL) {
            logn(LOGERR, "getcwd: %m");
            err = 1;
            goto l_end;
        }
        destdirp = destdir;
    }
    
    if (!packages_fetch(get_pkgs, destdirp, 1))
        err++;
    
 l_end:
    if (get_pkgs)
        n_array_free(get_pkgs);

    if (pkgs)
        n_array_free(pkgs);

    poldek_configure(cctx->ctx, POLDEK_CONF_FETCHDIR, NULL);
    return err == 0;
}


