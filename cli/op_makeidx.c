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

#define OPT_GID  1700

#define OPT_MKIDX       (OPT_GID + 1)
#define OPT_MAKEIDX     (OPT_GID + 2)
#define OPT_MKIDXZ      (OPT_GID + 3) /* legacy */
#define OPT_NODESC      (OPT_GID + 4)
#define OPT_COMPR       (OPT_GID + 5)
#define OPT_NOCOMPR     (OPT_GID + 6)
#define OPT_TYPE        (OPT_GID + 7)
#define OPT_NODIFF      (OPT_GID + 8)

/* The options we understand. */
static struct argp_option options[] = {
{0,0,0,0, N_("Repository index creation:"), OPT_GID},

{"mkidx", OPT_MKIDX, "PATH", OPTION_ARG_OPTIONAL,
 N_("Create package index (SOURCE-PATH/packages.dir by default)"), OPT_GID},

{"makeidx", OPT_MAKEIDX, 0, OPTION_ALIAS, 0, OPT_GID }, 

{"mkidx-type", OPT_TYPE, "TYPE", 0,
     N_("Sets the index type (use --stl to list available values)"),
     OPT_GID },

{"mkidxz", OPT_MKIDXZ, "PATH", OPTION_ARG_OPTIONAL | OPTION_HIDDEN,
 N_("Likewise, but gzipped file is created"), OPT_GID},

{"nodesc", OPT_NODESC, 0, 0,
 N_("Don't put package user-level information (like Summary or Description)"
     " into created index."), OPT_GID },

{"nodiff", OPT_NODIFF, 0, 0,
 N_("Don't create index delta files"), OPT_GID },

{"nocompress", OPT_NOCOMPR, NULL, 0, 
 N_("Create uncompressed index"), OPT_GID },

{"compress", OPT_COMPR, "type", OPTION_HIDDEN, /* not finished yet */
 N_("Sets compression type (none, bz2, gz)"), OPT_GID },
{ 0, 0, 0, 0, 0, 0 },


{ 0, 0, 0, 0, 0, 0 },    
};

struct arg_s {
    unsigned            cnflags;
    unsigned            crflags;
    struct poldek_ctx   *ctx;
    struct source       *src_mkidx;
    char                *idx_type;
    tn_hash             *opts;
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
    "", 
    &poclidek_argp, 
    &poclidek_argp_child,
    oprun,
};

static void arg_s_free(void *a) 
{
    struct arg_s *arg_s = a;
    
    if (arg_s->src_mkidx) {
        source_free(arg_s->src_mkidx);
        arg_s->src_mkidx = NULL;
    }

    n_cfree(&arg_s->idx_type);
    n_hash_free(arg_s->opts);
    free(arg_s);
}

extern int poclidek_op_source_nodesc;

static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct poclidek_opgroup_rt   *rt;
    struct arg_s *arg_s;
    
    
    rt = state->input;
    if (rt->_opdata) {
        arg_s = rt->_opdata;
        
    } else {
        arg_s = n_calloc(1, sizeof(*arg_s));
        arg_s->crflags = arg_s->cnflags = 0;
        arg_s->idx_type = NULL;
        arg_s->src_mkidx = NULL;
        arg_s->opts = n_hash_new(16, free);
        arg_s->ctx = rt->ctx;
        rt->_opdata = arg_s;
        rt->_opdata_free = arg_s_free;
        rt->run = oprun;
    }
    DBGF("key %d\n", key);
    
    switch (key) {
        case OPT_TYPE:
            arg_s->idx_type = n_strdup(arg);
            break;
            
        case OPT_MKIDXZ:
        case OPT_MKIDX:
        case OPT_MAKEIDX:
            if (arg)
                arg_s->src_mkidx = source_new_pathspec(NULL, arg, NULL);
            arg_s->cnflags |= DO_MAKEIDX;
            break;
            
        case OPT_NODESC:
            arg_s->crflags |= PKGDIR_CREAT_NODESC;
            /* hack, no way to pass option between argps (?)*/
            poclidek_op_source_nodesc = 1;
            break;

        case OPT_NODIFF:
            arg_s->crflags |= PKGDIR_CREAT_NOPATCH;
            break;
            
        case OPT_COMPR:
            n_hash_replace(arg_s->opts, "compress", n_strdup(arg));
            break;

        case OPT_NOCOMPR:
            n_hash_replace(arg_s->opts, "compress", n_strdup("none"));
            break;
           
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}

static tn_array *parse_types(const char *type) 
{
    tn_array *types = n_array_new(4, free, (tn_fn_cmp)strcmp);

    if (type == NULL)
        return types;

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
    n_array_sort(types);
    n_array_uniq(types);
    return types;
}


static int make_idx(struct arg_s *arg_s) 
{
    struct source   *src;
    const char      *path = NULL;
    tn_array        *sources, *types = NULL;
    int i, j, nerr = 0;

    sources = poldek_get_sources(arg_s->ctx);
    if (sources == NULL || n_array_size(sources) == 0) {
        logn(LOGERR, _("no sources specified"));
        nerr++;
        goto l_end;
    }
    
    if (n_array_size(sources) > 1 && arg_s->src_mkidx) {
        logn(LOGERR, _("multiple sources not allowed if index path is specified"));
        nerr++;
        goto l_end;
    }

    if (arg_s->src_mkidx)
        path = arg_s->src_mkidx->path;

    if (arg_s->idx_type)
        types = parse_types(arg_s->idx_type);
    
    for (i=0; i < n_array_size(sources); i++) {
        src = n_array_nth(sources, i);
        MEMINF("before mkidx");

        if (types == NULL) {     /* no types  */
            msgn(3, "Making index of %s (type=%s)...", source_idstr(src),
                 src->type);
            if (!source_make_idx(src, NULL, NULL, path, arg_s->crflags,
                                 arg_s->opts))
                nerr++;
            
        } else
            for (j = 0; j < n_array_size(types); j++) {
                const char *dtype = n_array_nth(types, j);
                msgn(3, "Making '%s' index of %s (type=%s)...", dtype,
                     source_idstr(src), src->type);
                MEMINF("before");
                if (!source_make_idx(src, NULL, dtype, path, arg_s->crflags,
                                     arg_s->opts))
                    nerr++;
                MEMINF("after");
            }
    }

    if (arg_s->src_mkidx) {
        source_free(arg_s->src_mkidx);
        arg_s->src_mkidx = NULL;
    }

 l_end:

    if (sources)
        n_array_free(sources);
    
    if (types)
        n_array_free(types);
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

