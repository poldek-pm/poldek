/*
  Copyright (C) 2000 - 2005 Pawel A. Gajda <mis@pld.org.pl>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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


#define OPT_GID  OPT_GID_OP_VERIFY

#define OPT_DEPS        'V'
#define OPT_CNFLS       (OPT_GID + 1)
#define OPT_FILECNFLS   (OPT_GID + 2)
#define OPT_FILEORPHANS (OPT_GID + 3)
#define OPT_ALL         (OPT_GID + 4)

/* The options we understand. */
static struct argp_option options[] = {
{0,0,0,0, N_("Package set verification:"), OPT_GID},
{"verify",  OPT_DEPS, "REPORT-TYPE...", OPTION_ARG_OPTIONAL,
     N_("Verify package set, available reports are: 'deps', 'order', 'conflicts', "
        "'file-conflicts', 'file-orphans' and 'file-missing-deps'. "
        "Default is 'deps'."), OPT_GID },
{"verify-conflicts",  OPT_CNFLS, 0, OPTION_HIDDEN, N_("Verify conflicts"), OPT_GID },
{"verify-fileconflicts",  OPT_FILECNFLS, 0, OPTION_HIDDEN,
     N_("Verify file conflicts"),OPT_GID },

{"verify-fileorphans",  OPT_FILEORPHANS, 0, OPTION_HIDDEN,
     N_("Find orphaned directories"),OPT_GID },
    
{"verify-all",  OPT_ALL, 0, OPTION_HIDDEN,
N_("Verify dependencies, conflicts, file conflicts and orphaned directories"),
     OPT_GID },
{ 0, 0, 0, 0, 0, 0 },
};

static
error_t parse_opt(int key, char *arg, struct argp_state *state);

static struct verify_op {
    const char *name;
    int op;
} verify_options[] = {
    { "deps", POLDEK_OP_VRFY_DEPS },
    { "order", POLDEK_OP_VRFY_ORDER },
    { "conflicts", POLDEK_OP_VRFY_CNFLS }, 
    { "file-conflicts", POLDEK_OP_VRFY_FILECNFLS },
    { "file-orphans", POLDEK_OP_VRFY_FILEORPHANS },
    { "file-missing-deps", POLDEK_OP_VRFY_FILEMISSDEPS },
    { NULL, 0 }, 
};

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
    int error;
};

static int arg_to_ts_option(char *arg, struct poldek_ts *ts)
{
    const char **tl_save, **tl;
    int nerr = 0;
    
    tl = tl_save = n_str_tokl(arg, ",");
    while (*tl) {
        int i = 0, found = 0, setall = 0;
        
        if (n_str_eq(*tl, "all")) {
            setall = 1;
            found = 1;
        }
        
        while (verify_options[i].name != NULL) {
            if (setall) {
                ts->setop(ts, verify_options[i].op, 1);
                i++;
                continue;
            }
            
            if (n_str_eq(verify_options[i].name, *tl)) {
                ts->setop(ts, verify_options[i].op, 1);
                found = 1;
                break;
            }
            i++;
        }

        if (!found) {
            logn(LOGERR, "%s: unknown verify parameter", *tl);
            nerr++;
        }
        
        tl++;
    }
    n_str_tokl_free(tl_save);
    
    return nerr == 0;
}

    

static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct poclidek_opgroup_rt   *rt;
    struct poldek_ts *ts;
    struct arg_s *arg_s;
    const char *mode = "verify";
    
    arg = arg;
    rt = state->input;
    ts = rt->ts;
    if (rt->_opdata) {
        arg_s = rt->_opdata;
        
    } else {
        arg_s = n_malloc(sizeof(*arg_s));
        arg_s->verify = 0;
        arg_s->error = 0;
        rt->_opdata = arg_s;
        rt->_opdata_free = free;
        rt->run = oprun;
    }
    switch (key) {
        case OPT_DEPS:
            arg_s->verify = 1;

            if (arg == NULL) 
                ts->setop(ts, POLDEK_OP_VRFY_DEPS, 1);

            else if (!arg_to_ts_option(arg, ts)) {
                arg_s->error = 1;
                return EINVAL;
            }
            
            rt->set_major_mode(rt, mode, "verify");
            break;

        case OPT_CNFLS:
            arg_s->verify = 1;
            ts->setop(ts, POLDEK_OP_VRFY_CNFLS, 1);
            rt->set_major_mode(rt, mode, "verify-conflicts");
            break;

        case OPT_FILECNFLS:
            arg_s->verify = 1;
            ts->setop(ts, POLDEK_OP_VRFY_FILECNFLS, 1);
            rt->set_major_mode(rt, mode, "verify-fileconflicts");
            break;
            
        case OPT_FILEORPHANS:
            arg_s->verify = 1;
            ts->setop(ts, POLDEK_OP_VRFY_FILEORPHANS, 1);
            rt->set_major_mode(rt, mode, "verify-fileorphans");
            break;

        case OPT_ALL:
            arg_s->verify = 1;
            ts->setop(ts, POLDEK_OP_VRFY_DEPS, 1);
            ts->setop(ts, POLDEK_OP_VRFY_CNFLS, 1);
            ts->setop(ts, POLDEK_OP_VRFY_FILECNFLS, 1);
            ts->setop(ts, POLDEK_OP_VRFY_FILEORPHANS, 1);
            rt->set_major_mode(rt, mode, "verify-all");
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int oprun(struct poclidek_opgroup_rt *rt)
{
    struct arg_s *arg_s;

    arg_s = rt->_opdata;
    n_assert(arg_s);

    if (arg_s->error)
        return OPGROUP_RC_ERROR;

    if (arg_s->verify == 0)
        return OPGROUP_RC_NIL;

    poldek_ts_set_type(rt->ts, POLDEK_TS_VERIFY, "verify");
    return poldek_ts_run(rt->ts, NULL) ? OPGROUP_RC_OK : OPGROUP_RC_ERROR;
}

