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

#include <string.h>
#include <sys/param.h>
#include <time.h>

#include <trurl/narray.h>

#include "compiler.h"
#include "pkgdir/source.h"
#include "poldek_util.h"
#include "i18n.h"
#include "pkgu.h"
#include "cli.h"
#include "log.h"

void poclidek__print_source_list(struct poldek_ctx *ctx, tn_array *sources,
                                 int print_groups);

static int pull(struct cmdctx *cmdctx);
static error_t parse_opt(int key, char *arg, struct argp_state *state);

static struct argp_option options[] = {
  { 0, 'l', 0, 0, N_("List repos"), OPT_GID_OP_UP + 1 }, /* just for convinience */
  { 0, 0, 0, 0, 0, 0 }
};

struct poclidek_cmd command_pull = {
    COMMAND_SELFARGS | COMMAND_HASVERBOSE | COMMAND_EMPTYARGS | COMMAND_BATCH,
    "up", N_("REPO..."), N_("Update list of available packages"),
    options, parse_opt,
    NULL, pull, NULL, NULL, NULL, NULL, NULL, 0, 0,
    NULL
};

#define ACTION_UPDATE 1
#define ACTION_LIST   2

struct arg_s {
    unsigned            action;
    tn_array            *repos;
};

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdctx *cmdctx = state->input;
    struct arg_s *arg_s;

    if (cmdctx->_data) {
        arg_s = cmdctx->_data;

    } else {
        arg_s = n_malloc(sizeof(*arg_s));
        memset(arg_s, 0, sizeof(*arg_s));
        arg_s->action = ACTION_UPDATE;
        cmdctx->_data = arg_s;
    }

    switch (key) {
        case 'l':
            arg_s->action = ACTION_LIST;
            break;

        case ARGP_KEY_ARG:
            if (arg_s->repos == NULL)
                arg_s->repos = n_array_new(8, free, (tn_fn_cmp)strcmp);

            n_array_push(arg_s->repos, n_strdup(arg));
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
}

static int pull(struct cmdctx *cmdctx)
{
    tn_array *sources = poldek_get_sources(cmdctx->cctx->ctx);
    struct arg_s *args = cmdctx->_data;
    int nerr = 0;

    if (args->action == ACTION_LIST) {
        poclidek__print_source_list(cmdctx->cctx->ctx, sources, 1);

    } else {
        if (args->repos)
            n_array_sort(args->repos);

        for (int i=0; i < n_array_size(sources); i++) {
            struct source *src = n_array_nth(sources, i);

            if (args->repos && n_array_bsearch(args->repos, src->name) == NULL) {
                continue;
            } else if (src->flags & PKGSOURCE_NOAUTOUP) {
                continue;
            }

            if (!source_update(src, PKGSOURCE_UP | PKGSOURCE_UPAUTOA))
                nerr++;
        }

        if (args->repos)
            n_array_free(args->repos);
    }


    n_array_free(sources);
    return nerr == 0;
}
