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
#include "op.h"


static error_t parse_opt(int key, char *arg, struct argp_state *state);
static int uninstall(struct cmdarg *cmdarg);

#define OPT_GID             2500
#define OPT_UNINSTALL       'e'
#define OPT_INST_NODEPS     (OPT_GID + 2)
#define OPT_INST_FORCE      (OPT_GID + 3)

static struct argp_option options[] = {
{"force", OPT_INST_FORCE, 0, 0, N_("Be unconcerned"), OPT_GID },
{"test", 't', 0, 0, N_("Don't uninstall, but tell if "
                       "it would work or not"), OPT_GID },
{"nofollow", 'N', 0, 0, N_("Don't remove packages orphaned by "
                           "selected ones"), OPT_GID },    
{"nodeps", OPT_INST_NODEPS, 0, 0, N_("Ignore broken dependencies"), OPT_GID },
{ 0, 0, 0, 0, 0, 0 },
};

static struct argp_option cmdl_options[] = {
    {0,0,0,0, N_("Package deinstallation:"), OPT_GID - 100 },
    {"erase", OPT_UNINSTALL, 0, 0, N_("Uninstall given packages"), OPT_GID - 100 },
    {"uninstall", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, 0, 11 }, 
    { 0, 0, 0, 0, 0, 0 },
};


struct poclidek_cmd command_uninstall = {
    COMMAND_HASVERBOSE | COMMAND_MODIFIESDB, 
    "uninstall", N_("PACKAGE..."), N_("Uninstall packages"), 
    options, parse_opt,
    NULL, uninstall, NULL, NULL, NULL, NULL
};

static struct argp cmd_argp = {
    options, parse_opt, 0, 0, 0, 0, 0
};

static struct argp_child cmd_argp_child[2] = {
    { &cmd_argp, 0, NULL, 0 },
    { NULL, 0, NULL, 0 },
};

static
error_t cmdl_parse_opt(int key, char *arg, struct argp_state *state);

static struct argp poclidek_argp = {
    cmdl_options, cmdl_parse_opt, 0, 0, cmd_argp_child, 0, 0
};


static 
struct argp_child poclidek_argp_child = {
    &poclidek_argp, 0, NULL, OPT_GID,
};


static int cmdl_run(struct poclidek_opgroup_rt *rt);

struct poclidek_opgroup poclidek_opgroup_uninstall = {
    "Package deinstallation", 
    &poclidek_argp, 
    &poclidek_argp_child,
    cmdl_run
};


struct cmdl_arg_s {
    struct cmdarg cmdarg;
};

static
error_t cmdl_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct poclidek_opgroup_rt  *rt;
    struct poldek_ts            *ts;
    struct cmdarg               cmdarg;


    rt = state->input;
    ts = rt->ts;
    arg = arg;
    
    cmdarg.ts = rt->ts;
    
    switch (key) {
        case ARGP_KEY_INIT:
            state->child_inputs[0] = &cmdarg;
            state->child_inputs[1] = NULL;
            break;

        case OPT_UNINSTALL:
            poldek_ts_setf(ts, POLDEK_TS_UNINSTALL);
            break;

        default:
            return ARGP_ERR_UNKNOWN;
            
    }
    
    return 0;
}

static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdarg     *cmdarg = state->input;
    struct poldek_ts  *ts = cmdarg->ts;
    
    arg = arg;
    switch (key) {
        case 'm':
            //cmdarg->cctx->pkgset->flags |= PSVERIFY_MERCY;
            break;

        case OPT_INST_NODEPS:
            ts->setop(ts, POLDEK_OP_NODEPS, 1);
            break;
            
        case OPT_INST_FORCE:
            ts->setop(ts, POLDEK_OP_FORCE, 1);
            break;

        case 't':
            if (ts->getop(ts, POLDEK_OP_TEST))
                ts->setop(ts, POLDEK_OP_RPMTEST, 1);
            else
                ts->setop(ts, POLDEK_OP_TEST, 1);
            break;

        case 'N':
            ts->setop(ts, POLDEK_OP_FOLLOW, 0);
            break;
        
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int uninstall(struct cmdarg *cmdarg) 
{
    struct poclidek_ctx  *cctx;
    struct poldek_ts      *ts;
    tn_array              *pkgs = NULL;
    struct install_info   iinf, *iinfp;
    int                   i, err = 0;
    
    
    cctx = cmdarg->cctx;
    ts = cmdarg->ts;

    
    if (cctx->instpkgs != NULL) {
        //log(LOGERR, "uninstall: installed packages not loaded, "
        //    "type \"reload\" to load them\n");
        //return 0;

        poclidek_set_pkgctx(cctx, POLDEKCLI_PKGCTX_INSTD);
        pkgs = poclidek_resolve_packages(cctx, cmdarg->ts, 1);
        if (pkgs == NULL) {
            err++;
            goto l_end;
        }
        
        if (pkgs == cctx->instpkgs) {
            logn(LOGERR, _("uninstall: better do \"rm -rf /\""));
            return 0;
        }
    
        if (err) 
            goto l_end;
        
        poldek_ts_clean_arg_pkgmasks(ts);
        for (i=0; i < n_array_size(pkgs); i++)
            poldek_ts_add_pkg(ts, n_array_nth(pkgs, i));
    }
    

    if (ts->getop_v(ts, POLDEK_OP_TEST, POLDEK_OP_RPMTEST, 0))
        iinfp = NULL;
    else
        iinfp = &iinf;

    poldek_ts_setf(ts, POLDEK_TS_UNINSTALL);
    if (!poldek_ts_run(ts, iinfp))
        err++;
    
    if (iinfp) {
        poclidek_apply_iinf(cmdarg->cctx, iinfp);
        install_info_destroy(iinfp);
    }
    
    
 l_end:
    if (pkgs) 
        n_array_free(pkgs);
    
    return err == 0;
}


static int cmdl_run(struct poclidek_opgroup_rt *rt)
{
    int rc;
    
    if (!poldek_ts_issetf(rt->ts, POLDEK_TS_UNINSTALL))
        return 0;

    rc = poldek_ts_run(rt->ts, NULL);
    return rc ? 0 : OPGROUP_RC_ERROR;
}
