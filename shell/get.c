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

#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "pkgset.h"
#include "misc.h"
#include "shell.h"


static error_t parse_opt(int key, char *arg, struct argp_state *state);
static int get(struct cmdarg *cmdarg);


#define OPT_GET_VERBOSE      (1 << 0) /* cmd_state->flags */
#define OPT_GET_DIR          (1 << 1) /* cmd_state->flags */

static struct argp_option options[] = {
 { 0, 'v', 0, 0, "Be verbose", 1},
 { "dir", 'd', "DIR", 0, "Download to directory DIR instead to current one", 1},
 {NULL, 'h', 0, OPTION_HIDDEN, "", 1 },
 { 0, 0, 0, 0, 0, 0 },
};


struct command command_get = {
    0, 
    "get", "PACKAGE...", "Download packages", 
    options, parse_opt,
    NULL, get,
    NULL, NULL, NULL, NULL
};



static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdarg *cmdarg = state->input;

    arg = arg;
    
    switch (key) {
        case 'd':
            cmdarg->sh_s->inst->fetchdir = trimslash(arg);
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int get(struct cmdarg *cmdarg)
{
    tn_array *shpkgs = NULL, *av_shpkgs, *pkgs;
    char destdir[PATH_MAX], *destdirp;
    int i, err = 0;
    
    
    if (n_array_size(cmdarg->pkgnames) == 0)
        return 0;
    
    av_shpkgs = cmdarg->sh_s->avpkgs;
    sh_resolve_packages(cmdarg->pkgnames, av_shpkgs, &shpkgs, 1);

    if (shpkgs == NULL) 
        return 0;

    if (n_array_size(shpkgs) == 0)
        log(LOGERR, "get: specify what packages you want to download\n");

    /* build array if struct pkg */
    pkgs = n_array_new(n_array_size(shpkgs), NULL, NULL);
    for (i=0; i<n_array_size(shpkgs); i++) {
        struct shpkg *shpkg = n_array_nth(shpkgs, i);
        n_assert(shpkg->pkg->pkgdir);
        if (!pkgdir_isremote(shpkg->pkg->pkgdir)) {
            log(LOGERR, "get: %s: %s is not remote URL\n",
                pkg_snprintf_s(shpkg->pkg), shpkg->pkg->pkgdir->path);
            continue;
        }
        n_array_push(pkgs, shpkg->pkg);
    }
    
    if (n_array_size(pkgs) == 0)
        goto l_end;
    
    if (cmdarg->sh_s->inst->fetchdir != NULL) {
        destdirp = (char*)cmdarg->sh_s->inst->fetchdir;
        
    } else {
        if (getcwd(destdir, sizeof(destdir)) == NULL) {
            log(LOGERR, "getcwd: %m\n");
            err = 1;
            goto l_end;
        }
        destdirp = destdir;
    }
    
    if (!pkgset_fetch_pkgs(destdirp, pkgs, 1))
        err++;
    
 l_end:
    if (pkgs)
        n_array_free(pkgs);

    if (shpkgs)
        n_array_free(shpkgs);
    

    cmdarg->sh_s->inst->fetchdir = NULL;
    return err == 0;
}


