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

#define OPT_INST_FETCH      1
#define OPT_INST_NODEPS     2
#define OPT_INST_FORCE      3
#define OPT_INST_REINSTALL  4
#define OPT_INST_DOWNGRADE  5


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
{"reinstall", OPT_INST_REINSTALL, 0, 0, N_("Reinstall"), 1}, 
{"downgrade", OPT_INST_DOWNGRADE, 0, 0, N_("Downgrade"), 1},     

{"fetch", 'd', "DIR", OPTION_ARG_OPTIONAL,
N_("Do not install, only download packages"), 1},

{"nodeps", OPT_INST_NODEPS, 0, 0,
 N_("Install packages with broken dependencies"), 1 },

{0,  'v', 0, 0, N_("Be verbose."), 1 },
{NULL, 'h', 0, OPTION_HIDDEN, "", 1 }, /* alias for -? */
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
            //cmdarg->sh_s->pkgset->flags |= PSMODE_UPGRADE;
            //cmdarg->sh_s->pkgset->flags &= ~PSMODE_INSTALL;

            cmdarg->sh_s->inst->flags &= ~(INSTS_UPGRADE | INSTS_INSTALL);
            cmdarg->sh_s->inst->flags |= INSTS_FOLLOW | INSTS_UPGRADE;
            break;
            
        case 'm':
            cmdarg->sh_s->pkgset->flags |= PSVERIFY_MERCY;
            break;

        case OPT_INST_NODEPS:
            cmdarg->sh_s->inst->flags  |= INSTS_NODEPS;
            break;
            
        case OPT_INST_FORCE:
            cmdarg->sh_s->inst->flags |= INSTS_FORCE;
            break;
            
            
        case 't':
            if (cmdarg->sh_s->inst->flags & INSTS_TEST)
                cmdarg->sh_s->inst->flags |= INSTS_RPMTEST;
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
            cmdarg->sh_s->inst->flags |= INSTS_INSTALL;
            cmdarg->sh_s->inst->flags &= ~INSTS_UPGRADE;
            //cmdarg->sh_s->pkgset->flags |= PSMODE_INSTALL;
            //cmdarg->sh_s->pkgset->flags &= ~PSMODE_UPGRADE;
            break;


        case OPT_INST_REINSTALL:
            cmdarg->sh_s->inst->flags |= INSTS_REINSTALL;
            break;

        case OPT_INST_DOWNGRADE:
            cmdarg->sh_s->inst->flags |= INSTS_DOWNGRADE;
            break;

        case 'd':
            cmdarg->sh_s->inst->flags |= INSTS_JUSTFETCH;
            if (cmdarg->sh_s->inst->fetchdir) {
                free((char*)cmdarg->sh_s->inst->fetchdir);
                cmdarg->sh_s->inst->fetchdir = NULL;
            }
            
            if (arg)
                cmdarg->sh_s->inst->fetchdir = n_strdup(arg);
            break;
            
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int install(struct cmdarg *cmdarg)
{
    tn_array              *shpkgs = NULL;
    struct install_info   iinf;
    int                   i, rc = 1, is_test = 0;
    struct inst_s         *inst;

    inst = cmdarg->sh_s->inst;
    n_assert(inst);

    if ((inst->flags & INSTS_JUSTFETCH) && inst->fetchdir)
        if (!is_rwxdir(inst->fetchdir)) {
            logn(LOGERR, _("%s: no such directory"), inst->fetchdir);
            return 0;
        }
    
    sh_resolve_packages(cmdarg->pkgnames, cmdarg->sh_s->avpkgs, &shpkgs, 1);
    
    if (shpkgs == NULL || n_array_size(shpkgs) == 0) {
        rc = 0;
        goto l_end;
    }

    packages_unmark_all(cmdarg->sh_s->pkgset->pkgs);
    
    for (i=0; i < n_array_size(shpkgs); i++) {
        struct shpkg *shpkg = n_array_nth(shpkgs, i);
        pkg_hand_mark(shpkg->pkg);
    }

    is_test = (inst->flags & (INSTS_TEST | INSTS_RPMTEST));
    
    if (is_test) {
        iinf.installed_pkgs = NULL;
        iinf.uninstalled_pkgs = NULL;
        
    } else {
        iinf.installed_pkgs = pkgs_array_new(16);
        iinf.uninstalled_pkgs = pkgs_array_new(16);
    }
    
    rc = install_pkgs(cmdarg->sh_s->pkgset, cmdarg->sh_s->inst,
                      iinf.installed_pkgs ? &iinf : NULL);
    
    if (rc == 0) 
        msgn(1, _("There were errors"));


    /* update installed set */
    if (iinf.installed_pkgs && cmdarg->sh_s->instpkgs) {
        
        for (i=0; i < n_array_size(iinf.uninstalled_pkgs); i++) {
            struct pkg   *pkg = n_array_nth(iinf.uninstalled_pkgs, i);
            struct shpkg *shpkg = alloca(sizeof(*shpkg) + 1024);
            
            pkg_snprintf(shpkg->nevr, 1024, pkg);
            n_array_remove(cmdarg->sh_s->instpkgs, shpkg);
            //printf("- %s\n", shpkg->nevr);
        }
        n_array_sort(cmdarg->sh_s->instpkgs);
        
        
        for (i=0; i < n_array_size(iinf.installed_pkgs); i++) {
            struct pkg     *pkg = n_array_nth(iinf.installed_pkgs, i);
            struct shpkg   *shpkg;
            char           nevr[1024];
            int            len;

            
            len = pkg_snprintf(nevr, sizeof(nevr), pkg);
            //printf("+ %s\n", nevr);
            shpkg = n_malloc(sizeof(*shpkg) + len + 1);
            memcpy(shpkg->nevr, nevr, len + 1);
            shpkg->pkg = pkg_link(pkg);

            n_array_push(cmdarg->sh_s->instpkgs, shpkg);
        }
        n_array_sort(cmdarg->sh_s->instpkgs);
        
        
        //printf("s = %d\n", n_array_size(cmdarg->sh_s->instpkgs));
        if (n_array_size(iinf.installed_pkgs) + n_array_size(iinf.uninstalled_pkgs))
            cmdarg->sh_s->ts_instpkgs = time(0);
    }
    
    if (iinf.installed_pkgs) {
        n_array_free(iinf.installed_pkgs);
        n_array_free(iinf.uninstalled_pkgs);
    }
    
    
 l_end:

    if (shpkgs != cmdarg->sh_s->avpkgs)
        n_array_free(shpkgs);
    
    return rc;
}


