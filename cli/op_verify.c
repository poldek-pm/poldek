
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

#include <trurl/trurl.h>

#include "i18n.h"
#include "log.h"

#include "pkgdir/pkgdir.h"
#include "pkgdir/source.h"

#include "cli.h"
#include "op.h"


#define OPT_GID  2000

#define OPT_DEPS       'V'
#define OPT_CNFLS      (OPT_GID + 1)
#define OPT_FILECNFLS  (OPT_GID + 2)
#define OPT_ALL        (OPT_GID + 3)

/* The options we understand. */
static struct argp_option options[] = {
{0,0,0,0, N_("Verification options/switches:"), OPT_GID},
{"verify",  OPT_DEPS, 0, 0, N_("Verify dependencies"), OPT_GID },
{"verify-conflicts",  OPT_CNFLS, 0, 0, N_("Verify conflicts"), OPT_GID },
{"verify-fileconflicts",  OPT_FILECNFLS, 0, 0,
     N_("Verify file conflicts"),OPT_GID },
{"verify-all",  OPT_ALL, 0, 0,
     N_("Verify dependencies, conflicts and file conflicts"), OPT_GID },
{ 0, 0, 0, 0, 0, 0 },
};

static
error_t parse_opt(int key, char *arg, struct argp_state *state);

static struct argp poclidek_argp = {
    options, parse_opt, 0, 0, 0, 0, 0
};

static 
struct argp_child poclidek_argp_child = {
    &poclidek_argp, 0, NULL, OPT_GID,
};

static int oprun(struct poclidek_opgroup_rt *);

struct poclidek_opgroup poclidek_opgroup_verify = {
    "", 
    &poclidek_argp, 
    &poclidek_argp_child,
    oprun,
};

struct arg_s {
    int verify;
};

static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct poclidek_opgroup_rt   *rt;
    struct poldek_ts *ts;
    struct arg_s *arg_s;

    arg = arg;
    rt = state->input;
    ts = rt->ts;
    if (rt->_opdata) {
        arg_s = rt->_opdata;
        
    } else {
        arg_s = n_malloc(sizeof(*arg_s));
        arg_s->verify = 0;
        rt->_opdata = arg_s;
        rt->_opdata_free = free;
        rt->run = oprun;
    }
    switch (key) {
        case OPT_DEPS:
            arg_s->verify = 1;
            ts->setop(ts, POLDEK_OP_VRFY_DEPS, 1);
            //poldek_configure(ts->ctx, POLDEK_CONF_OPT, POLDEK_OP_VRFY_DEPS, 1);
            break;

        case OPT_CNFLS:
            arg_s->verify = 1;
            ts->setop(ts, POLDEK_OP_VRFY_CNFLS, 1);
            //poldek_configure(ts->ctx, POLDEK_CONF_OPT, POLDEK_OP_VRFY_CNFLS, 1);
            break;

        case OPT_FILECNFLS:
            arg_s->verify = 1;
            poldek_configure(ts->ctx, POLDEK_CONF_OPT,
                              POLDEK_OP_VRFY_FILECNFLS, 1);
            break;

        case OPT_ALL:
            arg_s->verify = 1;
            poldek_configure(ts->ctx, POLDEK_CONF_OPT, POLDEK_OP_VRFY_DEPS, 1);
            poldek_configure(ts->ctx, POLDEK_CONF_OPT, POLDEK_OP_VRFY_CNFLS, 1);
            poldek_configure(ts->ctx, POLDEK_CONF_OPT,
                             POLDEK_OP_VRFY_FILECNFLS, 1);
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int oprun(struct poclidek_opgroup_rt *rt)
{
    struct arg_s *arg_s;
    int rc = OPGROUP_RC_NIL;
    
    arg_s = rt->_opdata;
    n_assert(arg_s);

    if (arg_s->verify == 0)
        return rc;

    poldek_ts_set_type(rt->ts, POLDEK_TS_VERIFY, "verify");
    rc = poldek_ts_run(rt->ts, NULL) ? OPGROUP_RC_FINI : OPGROUP_RC_ERROR;
    return rc;
}

