/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

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
#include <trurl/nstr.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"

#include "poldek.h"
#include "pkgdir/source.h"
#include "pkgdir/pkgdir.h"
#include "conf.h"
#include "cli.h"
#include "op.h"

#define OPT_GID  OPT_GID_OP_SOURCE

#define OPT_SRC         's'
#define OPT_SRCTXT      (OPT_GID + 1) /* legacy */
#define OPT_SRCDIR      (OPT_GID + 2) /* legacy */
#define OPT_SRCHDL      (OPT_GID + 3) /* legacy */

#define OPT_PKGPREFIX   (OPT_GID + 4)
#define OPT_SRCTYPE     (OPT_GID + 5)


#define OPT_SRCUPDATE   (OPT_GID + 6)
#define OPT_SRCUPDATE_A (OPT_GID + 7)

#define OPT_SRCCLEAN     (OPT_GID + 8)
#define OPT_SRCCLEAN_PKG (OPT_GID + 9)
#define OPT_SRCCLEAN_ALL (OPT_GID + 10)

#define OPT_SRCTYPE_LS  (OPT_GID + 11)

#define OPT_DEST        (OPT_GID + 12)
#define OPT_DEST_TYPE   (OPT_GID + 13)

#define OPT_DEST_NAME   (OPT_GID + 14)

/* The options we understand. */
static struct argp_option source_options[] = {
{0,0,0,0, N_("Repository selection:"), OPT_GID },
{"source", 's', "PATH", 0, N_("Get packages info from repository under PATH"),
     OPT_GID },
{"sn", 'n', "SOURCE-NAME", 0,
     N_("Get packages info from repository named SOURCE-NAME"), OPT_GID },


{"dt", OPT_DEST_TYPE, "SOURCE-TYPE", 0,
       N_("Set the type of index specified by --destination option"),
       OPT_GID },

{"destination", OPT_DEST, "PATH", 0,
    N_("Install to specified destination"), OPT_GID },


{"dn", OPT_DEST_NAME, "SOURCE-NAME", 0,
    N_("Install to source SOURCE-NAME instead to the system"), OPT_GID },

{"sidx", OPT_SRC, "FILE", OPTION_HIDDEN, /* legacy */
 N_("Get packages info from package index file FILE"), OPT_GID },

{"sdir", OPT_SRCDIR, "DIR", OPTION_HIDDEN,
 N_("Get packages info from directory DIR by scanning it"), OPT_GID },

{"st", OPT_SRCTYPE, "SOURCE-TYPE", 0,
       N_("Set the source type (use --stl to list available values)"),
       OPT_GID },

{"prefix", 'P', "PREFIX", 0,
        N_("Get packages from PREFIX instead of SOURCE"), OPT_GID },

{0,0,0,0, N_("Repository related actions:"), OPT_GID + 1 },
{"stl", OPT_SRCTYPE_LS, 0, 0, N_("List available source types"),
     OPT_GID + 1},

{"sl", 'l', 0, 0, N_("List configured sources"), OPT_GID + 1 },

{"update", OPT_SRCUPDATE, 0, 0,
 N_("Update the source and verify it"), OPT_GID + 1 },

{"up", OPT_SRCUPDATE, 0, OPTION_ALIAS, 0, OPT_GID + 1 },

{"update-whole", OPT_SRCUPDATE_A, 0, 0,
 N_("Update whole index of source"), OPT_GID + 1 },

{"upa", OPT_SRCUPDATE_A, 0, OPTION_ALIAS, 0, OPT_GID + 1 },

{"clean", OPT_SRCCLEAN, 0, 0,
 N_("Remove source local cache"), OPT_GID + 1 },

{"clean-pkg", OPT_SRCCLEAN_PKG, 0, OPTION_HIDDEN,
 N_("Remove cached packages of the source"), OPT_GID + 1 },

{"clean-whole", OPT_SRCCLEAN_ALL, 0, OPTION_HIDDEN,
 N_("Remove all files belongs to source from cache directory"), OPT_GID + 1 },

{"cleana", OPT_SRCCLEAN_ALL, 0, OPTION_ALIAS|OPTION_HIDDEN, 0, OPT_GID + 1 },
{ 0, 0, 0, 0, 0, 0 },
};

#define POLDEKCLI_SRC_SRCLS        (1 << 0)
#define POLDEKCLI_SRC_SRCTYPE_LS   (1 << 1)
#define POLDEKCLI_SRC_UPDATE       (1 << 2)
#define POLDEKCLI_SRC_UPDATEA      (1 << 3)
#define POLDEKCLI_SRC_UPDATE_AUTOA (1 << 4)
#define POLDEKCLI_SRC_CLEAN        (1 << 5)
#define POLDEKCLI_SRC_CLEAN_PKG    (1 << 6)
#define POLDEKCLI_SRC_SPECIFIED    (1 << 10) /* any -s or -n */

/* TODO: there is no way to get other group parameters */
int poclidek_op_source_nodesc = 0;

struct arg_s {
    unsigned            cnflags;
    struct poldek_ctx   *ctx;
    struct source       *src;

    struct source       *srcdst; /* temporary --dn arg */

    struct source       *destination; /* --destination */
    char                *dt;          /* --dt */

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
    &poclidek_source_argp, 0, NULL, OPT_GID,
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
    struct poclidek_opgroup_rt   *rt;
    struct arg_s *arg_s;
    char *source_type = NULL;
    int source_type_isset = 0;

    rt = state->input;
    if (rt->_opdata) {
        arg_s = rt->_opdata;

    } else {
        arg_s = n_malloc(sizeof(*arg_s));
        memset(arg_s, 0, sizeof(*arg_s));

        arg_s->cnflags = 0;
        arg_s->src = NULL;
        arg_s->srcdst = NULL;
        arg_s->destination = NULL;
        arg_s->dt = NULL;
        arg_s->curr_src_type = arg_s->curr_src_path = NULL;
        arg_s->ctx = rt->ctx;
        rt->_opdata = arg_s;
        rt->_opdata_free = free;
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
            arg_s->cnflags |= POLDEKCLI_SRC_SPECIFIED;
            //arg_s->src = NULL;
            break;

        case OPT_DEST_NAME:
            arg_s->srcdst = source_new(arg, NULL, NULL, NULL);
            poldek_configure(arg_s->ctx, POLDEK_CONF_DESTINATION, arg_s->srcdst);
            poldek_configure(arg_s->ctx, POLDEK_CONF_PM, "pset");
            arg_s->srcdst = NULL;
            break;

        case OPT_SRCTYPE:
            arg_s->curr_src_type = arg;
            break;

        case OPT_SRCTXT:     /* no break */
            source_type = NULL; /* guess */
            source_type_isset = 1;
            /* fallthru */

        case OPT_SRCDIR:     /* no break */
            if (source_type_isset == 0) {
                source_type = n_strdup("dir");
                source_type_isset = 1;
            }
            /* fallthru */
        case 's':
            arg_s->curr_src_path = arg;
            if (arg_s->curr_src_type == NULL)
                arg_s->curr_src_type = source_type;

            arg_s->src = source_new_pathspec(arg_s->curr_src_type, arg, NULL);
			poldek_configure(arg_s->ctx, POLDEK_CONF_SOURCE, arg_s->src);
            arg_s->cnflags |= POLDEKCLI_SRC_SPECIFIED;
            break;

        case OPT_DEST_TYPE:
            if (arg_s->destination) {
                logn(LOGERR, _("--dt: destination is already set, "
                               "use me before --destination"));
                exit(EXIT_FAILURE);
            }
            arg_s->dt = arg;
            break;

        case OPT_DEST:
            if (arg_s->destination) {
                logn(LOGERR, _("--destination: destination is already set"));
                exit(EXIT_FAILURE);
            }

            arg_s->destination = source_new_pathspec(arg_s->dt, arg, NULL);
            poldek_configure(arg_s->ctx, POLDEK_CONF_DESTINATION, arg_s->destination);
            poldek_configure(arg_s->ctx, POLDEK_CONF_PM, "pset");
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
            arg_s->cnflags |= POLDEKCLI_SRC_CLEAN | POLDEKCLI_SRC_CLEAN_PKG;
            break;

        case OPT_SRCCLEAN_PKG:
            arg_s->cnflags |= POLDEKCLI_SRC_CLEAN_PKG;
            break;

        case OPT_SRCCLEAN_ALL:
            arg_s->cnflags |= POLDEKCLI_SRC_CLEAN | POLDEKCLI_SRC_CLEAN_PKG;
            break;

        case ARGP_KEY_END:
            //argp_usage (state);
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static
void print_source_list(struct poldek_ctx *ctx, tn_array *sources,
                       int print_groups)
{
    int i;
    tn_hash *htcnf;
    tn_array *htcnf_sources;

    n_array_sort_ex(sources, (tn_fn_cmp)source_cmp_name);
    for (i=0; i < n_array_size(sources); i++)
        source_printf(n_array_nth(sources, i));
    n_array_sort(sources);

    if (print_groups == 0)
        return;

    if ((htcnf = poldek_get_config(ctx)) == NULL)
        return;

    if ((htcnf_sources = poldek_conf_get_sections(htcnf, "source")) == NULL)
        return;

    for (i=0; i < n_array_size(htcnf_sources); i++) {
        tn_hash *ht = n_array_nth(htcnf_sources, i);
        const char *type;

        type = poldek_conf_get(ht, "type", NULL);
        if (type && n_str_eq(type, source_TYPE_GROUP)) {
            struct source *src = source_new_htcnf(ht);
            if (src == NULL)
                continue;
            source_printf(src);
            source_free(src);
        }
    }
}


static void print_source_type_list(void)
{
    int i;
    tn_array *list;

    list = pkgdir_typelist();
    if (list) {
        for (i=0; i < n_array_size(list); i++) {
            char ns[32], ms[32];
            struct pkgdir_type_uinf *inf = n_array_nth(list, i);

            poldek_term_snprintf_c(PRCOLOR_GREEN, ns, sizeof(ns),
                                   "%s", inf->name);
            poldek_term_snprintf_c(PRCOLOR_CYAN, ms, sizeof(ms),
                                   "%s", inf->mode);

            printf("%-22s%s", ns, ms);
            printf("  %s\n", inf->description);
            if (*inf->aliases) {
                printf("%-10s   (aliases: ", "");
                poldek_term_printf_c(PRCOLOR_GREEN, "%s", inf->aliases);
                printf(")\n");
            }
        }
        n_array_free(list);
    }
    printf("Legend: ");
    poldek_term_printf_c(PRCOLOR_CYAN, "r");
    printf(" - readable, ");
    poldek_term_printf_c(PRCOLOR_CYAN, "w");
    printf(" - writeable, ");
    poldek_term_printf_c(PRCOLOR_CYAN, "u");
    printf(" - updateable\n");
}


static int oprun(struct poclidek_opgroup_rt *rt)
{
    struct arg_s *arg_s;
    tn_array *sources;
    int rc = OPGROUP_RC_NIL;

    arg_s = rt->_opdata;
    n_assert(arg_s);

    sources = poldek_get_sources(rt->ctx);

    if (sources && poclidek_op_source_nodesc) {
        int i;
        for (i=0; i < n_array_size(sources); i++) {
            struct source *src = n_array_nth(sources, i);
            src->flags |= PKGSOURCE_NODESC;
        }
    }
    poclidek_op_source_nodesc = 0;

    if (arg_s->cnflags & (POLDEKCLI_SRC_CLEAN | POLDEKCLI_SRC_CLEAN_PKG)) {
        unsigned flags = 0;

        if (arg_s->cnflags & POLDEKCLI_SRC_CLEAN)
            flags |= PKGSOURCE_CLEAN;

        if (arg_s->cnflags & POLDEKCLI_SRC_CLEAN_PKG)
            flags |= PKGSOURCE_CLEANPKG;

        if ((arg_s->cnflags & POLDEKCLI_SRC_SPECIFIED) == 0)
            flags |= PKGSOURCE_CLEAN_WHOLE_CACHEDIR;

        if (rt->ts->getop(rt->ts, POLDEK_OP_TEST))
            flags |= PKGSOURCE_CLEAN_TEST;

        sources_clean(sources, flags);
        rc = OPGROUP_RC_OK;
    }

    if (arg_s->cnflags & POLDEKCLI_SRC_SRCTYPE_LS) {
        rc = OPGROUP_RC_OK;
        print_source_type_list();
    }

    if (arg_s->cnflags & POLDEKCLI_SRC_SRCLS) {
        rc = OPGROUP_RC_OK;
        print_source_list(rt->ctx, sources,
                          (arg_s->cnflags & POLDEKCLI_SRC_SPECIFIED) == 0);
                          /* print source groups if no -n or -s */
    }

    if (arg_s->cnflags & POLDEKCLI_SRC_UPDATE) {
        unsigned flags = PKGSOURCE_UP;

        if (arg_s->cnflags & POLDEKCLI_SRC_UPDATEA)
            flags |= PKGSOURCE_UPA;

        if (arg_s->cnflags & POLDEKCLI_SRC_UPDATE_AUTOA)
            flags |= PKGSOURCE_UPAUTOA;

        if (sources_update(sources, flags))
            rc = OPGROUP_RC_OK;
        else
            rc = OPGROUP_RC_ERROR;
    }

    if (sources)
        n_array_free(sources);

    DBGF("op_source %d\n", rc);
    return rc;
}
