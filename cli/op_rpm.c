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

#include "pkgdir/source.h"

#include "cli.h"
#include "op.h"

static struct argp_option source_options[] = {
{ 0, 0, 0, 0, 0, 0 },
};

struct args_s {
    char                *curr_src_path;
    char                *curr_src_type;
};

static
error_t parse_opt(int key, char *arg, struct argp_state *state);

static struct argp poclidek_source_argp = {
    source_options, parse_opt, 0, 0, 0, 0, 0
};

static 
struct argp_child poclidek_source_argp_child = {
    &poclidek_source_argp, 0, NULL, OPT_SRC_GID,
};

static int oprun(struct poclidek_opgroup_rt *);

struct poclidek_opgroup poclidek_opgroup_source = {
    "Source selection", 
    &poclidek_source_argp, 
    &poclidek_source_argp_child,
    oprun,
};

static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct poclidek_opgroup_rt *rt;

    switch (key) {
        case ARGP_KEY_ARG:
            if (strncmp(arg, "--rpm-", 6) != 0) 
                //n_array_push(argsp->pkgdef_files, arg);
                ;
            
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
                else {
                    
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

static void print_source_list(tn_array *sources) 
{
    int i;

    n_array_sort_ex(sources, (tn_fn_cmp)source_cmp_pri_name);
    for (i=0; i < n_array_size(sources); i++)
        source_printf(n_array_nth(sources, i));
    n_array_sort(sources);
}

static int oprun(struct poclidek_opgroup_rt *rt)
{
    struct args_s *arg_s;

    printf("oprun source %p\n", rt);

    arg_s = rt->_opdata;
    n_assert(arg_s);

    if (arg_s->cnflags & POLDEKCLI_SRC_CLEAN) {
        unsigned flags = PKGSOURCE_CLEAN;
        if (arg_s->cnflags & POLDEKCLI_SRC_CLEANA)
            flags |= PKGSOURCE_CLEANA;
        
        sources_clean(rt->cctx->ctx->sources, flags);
    }

    if (arg_s->cnflags & POLDEKCLI_SRC_UPDATE) {
        unsigned flags = PKGSOURCE_UP;
    
        if (arg_s->cnflags & POLDEKCLI_SRC_UPDATEA)
            flags |= PKGSOURCE_UPA;

        sources_update(rt->cctx->ctx->sources, flags);
    }

    if (arg_s->cnflags & POLDEKCLI_SRC_SRCTYPE_LS)
        printf("op: srctype ls\n");

    if (arg_s->cnflags & POLDEKCLI_SRC_SRCLS)
        print_source_list(rt->cctx->ctx->sources);
    
    return 0;
}

