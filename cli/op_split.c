/* 
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

#include "pkgdir/pkgdir.h"

#include "cli.h"
#include "op.h"

#define OPT_GID  OPT_GID_OP_SPLIT

#define OPT_SPLITSIZE    (OPT_GID + 2) /* legacy */
#define OPT_SPLITCONF    (OPT_GID + 3)
#define OPT_SPLITPREFIX  (OPT_GID + 4)


/* The options we understand. */
static struct argp_option options[] = {
{0,0,0,0, N_("Splitting:"), OPT_GID},
{"split", OPT_SPLITSIZE, "SIZE[:FIRST_FREE_SPACE]", 0,
     N_("Split package set to SIZE MB size chunks, the first chunk will "
           "be FIRST_FREE_SPACE MB smaller"), OPT_GID },
    
{"split-conf", OPT_SPLITCONF, "FILE", OPTION_HIDDEN,
     N_("Take package priorities from FILE"), OPT_GID },
    
{"priconf", OPT_SPLITCONF, "FILE", 0,
     N_("Take package priorities from FILE"), OPT_GID },
    
{"split-out", OPT_SPLITPREFIX, "PREFIX", 0,
     N_("Write chunks to PREFIX.XX, default PREFIX is 'packages.chunk'"), OPT_GID },    
{ 0, 0, 0, 0, 0, 0 },
};

struct arg_s {
    unsigned   cnflags;
    int        size;
    int        first_free_space;
    char       *prefix;
};

#define DO_SPLIT (1 << 0)

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

struct poclidek_opgroup poclidek_opgroup_split = {
    N_("Repository index creation:"), 
    &poclidek_argp, 
    &poclidek_argp_child,
    oprun,
};

static void arg_s_free(void *a) 
{
    struct arg_s *arg_s = a;
    
    n_cfree(&arg_s->prefix);
    free(arg_s);
}


static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct poclidek_opgroup_rt   *rt;
    struct arg_s *arg_s;
    
    
    rt = state->input;
    if (rt->_opdata) {
        arg_s = rt->_opdata;
        
    } else {
        arg_s = n_malloc(sizeof(*arg_s));
        arg_s->cnflags = 0;
        arg_s->size = arg_s->first_free_space = 0;
        arg_s->prefix = NULL;
        rt->_opdata = arg_s;
        rt->_opdata_free = arg_s_free;
        rt->run = oprun;
    }

    switch (key) {
        case OPT_SPLITSIZE: {
            char *p;
            int  rc;
            
            if ((p = strrchr(arg, ':'))) {
                rc = sscanf(arg, "%d:%d", &arg_s->size,
                            &arg_s->first_free_space);
                rc = (rc == 2);

            } else {
                rc = sscanf(arg, "%d", &arg_s->size);
                rc = (rc == 1);
            }
            
            if (rc)
                arg_s->cnflags |= DO_SPLIT;
            
            else {
                logn(LOGERR, _("split: bad option argument"));
                exit(EXIT_FAILURE);
            }
        }
            break;
            
        case OPT_SPLITCONF:
            poldek_configure(rt->ctx, POLDEK_CONF_PRIFILE, arg);
            break;

        case OPT_SPLITPREFIX:
            arg_s->prefix = n_strdup(arg);
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int do_split(struct poldek_ctx *ctx, struct arg_s *arg_s) 
{

    if (arg_s->size < 50) {
        logn(LOGERR, _("split size too small"));
        return 0;
    }
            
    if (arg_s->size < arg_s->first_free_space) {
        logn(LOGERR, _("first free space bigger than chunk size"));
        return 0;
    }

    return poldek_split(ctx, arg_s->size, arg_s->first_free_space,
                        arg_s->prefix);
}


static int oprun(struct poclidek_opgroup_rt *rt)
{
    struct arg_s *arg_s;
    int rc = OPGROUP_RC_NIL;
    
    arg_s = rt->_opdata;
    n_assert(arg_s);
    
    if (arg_s->cnflags & DO_SPLIT) {
        if (poldek_load_sources(rt->ctx)) 
            rc = do_split(rt->ctx, arg_s);
        else
            rc = 0;
        
        rc = rc ? OPGROUP_RC_OK : OPGROUP_RC_ERROR; 
    }
    
    return rc;
}

