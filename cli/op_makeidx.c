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

#define OPT_GID  OPT_GID_OP_MAKEIDX

#define OPT_MKIDX       (OPT_GID + 1)
#define OPT_MAKEIDX     (OPT_GID + 2)
#define OPT_MKIDXZ      (OPT_GID + 3) /* legacy */
#define OPT_NODESC      (OPT_GID + 4)
#define OPT_COMPR       (OPT_GID + 5)
#define OPT_NOCOMPR     (OPT_GID + 6)
#define OPT_TYPE        (OPT_GID + 7)
#define OPT_TYPE_ALIAS  (OPT_GID + 11) /* XXX argp bug? with +8 this doesn't work... */
#define OPT_NODIFF      (OPT_GID + 9)
#define OPT_MOPT        (OPT_GID + 10)

/* The options we understand. */
static struct argp_option options[] = {
{0,0,0,0, N_("Repository index creation:"), OPT_GID},

{"mkidx", OPT_MKIDX, "PATH", OPTION_ARG_OPTIONAL,
 N_("Create package index (under SOURCE-PATH by default)"), OPT_GID},

{"makeidx", OPT_MAKEIDX, 0, OPTION_ALIAS, 0, OPT_GID }, 

{"mt", OPT_TYPE, "TYPE[,TYPE]", 0,
     N_("Set created index type (use --stl to list available values)"),
     OPT_GID },

{"mkidx-type", OPT_TYPE_ALIAS, 0, OPTION_ALIAS, 0, 0 },

{"mkidxz", OPT_MKIDXZ, "PATH", OPTION_ARG_OPTIONAL | OPTION_HIDDEN,
 N_("Likewise, but gzipped file is created"), OPT_GID},

{"nodesc", OPT_NODESC, 0, OPTION_HIDDEN, "", OPT_GID },
{"nodiff", OPT_NODIFF, 0, OPTION_HIDDEN, "", OPT_GID },
{"nocompress", OPT_NOCOMPR, NULL, OPTION_HIDDEN, "", OPT_GID },

{"mo", OPT_MOPT, "OPTION[,OPTION]", 0, 
     N_("Create options (type --mo=help for help)"), OPT_GID },
{ 0, 0, 0, 0, 0, 0 },
};

struct mopt {
    char *name;
    unsigned flag;
    char *doc;
};

static struct mopt valid_mopts[] = {
    {
        "nodesc", PKGDIR_CREAT_NODESC, 
        N_("Omit package user-level information (like Summary or Description)")
    },
    
    { "nodiff", PKGDIR_CREAT_NOPATCH, N_("Don't create index delta files") },
    
    { "v018x", PKGDIR_CREAT_v018x, /* pdir without pkg files timestamps */
      N_("Create pdir compatibile with versions prior 0.18.9")},
    
    { "nocompress", 0, N_("Create uncompressed index") },
    { "compress", 0, NULL }, /* compress=[gz,bz2,none] - a compression type, NFY */
    { "help", 0, NULL},
    { NULL, 0, 0 },
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


static int parse_mopts(struct arg_s *arg_s, char *opstr);

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

static void log_modeprecated(const char *opname)
{
    logn(LOGNOTICE, _("--%s is deprecated, use --mo=%s"), opname, opname);
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
        case OPT_TYPE_ALIAS:
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
            log_modeprecated("nodesc");
            parse_mopts(arg_s, "nodesc");
            /* XXX hack, no way to pass option between argps (?)*/
            poclidek_op_source_nodesc = 1;
            break;

        case OPT_NODIFF:
            log_modeprecated("nodiff");
            parse_mopts(arg_s, "nodiff");
            break;
            
        case OPT_COMPR: {
            char tmp[128];
            log_modeprecated("compress");
            n_snprintf(tmp, sizeof(tmp), "compress=%s", arg);
            parse_mopts(arg_s, tmp);
            break;
        }

        case OPT_NOCOMPR:
            log_modeprecated("nocompress");
            parse_mopts(arg_s, "nocompress");
            break;

        case OPT_MOPT:
            if (!parse_mopts(arg_s, arg))
                return ARGP_ERR_UNKNOWN;
            break;
            
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int parse_mopt(struct arg_s *arg_s, const char *opstr)
{
    char *p, *tmp;
    int i, valid;
    
    n_strdupap(opstr, &tmp);
    
    if ((p = strchr(tmp, '='))) {
        *p = 0;
        p++;
        p = n_str_strip_ws(p);
        tmp = n_str_strip_ws(tmp);
    }
    
    n_hash_replace(arg_s->opts, tmp, p ? n_strdup(p) : NULL);
        
    i = 0;
    valid = 0;
    while (valid_mopts[i].name) {
        if (n_str_eq(tmp, valid_mopts[i].name)) {
            arg_s->crflags |= valid_mopts[i].flag;
            valid = 1;
            break;
        }
        i++;
    }
    
    if (!valid) {
        logn(LOGERR, _("%s: unknown option"), tmp);
    
    } else {
        if (n_str_eq(tmp, "nocompress"))
            n_hash_replace(arg_s->opts, "compress", n_strdup("none"));
        else 
            n_hash_replace(arg_s->opts, tmp, p ? n_strdup(p) : NULL);
    }
    
    return valid;
}


static int parse_mopts(struct arg_s *arg_s, char *opstr) 
{
    opstr = n_str_strip_ws(opstr);

    if (opstr == NULL || *opstr == '\0')
        return 1;
    
    if (strchr(opstr, ',') == NULL) {
        parse_mopt(arg_s, opstr);
        
    } else {
        const char **tl_save, **tl;
        
        tl = tl_save = n_str_tokl(opstr, ",");
        while (*tl) {
            parse_mopt(arg_s, *tl);
            tl++;
        }
        n_str_tokl_free(tl_save);
    }
    
    return 1;
}

void help_mopts(void)
{
    int i = 0;
    printf(_("Index create options are:\n"));
    while (valid_mopts[i].doc) {
        printf("  %-12s", valid_mopts[i].name);
        printf("%s\n", valid_mopts[i].doc);
        i++;
    }
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


/*
  Index creation use cases:
  a) -s /foo                  =>  dir  -> default type
  b) --st type -s /foo        =>  type -> default type
  c) -s /foo --mt dtype       =>  dir  -> dtype
  d) --st type /foo --mt type =>  type -> dtype
  e) -n foo                   =>  dir (or original type) -> foo's type
*/
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
        logn(LOGERR,
             _("multiple sources not allowed if index path is specified"));
        nerr++;
        goto l_end;
    }

    if (arg_s->src_mkidx)
        path = arg_s->src_mkidx->path;

    if (arg_s->idx_type)
        types = parse_types(arg_s->idx_type);

    arg_s->crflags |= PKGDIR_CREAT_IFORIGCHANGED;
    for (i=0; i < n_array_size(sources); i++) {
        src = n_array_nth(sources, i);
        DBGF("src %s type=%s\n", src->path, src->type);
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

    if (n_hash_exists(arg_s->opts, "help")) { /* --mo=help */
        help_mopts();
        rc = OPGROUP_RC_OK;
    }

    if (arg_s->cnflags & DO_MAKEIDX) {
        rc = make_idx(arg_s); 
        rc = rc ? OPGROUP_RC_OK : OPGROUP_RC_ERROR;
    }

    return rc;
}

