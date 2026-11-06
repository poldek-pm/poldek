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

#include <ctype.h>
#include <dirent.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include <unistd.h>

#include <trurl/narray.h>
#include <trurl/nmalloc.h>
#include <trurl/nstr.h>

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

#define OPT_OVERWRITE   (OPT_GID + 1)
#define OPT_FORCE       (OPT_GID + 2)

#define ACTION_NONE     0
#define ACTION_ADD      1
#define ACTION_DEL      2
#define ACTION_LIST     3

static struct argp_option options[] = {
    { 0, 'a', "URL", 0, N_("Add repository from remote .conf URL"), 0 },
    { 0, 'd', "FILE.conf|URL", 0, N_("Remove managed .conf file or URL"), 0 },
    { 0, 'l', 0, 0, N_("List managed repositories"), 0 },
    { "force", OPT_FORCE, 0, 0,
      N_("Overwrite existing file or remove unmanaged file"), 0 },
    { 0, 0, 0, 0, 0, 0 }
};

struct arg_s {
    unsigned    action;
    tn_array    *urls;
    tn_array    *files;
    int         force;
};

static int is_managed_file(const char *path)
{
    FILE *f;
    char buf[1024];
    static const char prefix[] = "# remote repository managed by poldek";
    size_t len;
    int rc = 0;

    if ((f = fopen(path, "r")) == NULL)
        return 0;

    if (fgets(buf, sizeof(buf), f)) {
        len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
            buf[len - 1] = '\0';
        if (strncmp(buf, prefix, sizeof(prefix) - 1) == 0)
            rc = 1;
    }

    fclose(f);
    return rc;
}

static void arg_s_free(void *ptr)
{
    struct arg_s *arg_s = ptr;

    if (arg_s->urls)
        n_array_free(arg_s->urls);
    if (arg_s->files)
        n_array_free(arg_s->files);
    free(arg_s);
}

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdctx *cmdctx = state->input;
    struct arg_s *arg_s;

    if (cmdctx->_data) {
        arg_s = cmdctx->_data;

    } else {
        arg_s = n_malloc(sizeof(*arg_s));
        memset(arg_s, 0, sizeof(*arg_s));
        cmdctx->_data = arg_s;
    }

    switch (key) {
        case 'a':
            arg_s->action = ACTION_ADD;
            if (arg_s->urls == NULL)
                arg_s->urls = n_array_new(4, free, (tn_fn_cmp) strcmp);
            n_array_push(arg_s->urls, n_strdup(arg));
            break;

        case 'd':
            arg_s->action = ACTION_DEL;
            if (arg_s->files == NULL)
                arg_s->files = n_array_new(4, free, (tn_fn_cmp) strcmp);
            n_array_push(arg_s->files, n_strdup(arg));
            break;

        case 'l':
            arg_s->action = ACTION_LIST;
            break;

        case OPT_FORCE:
            arg_s->force = 1;
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }

    return 0;
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
        if (!origin)
            origin = n_basenam(path);

        printf("%s\n", origin);

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

static char *find_repo_file_by_origin(const char *reposd, const char *origin)
{
    tn_array *files;
    char *found = NULL;
    int i;

    files = poldek_util_readdir(reposd, "*.conf");
    for (i = 0; i < n_array_size(files); i++) {
        const char *path = n_array_nth(files, i);
        tn_hash *htconf;
        tn_hash *ht_global;
        const char *v;

        htconf = poldek_conf_load(path, POLDEK_LDCONF_NOVALIDATE |
                                  POLDEK_LDCONF_NOINCLUDE);
        if (htconf == NULL)
            continue;

        ht_global = poldek_conf_get_section(htconf, "global");
        v = ht_global ? poldek_conf_get(ht_global, "_origin", NULL) : NULL;
        if (v && strcmp(v, origin) == 0) {
            found = n_strdup(n_basenam(path));
            n_hash_free(htconf);
            break;
        }
        n_hash_free(htconf);
    }

    n_array_free(files);
    return found;
}

static int repo(struct cmdctx *cmdctx)
{
    struct arg_s *arg_s = cmdctx->_data;
    unsigned flags;
    int i, rc = 1;

    if (arg_s == NULL || arg_s->action == ACTION_NONE) {
        logn(LOGERR, _("repo: missing action (-a, -d or -l)"));
        return 0;
    }

    if (arg_s->action == ACTION_LIST)
        return list_repos(cmdctx->cctx->ctx);

    if (arg_s->action == ACTION_ADD) {
        flags = arg_s->force ? POLDEK_REPO_FORCE : 0;
        for (i = 0; i < n_array_size(arg_s->urls); i++) {
            const char *url = n_array_nth(arg_s->urls, i);
            if (!poldek_repo_add(cmdctx->cctx->ctx, url, flags))
                rc = 0;
        }
        return rc;
    }

    if (arg_s->action == ACTION_DEL) {
        char reposd[PATH_MAX], path[PATH_MAX];
        int nerr = 0;

        if (poldek_conf_reposd(reposd, sizeof(reposd)) == NULL) {
            logn(LOGERR, _("repo: cannot determine repos.d directory"));
            return 0;
        }

        /* map URLs to filenames */
        for (i = 0; i < n_array_size(arg_s->files); i++) {
            const char *fn = n_array_nth(arg_s->files, i);
            char *mapped;

            if (strchr(fn, '/') == NULL && strstr(fn, "://") == NULL)
                continue;

            mapped = find_repo_file_by_origin(reposd, fn);
            if (mapped == NULL) {
                logn(LOGERR, _("%s: no managed repository with this URL"), fn);
                nerr++;
                continue;
            }

            n_array_set_nth(arg_s->files, i, mapped);
        }

        if (nerr)
            return 0;

        /* preflight */
        for (i = 0; i < n_array_size(arg_s->files); i++) {
            const char *fn = n_array_nth(arg_s->files, i);
            size_t len = strlen(fn);

            if (strchr(fn, '/') != NULL) {
                logn(LOGERR, _("repo: %s: filename must not contain '/'"), fn);
                nerr++;
                continue;
            }

            if (len < 5 || strcmp(fn + len - 5, ".conf") != 0) {
                logn(LOGERR, _("repo: %s: not a .conf filename"), fn);
                nerr++;
                continue;
            }

            n_snprintf(path, sizeof(path), "%s/%s", reposd, fn);
            if (access(path, F_OK) != 0) {
                logn(LOGERR, _("%s: no such file"), path);
                nerr++;
                continue;
            }

            if (!arg_s->force && !is_managed_file(path)) {
                logn(LOGERR, _("%s: not a managed repository file (use --force)"),
                     path);
                nerr++;
            }
        }

        if (nerr)
            return 0;

        flags = arg_s->force ? POLDEK_REPO_FORCE : 0;
        for (i = 0; i < n_array_size(arg_s->files); i++) {
            const char *fn = n_array_nth(arg_s->files, i);
            if (!poldek_repo_del(cmdctx->cctx->ctx, fn, flags))
                rc = 0;
        }
        return rc;
    }

    return 0;
}

struct poclidek_cmd command_repo = {
    COMMAND_SELFARGS | COMMAND_EMPTYARGS | COMMAND_HASVERBOSE | COMMAND_BATCH,
    "repo", NULL, N_("Manage built-in repos.d repositories"),
    options, parse_opt,
    NULL, repo, NULL, arg_s_free, NULL, NULL, NULL, 0, 0,
    NULL
};
