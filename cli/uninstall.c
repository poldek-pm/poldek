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
#include "cli.h"
#include "misc.h"

static error_t parse_opt(int key, char *arg, struct argp_state *state);
static int uninstall(struct cmdarg *cmdarg);


#define OPT_INST_NODEPS  2
#define OPT_INST_FORCE   3

static struct argp_option options[] = {
{"force", OPT_INST_FORCE, 0, 0, N_("Be unconcerned"), 1 },
{"test", 't', 0, 0, N_("Don't uninstall, but tell if it would work or not"), 1 },
{"nofollow", 'N', 0, 0, N_("Don't automatically remove packages orphaned by "
                           "selected ones"), 1 },    
{"nodeps", OPT_INST_NODEPS, 0, 0,
 N_("Ignore broken dependencies"), 1 },
{0,  'v', 0, 0, N_("Be verbose"), 1 },
{NULL, 'h', 0, OPTION_HIDDEN, "", 1 },
{ 0, 0, 0, 0, 0, 0 },
};


struct command command_uninstall = {
    COMMAND_HASVERBOSE | COMMAND_MODIFIESDB, 

    "uninstall", N_("PACKAGE..."), N_("Uninstall packages"), 
    
    options, parse_opt,
    
    NULL, uninstall, NULL, NULL, NULL
};


static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdarg *cmdarg = state->input;
    struct poldekcli_ctx *cctx = cmdarg->cctx;
    
    arg = arg;
    switch (key) {
        case ARGP_KEY_INIT:
            
            //cmdarg->cctx->pkgset->flags |= PSMODE_UPGRADE;
            //cmdarg->cctx->pkgset->flags &= ~PSMODE_INSTALL;
            poldek_configure_f_reset(cctx->ctx);
            break;
            
        case 'm':
            //cmdarg->cctx->pkgset->flags |= PSVERIFY_MERCY;
            break;

        case OPT_INST_NODEPS:
            poldek_configure_f(cctx->ctx, INSTS_NODEPS);
            break;
            
        case OPT_INST_FORCE:
            poldek_configure_f(cctx->ctx, INSTS_FORCE);
            break;

        case 't':
            if (poldek_configure_f_isset(cctx->ctx, INSTS_TEST))
                poldek_configure_f(cctx->ctx, INSTS_RPMTEST);
            else
                poldek_configure_f(cctx->ctx, INSTS_TEST);
            break;

        case 'N':
            poldek_configure_f_clr(cctx->ctx, INSTS_FOLLOW);
            break;
        
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int uninstall(struct cmdarg *cmdarg) 
{
    struct poldekcli_ctx  *cctx = cmdarg->cctx;
    tn_array              *pkgs = NULL;
    struct install_info   iinf;
    int                   i, err = 0;
    
    
    if (cctx->instpkgs == NULL) {
        log(LOGERR, "uninstall: installed packages not loaded, "
            "type \"reload\" to load them\n");
        return 0;
    }
    
    sh_resolve_packages(cmdarg->pkgnames, cctx->instpkgs, &pkgs, 1);
    if (pkgs == NULL || n_array_size(pkgs) == 0) {
        err++;
        goto l_end;
    }
    
    if (pkgs == cctx->instpkgs) {
        logn(LOGERR, _("uninstall: better do \"rm -rf /\""));
        return 0;
    }
    
    if (err) 
        goto l_end;

#if 0                           /* DUPA */
    pkgs = pkgs_array_new(n_array_size(pkgs));

    for (i=0; i<n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        n_array_push(pkgs, pkg_link(pkg->pkg));
    }
#endif
    
    if (poldek_configure_f_isset(cctx->ctx, INSTS_TEST | INSTS_RPMTEST)) {
        iinf.installed_pkgs = NULL;
        iinf.uninstalled_pkgs = pkgs_array_new(16);
        
    } else {
        iinf.installed_pkgs = NULL;
        iinf.uninstalled_pkgs = NULL;
    }
    
    if (!packages_uninstall(pkgs, cctx->ctx->inst,
                            iinf.uninstalled_pkgs ? &iinf : NULL))
        err++;
    
    if (poldek_configure_f_isset(cctx->ctx, INSTS_TEST | INSTS_RPMTEST)) {
        for (i=0; i < n_array_size(iinf.uninstalled_pkgs); i++) {
            struct pkg *pkg = n_array_nth(iinf.uninstalled_pkgs, i);
            n_array_remove(cmdarg->cctx->instpkgs, pkg);
            DBGF("- %s\n", pkg->nevr);
        }
        n_array_sort(cmdarg->cctx->instpkgs);
        //cmdarg->cctx->ts_instpkgs = time(0);
    }
    
    if (iinf.uninstalled_pkgs)
        n_array_free(iinf.uninstalled_pkgs);
    
 l_end:
    if (pkgs && pkgs != cmdarg->cctx->instpkgs) 
        n_array_free(pkgs);
    
    return err == 0;
}
