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
#include "poldek_intern.h"
#include "cli.h"
#include "op.h"

#define OPT_SRC_GID  1110

#define OPT_SRC         's'
#define OPT_SRCTXT      (OPT_SRC_GID + 1) /* legacy */
#define OPT_SRCDIR      (OPT_SRC_GID + 2) /* legacy */
#define OPT_SRCHDL      (OPT_SRC_GID + 3) /* legacy */

#define OPT_PKGPREFIX   (OPT_SRC_GID + 4)
#define OPT_SRCTYPE     (OPT_SRC_GID + 5)


#define OPT_SRCUPDATE   (OPT_SRC_GID + 6)
#define OPT_SRCUPDATE_A (OPT_SRC_GID + 7)

#define OPT_SRCCLEAN    (OPT_SRC_GID + 8)
#define OPT_SRCCLEAN_A  (OPT_SRC_GID + 9)

#define OPT_SRCTYPE_LS  (OPT_SRC_GID + 10)

#define OPT_DEST        (OPT_SRC_GID + 11)
#define OPT_DEST_NAME   (OPT_SRC_GID + 12)

/* The options we understand. */
static struct argp_option source_options[] = {
{0,0,0,0, N_("Source repository selection:"), OPT_SRC_GID },
{"source", 's', "PATH", 0, N_("Get packages info from repository under PATH"),
     OPT_SRC_GID },
{"sn", 'n', "SOURCE-NAME", 0,
     N_("Get packages info from repository named SOURCE-NAME"), OPT_SRC_GID },

{"install-dest", OPT_DEST, "PM:SOURCESPEC", 0, 
    N_("Install to specified destination"), OPT_SRC_GID },

{"install-dest-dn", OPT_DEST_NAME, "SOURCE-NAME", 0,
    N_("Install to source SOURCE-NAME instead to system"), OPT_SRC_GID },

{"sidx", OPT_SRC, "FILE", OPTION_HIDDEN, /* legacy */
 N_("Get packages info from package index file FILE"), OPT_SRC_GID },

{"sdir", OPT_SRCDIR, "DIR", OPTION_HIDDEN, 
 N_("Get packages info from directory DIR by scanning it"), OPT_SRC_GID },

{"shdrl", OPT_SRCHDL, "FILE", OPTION_HIDDEN, 
     N_("Get packages info from package header list file (aka hdlist)"),
     OPT_SRC_GID },

{"st", OPT_SRCTYPE, "SOURCE-TYPE", 0,
     N_("Set the source type (use --stl to list available values)"),
     OPT_SRC_GID },

{"prefix", 'P', "PREFIX", 0,
     N_("Get packages from PREFIX instead of SOURCE"), OPT_SRC_GID },

{0,0,0,0, N_("Source related actions:"), OPT_SRC_GID + 1 },
{"stl", OPT_SRCTYPE_LS, 0, 0, N_("List available source types"),
     OPT_SRC_GID + 1},

{"sl", 'l', 0, 0, N_("List configured sources"), OPT_SRC_GID + 1 },            

{"update", OPT_SRCUPDATE, 0, 0, 
 N_("Update the source and verify it"), OPT_SRC_GID + 1 },

{"up", OPT_SRCUPDATE, 0, OPTION_ALIAS, 0, OPT_SRC_GID + 1 }, 

{"update-whole", OPT_SRCUPDATE_A, 0, 0, 
 N_("Update whole index of source"), OPT_SRC_GID + 1 },

{"upa", OPT_SRCUPDATE_A, 0, OPTION_ALIAS, 0, OPT_SRC_GID + 1 },

{"clean", OPT_SRCCLEAN, 0, 0, 
 N_("Remove source index files from cache directory"), OPT_SRC_GID + 1 },

{"clean-whole", OPT_SRCCLEAN_A, 0, 0, 
 N_("Remove all files belongs to source from cache directory"), OPT_SRC_GID + 1 },

{"cleana", OPT_SRCCLEAN_A, 0, OPTION_ALIAS, 0, OPT_SRC_GID + 1 },
{ 0, 0, 0, 0, 0, 0 },
};

#define POLDEKCLI_SRC_SRCLS        (1 << 0)
#define POLDEKCLI_SRC_SRCTYPE_LS   (1 << 1)
#define POLDEKCLI_SRC_UPDATE       (1 << 2)
#define POLDEKCLI_SRC_UPDATEA      (1 << 3)
#define POLDEKCLI_SRC_UPDATE_AUTOA (1 << 4)
#define POLDEKCLI_SRC_CLEAN        (1 << 5)
#define POLDEKCLI_SRC_CLEANA       (1 << 6)

struct arg_s {
    unsigned            cnflags;
    struct poldek_ctx   *ctx;
    struct source       *src;
    struct source       *srcdst;
    char                destpm[32];
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
#if 0
/* PM[:SOURCE|PATH] */
static int parse_destspec(char *spec, struct arg_s *arg_s)
{
    char *p, *q;

    if ((p = strchr(spec, ':')) == NULL) {
        logn(LOGERR, _("%s: invalid destination specified"), spec);
        return 0;
    }
    *p = '\0';

    if (strlen(spec) > sizeof(arg_s->destpm) - 1) {
        logn(LOGERR, _("%s: unknown PM specified"), spec);
        return 0;
    }
    
    q = spec;
    while (*q) {
        if (!isalnum(*q)) {
            logn(LOGERR, _("%s: unknown PM specified"), spec);
            return 0;
        }
        q++;
    }
    n_snprintf(arg_s->destpm, sizeof(arg_s->destpm), "%s", spec);
    
    if (strchr(spec, 
}
#endif

static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct poclidek_opgroup_rt   *rt;
    struct arg_s *arg_s;
    char *source_type = NULL;
    int source_type_isset = 0;

    
    
    rt = state->input;
    if (rt->_opdata) {
        arg_s = rt->_opdata;
        
    } else {
        arg_s = n_malloc(sizeof(*arg_s));
        arg_s->cnflags = 0;
        arg_s->src = NULL;
        arg_s->srcdst = NULL;
        arg_s->destpm[0] = '\0';
        arg_s->curr_src_type = arg_s->curr_src_path = NULL;
        arg_s->ctx = rt->ctx;
        rt->_opdata = arg_s;
        rt->run = oprun;
    }

    //if (key && arg)
    //    poldek_cli_internal_chkargparg(key, arg);
    switch (key) {
        case 'l':
            arg_s->cnflags |= POLDEKCLI_SRC_SRCLS;
            break;

        case OPT_SRCTYPE_LS:
            arg_s->cnflags |= POLDEKCLI_SRC_SRCTYPE_LS;
            break;

        case 'n':
            arg_s->src = source_new(arg, NULL, NULL, NULL);
            poldek_configure(arg_s->ctx, POLDEK_CONF_SOURCE, arg_s->src);
            //arg_s->src = NULL;
            break;

        case OPT_DEST_NAME:
            if (arg_s->srcdst != NULL) {
                logn(LOGERR, _("destination repository is already set"));
                exit(EXIT_FAILURE);
            }
            arg_s->srcdst = source_new(arg, NULL, NULL, NULL);
            break;

        case OPT_SRCTYPE:
            arg_s->curr_src_type = arg;
            break;
            
        case OPT_SRCTXT:     /* no break */
            source_type = NULL; /* guess */
            source_type_isset = 1;
            
        case OPT_SRCDIR:     /* no break */
            if (source_type_isset == 0) {
                source_type = n_strdup("dir");
                source_type_isset = 1;
            }
            
            
        case OPT_SRCHDL:     /* no break */
            if (source_type_isset == 0) {
                source_type = n_strdup("hdrl");
                source_type_isset = 1;
            }

        case 's':
            arg_s->curr_src_path = arg;
            if (arg_s->curr_src_type == NULL)
                arg_s->curr_src_type = source_type;
            
            arg_s->src = source_new_pathspec(arg_s->curr_src_type, arg, NULL);
			poldek_configure(arg_s->ctx, POLDEK_CONF_SOURCE, arg_s->src);
            break;

        case OPT_DEST:
            if (arg_s->destpm[0] != '\0') {
                logn(LOGERR, _("destination is already set"));
                exit(EXIT_FAILURE);
            }
            
            arg_s->curr_src_path = arg;
            if (arg_s->curr_src_type == NULL)
                arg_s->curr_src_type = source_type;
            
            arg_s->srcdst = source_new_pathspec(arg_s->curr_src_type, arg, NULL);
            break;
            
        case 'P':
            if (arg_s->curr_src_path == NULL) {
                logn(LOGERR, _("prefix option should be preceded by source one"));
                exit(EXIT_FAILURE);
                
            } else if (strcmp(arg_s->curr_src_type, "dir") == 0) {
                logn(LOGERR, _("prefix for directory source makes no sense"));
                exit(EXIT_FAILURE);
                
            } else {
                if (arg_s->src->flags & PKGSOURCE_NAMED)
                    logn(LOGERR | LOGDIE, _("poldek's panic"));
                
                if (!source_set_pkg_prefix(arg_s->src, arg))
                    exit(EXIT_FAILURE);
                
                arg_s->curr_src_path = NULL;
                arg_s->curr_src_type = NULL;
            }
            break;

            
        case OPT_SRCUPDATE:
            arg_s->cnflags |= POLDEKCLI_SRC_UPDATE;
            break;

        case OPT_SRCUPDATE_A:
            if (arg_s->cnflags & POLDEKCLI_SRC_UPDATE)
                arg_s->cnflags |= POLDEKCLI_SRC_UPDATE_AUTOA;
            else
                arg_s->cnflags |= POLDEKCLI_SRC_UPDATE;
            arg_s->cnflags |= POLDEKCLI_SRC_UPDATEA;
            break;

        case OPT_SRCCLEAN:
            arg_s->cnflags |= POLDEKCLI_SRC_CLEAN;
            break;

        case OPT_SRCCLEAN_A:
            arg_s->cnflags |= POLDEKCLI_SRC_CLEAN;
            arg_s->cnflags |= POLDEKCLI_SRC_CLEANA;
            break;

        case ARGP_KEY_END:
            if (arg_s->srcdst) {    /* configure as last source */
                poldek_configure(arg_s->ctx, POLDEK_CONF_SOURCE, arg_s->srcdst);
                poldek_configure(arg_s->ctx, POLDEK_CONF_PM, "pset");
            }
            //argp_usage (state);
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


static void print_source_type_list(void) 
{
    int i;
    tn_array *list;

    list = pkgdir_typelist();
    if (list) {
        for (i=0; i < n_array_size(list); i++) {
            char s[64]; int n;
            struct pkgdir_type_uinf *inf = n_array_nth(list, i);
            
            n = snprintf_c(PRCOLOR_GREEN, s, sizeof(s), "%s", inf->name);
            snprintf_c(PRCOLOR_CYAN, &s[n], sizeof(s) - n, "(%s)", inf->mode);
            printf("%-37s", s);
            
            printf(" - %s\n", inf->description);
            if (*inf->aliases) {
                printf("%-12s   (aliases: ", "");
                printf_c(PRCOLOR_GREEN, "%s", inf->aliases);
                printf(")\n");
            }
        }
        n_array_free(list);
    }
    printf("Legend: ");
    printf_c(PRCOLOR_CYAN, "r");
    printf(" - readable, ");
    printf_c(PRCOLOR_CYAN, "w");
    printf(" - writeable, ");
    printf_c(PRCOLOR_CYAN, "u");
    printf(" - updateable\n");
}


static int oprun(struct poclidek_opgroup_rt *rt)
{
    struct arg_s *arg_s;
    int rc = OPGROUP_RC_NIL;
    
    arg_s = rt->_opdata;
    n_assert(arg_s);

    if (arg_s->cnflags & POLDEKCLI_SRC_CLEAN) {
        unsigned flags = PKGSOURCE_CLEAN;
        if (arg_s->cnflags & POLDEKCLI_SRC_CLEANA)
            flags |= PKGSOURCE_CLEANA;
        sources_clean(rt->ctx->sources, flags);
        rc |= OPGROUP_RC_OK;
    }

    if (arg_s->cnflags & POLDEKCLI_SRC_SRCTYPE_LS) {
        rc |= OPGROUP_RC_FINI;
        print_source_type_list();
    }

    if (arg_s->cnflags & POLDEKCLI_SRC_SRCLS) {
        rc |= OPGROUP_RC_FINI;
        print_source_list(rt->ctx->sources);
    }

    if (arg_s->cnflags & POLDEKCLI_SRC_UPDATE) {
        unsigned flags = PKGSOURCE_UP;
        
        if (arg_s->cnflags & POLDEKCLI_SRC_UPDATEA)
            flags |= PKGSOURCE_UPA;

        if (arg_s->cnflags & POLDEKCLI_SRC_UPDATE_AUTOA)
            flags |= PKGSOURCE_UPAUTOA;

        if (!sources_update(rt->ctx->sources, flags)) {
            rc |= OPGROUP_RC_ERROR | OPGROUP_RC_IFINI;
        }

        rc |= OPGROUP_RC_FINI;
    }

    
    DBGF("op_source %d\n", rc);
    return rc;
}

