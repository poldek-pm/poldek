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
#include "install.h"
#include "shell.h"


static error_t parse_opt(int key, char *arg, struct argp_state *state);
static int install(struct cmdarg *cmdarg);

#define OPT_INST_FETCH    1
#define OPT_INST_NODEPS   2
#define OPT_INST_FORCE    3
#define OPT_INST_INSTALL  1

static struct argp_option options[] = {
{"mercy", 'm', 0, 0, N_("Be tolerant for bugs which RPM tolerates"), 1},
{"force", OPT_INST_FORCE, 0, 0, N_("Be unconcerned"), 1 },
{"test", 't', 0, 0, N_("Don't install, but tell if it would work or not"), 1 },
{"fresh", 'F', 0, 0, N_("Upgrade packages, but only if an earlier version "
     "currently exists"), 1 },

{"nofollow", 'N', 0, 0, N_("Don't automatically install packages required by "
     "selected ones"), 1 },

{"greedy", 'G', 0, 0, N_("Automatically upgrade packages which dependencies "
     "are broken by unistalled ones"), 1 }, 
    
{0, 'I', 0, 0, N_("Install, not upgrade packages"), 1 },

{"fetch", OPT_INST_FETCH, "DIR", OPTION_HIDDEN,
N_("Do not install, only fetch packages"), 1},

{"nodeps", OPT_INST_NODEPS, 0, 0,
 N_("Install packages with broken dependencies"), 1 },


{0,  'v', 0, 0, N_("Be verbose."), 1 },
{NULL, 'h', 0, OPTION_HIDDEN, "", 1 },
{ 0, 0, 0, 0, 0, 0 },
};


struct command command_install;

static
struct command_alias cmd_aliases[] = {
    {
        "freshen", "install -FN",  &command_install,
    },

    {
        "upgrade", "install -F",  &command_install,
    },

    {
        "greedy-upgrade", "install -FG",  &command_install,
    },

    {
        "just-install", "install -IN", &command_install,
    },
    
    {
        NULL, NULL, NULL
    }, 
};


struct command command_install = {
    COMMAND_HASVERBOSE | COMMAND_MODIFIESDB, 
    "install", N_("PACKAGE..."), N_("Install packages"), 
    options, parse_opt,
    NULL, install, NULL, NULL,
    (struct command_alias*)&cmd_aliases,
    NULL
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
            if (cmdarg->sh_s->inst->flags & INSTS_TEST)
                cmdarg->sh_s->inst->instflags |= PKGINST_TEST;
            else 
                cmdarg->sh_s->inst->flags |= INSTS_TEST;
            break;
            
        case 'F':
            cmdarg->sh_s->inst->flags |= INSTS_FRESHEN;
            break;

        case 'N':
            cmdarg->sh_s->inst->flags &= ~INSTS_FOLLOW;
            break;

        case 'G':
            cmdarg->sh_s->inst->flags |= INSTS_GREEDY;
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
    int i, rc = 1, is_test;

    
    sh_resolve_packages(cmdarg->pkgnames, cmdarg->sh_s->avpkgs, &shpkgs, 1);
    
    if (shpkgs == NULL || n_array_size(shpkgs) == 0) {
        rc = 0;
        goto l_end;
    }

    pkgset_mark(cmdarg->sh_s->pkgset, PS_MARK_OFF_ALL);
    
    for (i=0; i<n_array_size(shpkgs); i++) {
        struct shpkg *shpkg = n_array_nth(shpkgs, i);
        pkg_hand_mark(shpkg->pkg);
    }

    is_test = (cmdarg->sh_s->inst->flags & INSTS_TEST) |
        (cmdarg->sh_s->inst->instflags & PKGINST_TEST);
    
    uninst_pkgs = pkgs_array_new(16);
    rc = install_pkgs(cmdarg->sh_s->pkgset, cmdarg->sh_s->inst, uninst_pkgs);
    
    if (rc == 0) {
        msgn(1, _("There were errors during install"));
        
    } else if (!is_test && cmdarg->sh_s->instpkgs) { /* update installed set */
        int instpkgs_changed = 0;
        
        for (i=0; i < n_array_size(cmdarg->sh_s->avpkgs); i++) {
            struct shpkg *shpkg = n_array_nth(cmdarg->sh_s->avpkgs, i);
            if (pkg_is_marked(shpkg->pkg)) {
                n_array_push(cmdarg->sh_s->instpkgs, shpkg_link(shpkg));
                instpkgs_changed = 1;
            }
        }
            
        n_array_sort(cmdarg->sh_s->instpkgs);

        if (n_array_size(uninst_pkgs))
            instpkgs_changed = 1;
            
        for (i=0; i < n_array_size(uninst_pkgs); i++) {
            struct pkg   *pkg = n_array_nth(uninst_pkgs, i);
            struct shpkg *shpkg = alloca(sizeof(*shpkg) + 1024);
            
            pkg_snprintf(shpkg->nevr, 1024, pkg);
            n_array_remove(cmdarg->sh_s->instpkgs, shpkg);
        }
        n_array_sort(cmdarg->sh_s->instpkgs);

        if (instpkgs_changed)
            cmdarg->sh_s->ts_instpkgs = time(0);
    }
    n_array_free(uninst_pkgs);
    
 l_end:

    if (shpkgs != cmdarg->sh_s->avpkgs)
        n_array_free(shpkgs);
    
    return rc;
}


