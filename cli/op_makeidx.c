
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

#include "pkgdir/pkgdir.h"

#include "cli.h"
#include "op.h"

#define OPT_GID  1210

#define OPT_MKIDX       (OPT_GID + 1)
#define OPT_MKIDXZ      (OPT_GID + 2) /* legacy */
#define OPT_NODESC      (OPT_GID + 3)
#define OPT_NOCOMPR     (OPT_GID + 4)
#define OPT_TYPE        (OPT_GID + 5)
#define OPT_NODIFF      (OPT_GID + 6)

/* The options we understand. */
static struct argp_option options[] = {
{0,0,0,0, N_("Repository index creation:"), OPT_GID},

{"mkidx", OPT_MKIDX, "FILE", OPTION_ARG_OPTIONAL,
 N_("Create package index (SOURCE-PATH/packages.dir by default)"), OPT_GID},

{"itype", OPT_TYPE, "TYPE", 0,
     N_("Sets the index type (use --stl to list available values)"),
     OPT_GID },

{"mkidxz", OPT_MKIDXZ, "FILE", OPTION_ARG_OPTIONAL | OPTION_HIDDEN,
 N_("Likewise, but gzipped file is created"), OPT_GID},

{"nodesc", OPT_NODESC, 0, 0,
 N_("Don't put package user-level information (like Summary or Description)"
     " into created index."), OPT_GID },

{"nodiff", OPT_NODIFF, 0, 0,
 N_("Don't create index delta files"), OPT_GID },

{"nocompress", OPT_NOCOMPR, 0, OPTION_HIDDEN,
 N_("Create uncompressed index"), OPT_GID },
{ 0, 0, 0, 0, 0, 0 },
};

struct arg_s {
    unsigned            cnflags;
    unsigned            crflags;
    struct poldek_ctx   *ctx;
    struct source       *src_mkidx;
    char                *idx_type;
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
    N_("Repository index creationX:"), 
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
                arg_s->src_mkidx = source_new(arg_s->idx_type, arg, NULL);
            arg_s->cnflags |= DO_MAKEIDX;
            break;
            
        case OPT_NODESC:
            arg_s->crflags |= PKGDIR_CREAT_NODESC;
            break;

        case OPT_NODIFF:
            arg_s->crflags |= PKGDIR_CREAT_NOPATCH;
            break;
            
        case OPT_NOCOMPR:
            arg_s->crflags |= PKGDIR_CREAT_NOCOMPR;
            break;
           
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int make_idx(struct arg_s *arg_s) 
{
    struct source   *src;
    const char      *type = NULL, *path = NULL;
    int i, nerr = 0;

    if (n_array_size(arg_s->ctx->sources) > 1 && arg_s->src_mkidx) {
        logn(LOGERR, _("multiple sources not allowed if index path is specified"));
        return 0;
    }

    if (arg_s->src_mkidx) {
        type = arg_s->src_mkidx->type;
        path = arg_s->src_mkidx->path;
        
    } else {
        type = arg_s->idx_type;
    }
    
    if (type == NULL)
        type = PKGDIR_DEFAULT_TYPE;
    
    for (i=0; i < n_array_size(arg_s->ctx->sources); i++) {
        src = n_array_nth(arg_s->ctx->sources, i);
        if (src->type == NULL)
            source_set_type(src, "dir");
        msgn(0, "Preparing %s...", src->path);
        mem_info(-2, "MEM before mkidx");
        if (!source_make_idx(src, type, path, arg_s->crflags))
            nerr++;
        mem_info(-2, "MEM after mkidx");
    }

    if (arg_s->src_mkidx) {
        source_free(arg_s->src_mkidx);
        arg_s->src_mkidx = NULL;
    }

    if (arg_s->idx_type) {
        free(arg_s->idx_type);
        arg_s->idx_type = NULL;
    }
    
    return nerr == 0;
}


static int oprun(struct poclidek_opgroup_rt *rt)
{
    struct arg_s *arg_s;
    int rc = 1;
    
    arg_s = rt->_opdata;
    n_assert(arg_s);
    
    if (arg_s->cnflags & DO_MAKEIDX)
        rc = make_idx(arg_s);

    return rc ? 0 : OPGROUP_RC_ERROR;
}

