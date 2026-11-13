/*
  Copyright (C) 2026 PLD Linux Team <feedback@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <unistd.h>

#include <trurl/narray.h>
#include <trurl/nmalloc.h>
#include <trurl/nstr.h>
#include <vfile/vfile.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "conf.h"
#include "misc.h"
#include "repo.h"
#include "pkgdir/source.h"
#include "cli.h"
#include "op.h"

#define OPT_GID  OPT_GID_OP_OTHER

#define OPT_FORCE       (OPT_GID + 1)

#define ACTION_NONE     0
#define ACTION_ADD      1
#define ACTION_DEL      2
#define ACTION_LIST     3

static struct argp_option options[] = {
    { "force", OPT_FORCE, 0, 0, N_("Overwrite an existing .conf file"), 0 },
    { 0, 0, 0, 0, 0, 0 }
};

static struct {
    const char  *name;
    unsigned    action;
} subcommands[] = {
    { "add",    ACTION_ADD },
    { "del",    ACTION_DEL },
    { "rm",     ACTION_DEL },
    { "ls",     ACTION_LIST },
    { "list",   ACTION_LIST },
    { NULL,     ACTION_NONE },
};

struct arg_s {
    unsigned    action;
    char        *url;
    int         force;
};

static void arg_s_free(void *ptr)
{
    struct arg_s *arg_s = ptr;

    if (arg_s->url)
        n_free(arg_s->url);

    n_free(arg_s);
}

static const char *action_name(unsigned action)
{
    switch (action) {
        case ACTION_ADD: return "add";
        case ACTION_DEL: return "del";
        case ACTION_LIST: return "ls";
    }

    return "?";
}

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdctx *cmdctx = state->input;
    struct arg_s *arg_s;
    int i;

    if (cmdctx->_data) {
        arg_s = cmdctx->_data;

    } else {
        arg_s = n_malloc(sizeof(*arg_s));
        memset(arg_s, 0, sizeof(*arg_s));
        cmdctx->_data = arg_s;
    }

    switch (key) {
        case ARGP_KEY_ARG:
            if (arg_s->action == ACTION_NONE) {     /* the first word */
                for (i = 0; subcommands[i].name; i++)
                    if (strcmp(arg, subcommands[i].name) == 0)
                        break;

                if (subcommands[i].name == NULL) {
                    logn(LOGERR, _("repo: unknown command '%s'"), arg);
                    goto l_err;
                }

                arg_s->action = subcommands[i].action;
                return 0;
            }

            if (arg_s->action == ACTION_LIST) {
                logn(LOGERR, _("repo: %s takes no arguments"), action_name(arg_s->action));
                goto l_err;
            }

            if (arg_s->url) {
                logn(LOGERR, _("repo: %s takes one URL"),
                     action_name(arg_s->action));
                goto l_err;
            }

            arg_s->url = n_strdup(arg);
            return 0;

        case ARGP_KEY_END:
            if (cmdctx->rtflags & CMDCTX_ISHELP)
                break;

            if (arg_s->action == ACTION_NONE)   /* repo == repo ls */
                arg_s->action = ACTION_LIST;

            if (arg_s->force && arg_s->action != ACTION_ADD) {
                logn(LOGERR, _("repo: --force is only for add"));
                goto l_err;
            }

            if (arg_s->action == ACTION_LIST) {
                if (arg_s->url) {
                    logn(LOGERR, _("repo: ls takes no arguments"));
                    goto l_err;
                }

            } else if (arg_s->url == NULL) {
                logn(LOGERR, _("repo: %s: a URL is required"),
                     action_name(arg_s->action));
                goto l_err;
            }
            break;

        case OPT_FORCE:
            arg_s->force = 1;
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;

 l_err:
    cmdctx->rtflags |= CMDCTX_ERR;
    return EINVAL;
}

static int list_repos(struct poldek_ctx *ctx)
{
    char reposd[PATH_MAX];
    tn_array *files;
    int i, nerr = 0;

    (void)ctx;

    if (poldek_conf_reposd(reposd, sizeof(reposd)) == NULL)
        return 1;

    if (access(reposd, R_OK | X_OK) != 0) {
        if (errno == ENOENT)
            return 1;
        logn(LOGWARN, _("%s: %m"), reposd);
        return 0;
    }

    files = poldek_util_readdir(reposd, "*.conf");

    for (i = 0; i < n_array_size(files); i++) {
        const char *path = n_array_nth(files, i);
        tn_hash *htconf;
        tn_array *sources_ht;
        tn_array *sources = NULL;
        int j;

        htconf = poldek_conf_load(path, POLDEK_LDCONF_NOVALIDATE |
                                  POLDEK_LDCONF_NOINCLUDE);
        if (htconf == NULL) {
            logn(LOGERR, _("%s: failed to load"), path);
            nerr++;
            continue;
        }

        tn_hash *ht_global = poldek_conf_get_section(htconf, "global");
        const char *origin = ht_global ? poldek_conf_get(ht_global, "_origin", NULL) : NULL;
        char origin_url[PATH_MAX];
        if (!origin)
            origin = n_basenam(path);

        /* the URL is what the repository is known by, a password in it is
           not for the screen */
        poldek_term_printf_c(PRCOLOR_CYAN, "%s\n",
                             vf_url_hidepasswd(origin_url, sizeof(origin_url),
                                               origin));

        sources_ht = poldek_conf_get_sections(htconf, "source");
        if (sources_ht) {
            sources = n_array_new(4, (tn_fn_free)source_free, NULL);
            for (j = 0; j < n_array_size(sources_ht); j++) {
                tn_hash *ht = n_array_nth(sources_ht, j);
                struct source *src = source_new_htcnf(ht);
                if (src)
                    n_array_push(sources, src);
            }
        }

        if (sources && n_array_size(sources) > 0)
            poclidek__print_source_list(ctx, sources, 0);

        n_array_cfree(&sources);
        n_hash_free(htconf);
    }

    n_array_free(files);
    return nerr == 0;
}

static int repo(struct cmdctx *cmdctx)
{
    struct arg_s *arg_s = cmdctx->_data;
    int rc = 1;

    if (arg_s == NULL || arg_s->action == ACTION_NONE) { /* not reached */
        logn(LOGERR, _("repo: no command"));
        return 0;
    }

    if (arg_s->action == ACTION_LIST)
        return list_repos(cmdctx->cctx->ctx);

    if (arg_s->action == ACTION_ADD) {
        unsigned flags = arg_s->force ? POLDEK_REPO_FORCE : 0;
        if (!poldek_repo_add(cmdctx->cctx->ctx, arg_s->url, flags))
            rc = 0;

        return rc;
    }

    if (arg_s->action == ACTION_DEL) {
        if (!poldek_repo_del(cmdctx->cctx->ctx, arg_s->url))
            rc = 0;

        return rc;
    }

    return 0;
}

struct poclidek_cmd command_repo = {
    COMMAND_SELFARGS | COMMAND_EMPTYARGS | COMMAND_HASVERBOSE | COMMAND_BATCH,
    "repo", N_("COMMAND [URL]"),
    N_("Manage built-in repos.d repositories"),
    options, parse_opt,
    NULL, repo, NULL, arg_s_free,
    N_("Commands:\n"
       "  add URL      add a repository from a remote configuration\n"
       "  del URL      remove a repository added with add\n"
       "  ls           list the repositories"),
    NULL, NULL, 0, 0,
    NULL
};
