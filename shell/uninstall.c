/* 
  Copyright (C) 2001 Pawel A. Gajda (mis@k2.net.pl)
 
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
#include "misc.h"
#include "shell.h"
#include "misc.h"

static error_t parse_opt(int key, char *arg, struct argp_state *state);
static int uninstall(struct cmdarg *cmdarg);


#define OPT_UNINST_NODEPS  2
#define OPT_UNINST_FORCE   3

static struct argp_option options[] = {
{"force", OPT_UNINST_FORCE, 0, 0, N_("Be unconcerned"), 1 },
{"test", 't', 0, 0, N_("Don't uninstall, but tell if it would work or not"), 1 },
{"nofollow", 'N', 0, 0, N_("Don't automatically remove packages orphaned by "
                           "selected ones"), 1 },    
{"nodeps", OPT_UNINST_NODEPS, 0, 0,
 N_("Ignore broken dependencies"), 1 },
{0,  'v', 0, 0, N_("Be verbose"), 1 },
{NULL, 'h', 0, OPTION_HIDDEN, "", 1 },
{ 0, 0, 0, 0, 0, 0 },
};


struct command command_uninstall = {
    COMMAND_HASVERBOSE | COMMAND_MODIFIESDB, 

    "uninstall", N_("PACKAGE..."), N_("Uninstall packages"), 
    
    options, parse_opt,
    
    NULL, uninstall, NULL, NULL, NULL, NULL
};


static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdarg *cmdarg = state->input;

    arg = arg;
    
    switch (key) {
        case OPT_UNINST_NODEPS:
            cmdarg->sh_s->inst->flags  |= INSTS_NODEPS;
            break;
            
        case OPT_UNINST_FORCE:
            cmdarg->sh_s->inst->flags |= INSTS_FORCE;
            break;

        case 'N':
            cmdarg->sh_s->inst->flags &= ~INSTS_FOLLOW;
            break;
            
        case 't':
            if (cmdarg->sh_s->inst->flags & INSTS_TEST)
                cmdarg->sh_s->inst->flags |= INSTS_RPMTEST;
            else 
                cmdarg->sh_s->inst->flags |= INSTS_TEST;
            break;
        
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}

static int uninstall(struct cmdarg *cmdarg) 
{
    tn_array *shpkgs = NULL, *pkgs = NULL;
    struct install_info iinf;
    int i, err = 0, is_test = 1;
    
    if (cmdarg->sh_s->instpkgs == NULL) {
        log(LOGERR, "uninstall: installed packages not loaded, "
            "type \"reload\" to load them\n");
        return 0;
    }
    
    sh_resolve_packages(cmdarg->pkgnames, cmdarg->sh_s->instpkgs, &shpkgs, 1);
    if (shpkgs == NULL || n_array_size(shpkgs) == 0) {
        err++;
        goto l_end;
    }
    
    
    if (shpkgs == cmdarg->sh_s->instpkgs) {
        logn(LOGERR, _("uninstall: better do \"rm -rf /\""));
        return 0;
    }
    
    if (err) 
        goto l_end;

    
    pkgs = pkgs_array_new(n_array_size(shpkgs));

    for (i=0; i<n_array_size(shpkgs); i++) {
        struct shpkg *shpkg = n_array_nth(shpkgs, i);
        n_array_push(pkgs, pkg_link(shpkg->pkg));
    }

    is_test = (cmdarg->sh_s->inst->flags & (INSTS_TEST | INSTS_RPMTEST));
    if (!is_test) {
        iinf.installed_pkgs = NULL;
        iinf.uninstalled_pkgs = pkgs_array_new(16);
        
    } else {
        iinf.installed_pkgs = NULL;
        iinf.uninstalled_pkgs = NULL;
    }
    
    if (!packages_uninstall(pkgs, cmdarg->sh_s->inst,
                            iinf.uninstalled_pkgs ? &iinf : NULL))
        err++;
    
    if (!err && !is_test) {
        for (i=0; i < n_array_size(iinf.uninstalled_pkgs); i++) {
            struct pkg   *pkg = n_array_nth(iinf.uninstalled_pkgs, i);
            struct shpkg *shpkg = alloca(sizeof(*shpkg) + 1024);
            
            pkg_snprintf(shpkg->nevr, 1024, pkg);
            n_array_remove(cmdarg->sh_s->instpkgs, shpkg);
            DBGF("- %s\n", shpkg->nevr);
        }
        n_array_sort(cmdarg->sh_s->instpkgs);
        cmdarg->sh_s->ts_instpkgs = time(0);
    }
    
    if (iinf.uninstalled_pkgs)
        n_array_free(iinf.uninstalled_pkgs);
    
 l_end:
    if (pkgs != NULL)
        n_array_free(pkgs);

    if (shpkgs && shpkgs != cmdarg->sh_s->instpkgs) 
        n_array_free(shpkgs);
    
    return err == 0;
}
