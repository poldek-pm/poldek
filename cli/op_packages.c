/* 
  Copyright (C) 2000 - 2003 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <argp.h>

#include <trurl/nmalloc.h>

#include "i18n.h"
#include "log.h"

#include "pkgset.h"
#include "cli.h"
#include "op.h"

#define OPT_GID             1800
#define OPT_PKGSET          (OPT_GID + 1)
#define OPT_NEVR            (OPT_GID + 2)

static struct argp_option options[] = {
{0,0,0,0, N_("Package selection:"), OPT_GID},
{"pset", OPT_PKGSET, "FILE", 0, N_("Take package set definition from FILE"), OPT_GID },
{"pkgset", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, 0, OPT_GID }, /* backward compat */

// obsoleted by '#'    
{"nevr", OPT_NEVR, "\"NAME [[E:][V[-R]]]\"", OPTION_HIDDEN,
     "Specifies package by NAME and EVR", OPT_GID },
    
{"pkgnevr", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, 0,  OPT_GID }, /* backward compat */
{ 0, 0, 0, 0, 0, 0 },
};

static error_t parse_opt(int key, char *arg, struct argp_state *state);
static int oprun(struct poclidek_opgroup_rt *rt);

static struct argp poclidek_argp = {
    options, parse_opt, 0, 0, 0, 0, 0
};

static 
struct argp_child poclidek_argp_child = {
    &poclidek_argp, 0, NULL, OPT_GID,
};

struct poclidek_opgroup poclidek_opgroup_packages = {
    "Package selection", 
    &poclidek_argp, 
    &poclidek_argp_child,
    oprun,
};

static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct poclidek_opgroup_rt *rt;

    rt = state->input;
    printf("OPP %d %s\n", key, arg);
    switch (key) {
        case OPT_NEVR:
            n_array_push(rt->cctx->arg_packages, arg);
            break;

        case OPT_PKGSET:
            n_array_push(rt->cctx->arg_package_sets, arg);
            break;

        
        case ARGP_KEY_ARG:
            if (strncmp(arg, "--rpm-", 6) != 0)
                n_array_push(rt->cctx->arg_packages, arg);
                
            
            else if (strlen(arg) > 8) {
                char *optname;
                arg += strlen("--rp");
                *arg = '-';
                
                optname = arg + 2;
                if (strncmp(optname, "force", 5) == 0 ||
                    strncmp(optname, "install", 7) == 0 ||
                    strncmp(optname, "upgrade", 7) == 0 ||
                    strncmp(optname, "nodeps", 6) == 0  ||
                    strncmp(optname, "justdb", 6) == 0  ||
                    strncmp(optname, "test", 4) == 0    ||
                    strncmp(optname, "root", 4) == 0) {
                     logn(LOGERR, _("'%s' option should be set by --%s"),
                          optname, optname);
                     exit(EXIT_FAILURE);
                }

                if (strcmp(optname, "ignorearch") == 0)
                    poldek_configure_f(rt->cctx->ctx, INSTS_IGNOREARCH);
                else if (strcmp(optname, "ignoreos") == 0)
                    poldek_configure_f(rt->cctx->ctx, INSTS_IGNOREOS);
                else 
                    ;           /* DUPA */
                //n_assert(argsp->inst.rpmopts != NULL);
                //n_array_push(argsp->inst.rpmopts, arg);
                
            } else {
                argp_usage (state);
            }
            break;
    

        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}

static
int is_package_file(const char *path)
{
    struct stat st;
    
    if (strstr(path, ".rpm") == 0)
        return 0;

    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static
int prepare_given_packages(struct poldekcli_ctx *cctx) 
{
    int i, rc = 1;
    
    if (cctx->ups == NULL)
        cctx->ups = usrpkgset_new();

    for (i=0; i < n_array_size(cctx->arg_package_sets); i++) {
        char *path = n_array_nth(cctx->arg_package_sets, i);
        
        if (!usrpkgset_add_list(cctx->ups, path))
            rc = 0;
    }

    for (i=0; i<n_array_size(cctx->arg_packages); i++) {
        char *path = n_array_nth(cctx->arg_packages, i);

        if (is_package_file(path)) 
            rc = usrpkgset_add_pkgfile(cctx->ups, path);
        else
            rc = usrpkgset_add_str(cctx->ups, path, strlen(path));
    }
    
    usrpkgset_setup(cctx->ups);
    return usrpkgset_size(cctx->ups);
}

static int oprun(struct poclidek_opgroup_rt *rt)
{
    printf("oprun packages %p\n", rt);

    prepare_given_packages(rt->cctx);
    printf("oprun packages %p, %d\n", rt, usrpkgset_size(rt->cctx->ups));
    return 0;
}
