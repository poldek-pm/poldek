/* 
  Copyright (C) 2000 - 2008 Pawel A. Gajda (mis@pld-linux.org)
 
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

#include "compiler.h"
#include "i18n.h"
#include "log.h"

#include "cli.h"
#include "op.h"

#define OPT_GID             OPT_GID_OP_PACKAGES
#define OPT_PKGSET          (OPT_GID + 1)
#define OPT_NEVR            (OPT_GID + 2)
#define OPT_CAPLOOKUP       (OPT_GID + 3)

static struct argp_option options[] = {
{0,0,0,0, N_("Package related options:"), OPT_GID},
{"pset", OPT_PKGSET, "FILE", 0, N_("Take package set definition from FILE"), OPT_GID },
{"pkgset", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, 0, OPT_GID }, /* backward compat */

// obsoleted by '#'    
{"nevr", OPT_NEVR, "\"NAME [[E:][V[-R]]]\"", OPTION_HIDDEN,
     "Specifies package by NAME and EVR", OPT_GID },
    
{"pkgnevr", 0, 0, OPTION_ALIAS | OPTION_HIDDEN, 0,  OPT_GID }, /* backward compat */
{"caplookup", OPT_CAPLOOKUP, 0, 0,
N_("Look into package capabilities and files to resolve packages"), OPT_GID },
{ 0, 0, 0, 0, 0, 0 },
};

static error_t parse_opt(int key, char *arg, struct argp_state *state);

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
    NULL
};

static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct poclidek_opgroup_rt *rt;

    rt = state->input;

    switch (key) {
        case OPT_NEVR:
            poldek_ts_add_pkgmask(rt->ts, arg);
            break;

        case OPT_PKGSET:
            poldek_ts_add_pkglist(rt->ts, arg);
            break;

        case OPT_CAPLOOKUP:
            rt->ts->setop(rt->ts, POLDEK_OP_CAPLOOKUP, 1);
            break;
        
        case ARGP_KEY_ARG:
            DBGF("op_packages.arg %s\n", arg);
            
            if (strncmp(arg, "--rpm-", 6) != 0 && strncmp(arg, "rpm--", 5) != 0)
                poldek_ts_add_pkgfile(rt->ts, arg);
            
            else if (strlen(arg) < 7)
                argp_usage (state);
            
            else {
                char *optname;
                
                if (*arg == '-') { /* --rpm-FOO */
                    arg += strlen("--rp");
                    *arg = '-';
                    
                } else { /* rpm--FOO */
                    arg += 3;
                }
                
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
                
                poldek_ts_configure(rt->ts, POLDEK_CONF_RPMOPTS, arg);
            }
            break;
    

        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}
