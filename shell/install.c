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
#include "install.h"
#include "shell.h"


static error_t parse_opt(int key, char *arg, struct argp_state *state);
static int install(struct cmdarg *cmdarg);

#define OPT_INST_FETCH    1
#define OPT_INST_NODEPS   2
#define OPT_INST_FORCE    3
#define OPT_INST_INSTALL  1

static struct argp_option options[] = {
{"mercy", 'm', 0, OPTION_HIDDEN, "Be tolerant for bugs which RPM tolerates", 1},
{"force", OPT_INST_FORCE, 0, 0, "Be unconcerned", 1 },
{"test", 't', 0, 0, "Don't install, but tell if it would work or not", 1 },
{"freshen", 'F', 0, 0, "Upgrade packages, but only if an earlier version "
     "currently exists", 1 },

{"nofollow", 'N', 0, 0, "Don't automatically install packages required by "
     "installed ones", 1 }, 

{0, 'I', 0, 0, "Install, not upgrade packages", 1 },

{"fetch", OPT_INST_FETCH, "DIR", OPTION_HIDDEN,
 "Do not install, only fetch packages", 1},

{"nodeps", OPT_INST_NODEPS, 0, 0,
 "Install packages with broken dependencies", 1 },


{0,  'v', "v...", OPTION_ARG_OPTIONAL, "Be more (and more) verbose.", 1 },
{ 0, 0, 0, 0, 0, 0 },
};

struct command command_install = {
    0, 

    "install", "[PACKAGE...]", "Install packages", 
    
    options, parse_opt,
    
    NULL, install, NULL, NULL, 
};



static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdarg *cmdarg = state->input;

    arg = arg;
    switch (key) {
        case ARGP_KEY_INIT:
            cmdarg->sh_s->pkgset->flags |= PSMODE_UPGRADE;
            cmdarg->sh_s->pkgset->flags &= ~PSMODE_INSTALL;
            
            cmdarg->sh_s->inst->flags |= INSTS_FOLLOW;
            break;
            
        case 'm':
            cmdarg->sh_s->pkgset->flags |= PSVERIFY_MERCY;
            break;

        case OPT_INST_NODEPS:
            cmdarg->sh_s->inst->instflags  |= PKGINST_NODEPS;
            break;
            
        case OPT_INST_FORCE:
            cmdarg->sh_s->inst->instflags |= PKGINST_FORCE;
            break;
            
        case 't':
            cmdarg->sh_s->inst->instflags |= PKGINST_TEST;
            break;

        case 'F':
            cmdarg->sh_s->inst->flags |= INSTS_FRESHEN;
            break;

        case 'N':
            cmdarg->sh_s->inst->flags &= ~INSTS_FOLLOW;
            break;

        case 'I':
            cmdarg->sh_s->pkgset->flags |= PSMODE_INSTALL;
            cmdarg->sh_s->pkgset->flags &= ~PSMODE_UPGRADE;
            break;
            
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int install(struct cmdarg *cmdarg)
{
    tn_array *shpkgs = NULL;
    tn_array *uninst_pkgs = NULL;
    int i, rc = 1;

    
    sh_resolve_packages(cmdarg->pkgnames, cmdarg->sh_s->avpkgs, &shpkgs, 1);
    
    if (shpkgs == NULL) {
        rc = 0;
        goto l_end;
    }
        
    if (n_array_size(shpkgs) == 0) {
        printf("install: specify what packages you want to install\n");
        rc = 0;
        goto l_end;
    }
    
    pkgset_unmark(cmdarg->sh_s->pkgset, PS_MARK_UNMARK_ALL);
    
    for (i=0; i<n_array_size(shpkgs); i++) {
        struct shpkg *shpkg = n_array_nth(shpkgs, i);
        pkg_hand_mark(shpkg->pkg);
    }
    
    uninst_pkgs = pkgs_array_new(16);
    rc = install_pkgs(cmdarg->sh_s->pkgset, cmdarg->sh_s->inst, uninst_pkgs);

    if (rc == 0) {
        printf("Installation failed\n");
        
    } else if (cmdarg->sh_s->instpkgs) { /* update installed set */
        for (i=0; i<n_array_size(cmdarg->sh_s->avpkgs); i++) {
            struct shpkg *shpkg = n_array_nth(cmdarg->sh_s->avpkgs, i);
            if (pkg_is_marked(shpkg->pkg))
                n_array_push(cmdarg->sh_s->instpkgs, shpkg_link(shpkg));
        }
            
        n_array_sort(cmdarg->sh_s->instpkgs);
            
        for (i=0; i<n_array_size(uninst_pkgs); i++) {
            struct pkg   *pkg = n_array_nth(uninst_pkgs, i);
            struct shpkg *shpkg = alloca(sizeof(*shpkg) + 1024);
            
            pkg_snprintf(shpkg->nevr, 1024, pkg);
            n_array_remove(cmdarg->sh_s->instpkgs, shpkg);
        }
        n_array_sort(cmdarg->sh_s->instpkgs);
        
    }
    n_array_free(uninst_pkgs);
    
 l_end:

    if (shpkgs != cmdarg->sh_s->avpkgs)
        n_array_free(shpkgs);
    
    return rc;
}


