
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

#define OPT_GID  1210

#define OPT_MKIDX       (OPT_GID + 1)
#define OPT_MKIDXZ      (OPT_GID + 2) /* legacy */
#define OPT_NODESC      (OPT_GID + 3)
#define OPT_COMPR       (OPT_GID + 4)
#define OPT_TYPE        (OPT_GID + 5)
#define OPT_NODIFF      (OPT_GID + 6)

/* The options we understand. */
static struct argp_option options[] = {
{0,0,0,0, N_("Repository index creation:"), OPT_GID},

{"mkidx", OPT_MKIDX, "FILE", OPTION_ARG_OPTIONAL,
 N_("Create package index (SOURCE-PATH/packages.dir by default)"), OPT_GID},

{"mkidx-type", OPT_TYPE, "TYPE", 0,
     N_("Sets the index type (use --stl to list available values)"),
     OPT_GID },

{"mkidxz", OPT_MKIDXZ, "FILE", OPTION_ARG_OPTIONAL | OPTION_HIDDEN,
 N_("Likewise, but gzipped file is created"), OPT_GID},

{"nodesc", OPT_NODESC, 0, 0,
 N_("Don't put package user-level information (like Summary or Description)"
     " into created index."), OPT_GID },

{"nodiff", OPT_NODIFF, 0, 0,
 N_("Don't create index delta files"), OPT_GID },

{"compress", OPT_COMPR, "type", 0, 
 N_("Sets compression type (none, bz2, gz)"), OPT_GID },
{ 0, 0, 0, 0, 0, 0 },
};

struct arg_s {
    unsigned            cnflags;
    unsigned            crflags;
    struct poldek_ctx   *ctx;
    struct source       *src_mkidx;
    char                *idx_type;
    char                *idx_compress;
};
#define DO_MAKEIDX (1 << 0)

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

struct poclidek_opgroup poclidek_opgroup_makeidx = {
    N_(""), 
    &poclidek_argp, 
    &poclidek_argp_child,
    oprun,
};

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
        arg_s->crflags = arg_s->cnflags = 0;
        arg_s->idx_type = NULL;
        arg_s->src_mkidx = NULL;
        arg_s->ctx = rt->ctx;
        rt->_opdata = arg_s;
        rt->run = oprun;
    }

    switch (key) {
        case OPT_TYPE:
            arg_s->idx_type = n_strdup(arg);
            break;
            
        case OPT_MKIDXZ:
        case OPT_MKIDX:
            if (arg)
                arg_s->src_mkidx = source_new_pathspec(NULL, arg, NULL);
            arg_s->cnflags |= DO_MAKEIDX;
            break;
            
        case OPT_NODESC:
            arg_s->crflags |= PKGDIR_CREAT_NODESC;
            break;

        case OPT_NODIFF:
            arg_s->crflags |= PKGDIR_CREAT_NOPATCH;
            break;
            
        case OPT_COMPR:
            arg_s->idx_compress = n_strdup(arg);
            break;
           
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}

static tn_array *parse_types(const char *type) 
{
    tn_array *types = n_array_new(4, free, NULL);
    
    if (strchr(type, ',') == NULL) {
        n_array_push(types, n_strdup(type));
            
    } else {
        const char **tl_save, **tl;
        
        tl = tl_save = n_str_tokl(type, ",");
        while (*tl) {
            n_array_push(types, n_strdup(*tl));
            tl++;
        }
        n_str_tokl_free(tl_save);
    }
    
    return types;
}


static int make_idx(struct arg_s *arg_s) 
{
    struct source   *src;
    const char      *type = NULL, *path = NULL;
    tn_array        *types = NULL;
    int i, j, nerr = 0;

    if (n_array_size(arg_s->ctx->sources) > 1 && arg_s->src_mkidx) {
        logn(LOGERR, _("multiple sources not allowed if index path is specified"));
        return 0;
    }

    if (arg_s->src_mkidx)
        path = arg_s->src_mkidx->path;

    
    if (arg_s->idx_type == NULL)
        arg_s->idx_type = n_strdup(pkgdir_DEFAULT_TYPE);
    types = parse_types(arg_s->idx_type);
    
    
    for (i=0; i < n_array_size(arg_s->ctx->sources); i++) {
        const char *stype, *itype = type;
        
        src = n_array_nth(arg_s->ctx->sources, i);
        mem_info(-2, "MEM before mkidx");

        stype = src->type;
            
            /* type not specified */
        if ((src->flags & PKGSOURCE_TYPE) == 0 || src->type == NULL) 
            source_set_type(src, "dir");
        
        else if (itype && source_is_type(src, itype)) /* the same type */
            source_set_type(src, "dir");

        for (j = 0; j < n_array_size(types); j++) {
            const char *t = n_array_nth(types, j);
            msgn(0, "Making '%s' index of %s (type=%s)...", t,
                 source_idstr(src), src->type);
            if (!source_make_idx(src, t, path, arg_s->crflags))
                nerr++;
            mem_info(-2, "MEM after mkidx");
        }
    }

    if (arg_s->src_mkidx) {
        source_free(arg_s->src_mkidx);
        arg_s->src_mkidx = NULL;
    }

    if (arg_s->idx_type)
        n_cfree(&arg_s->idx_type);
    
    return nerr == 0;
}


static int oprun(struct poclidek_opgroup_rt *rt)
{
    struct arg_s *arg_s;
    int rc = OPGROUP_RC_NIL;
    
    arg_s = rt->_opdata;
    n_assert(arg_s);
    
    if (arg_s->cnflags & DO_MAKEIDX) {
        rc = make_idx(arg_s); 
        rc = rc ? OPGROUP_RC_FINI : OPGROUP_RC_ERROR;
    }

    return rc;
}

