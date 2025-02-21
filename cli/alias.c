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

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <trurl/trurl.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "cli.h"
#include "cmd_chain.h"
#include "conf.h"


static int alias(struct cmdctx *cmdctx);

struct poclidek_cmd command_alias = {
    COMMAND_NOARGS | COMMAND_NOOPTS,
    "alias", NULL, N_("Print defined command aliases"),
    NULL, NULL, NULL, alias,
    NULL, NULL, NULL, NULL, NULL, 0, 0,
    NULL
};

static int alias(struct cmdctx *cmdctx)
{
    int i;

    for (i=0; i < n_array_size(cmdctx->cctx->commands); i++) {
        struct poclidek_cmd *cmd = n_array_nth(cmdctx->cctx->commands, i);

        if ((cmd->flags & COMMAND_IS_ALIAS) == 0)
            continue;

        /* skip external commands aliases in non-interactive mode */
        if ((cmdctx->cctx->_flags & POLDEKCLI_UNDERIMODE) == 0) {
            if ((cmd->flags & COMMAND_INTERACTIVE))
                continue;
        }

        cmdctx_printf(cmdctx, "%-18s = %s\n", cmd->name, cmd->cmdline);
    }
    return 1;
}

static void free_alias(struct poclidek_cmd *cmd)
{
    if (cmd->flags & COMMAND_IS_ALIAS) {
        n_cfree(&cmd->cmdline);
        n_cfree(&cmd->name);
        n_cfree(&cmd->aliasto);
        memset(cmd, 0, sizeof(*cmd));
        free(cmd);
    }
}

static struct poclidek_cmd *new_command_alias(const char *name, const char *cmdline)
{
    struct poclidek_cmd *alias;

    alias = n_malloc(sizeof(*alias));
    memset(alias, 0, sizeof(*alias));
    alias->flags = COMMAND_IS_ALIAS | COMMAND__MALLOCED;
    alias->name = n_strdup(name);
    alias->cmdline = n_strdup(cmdline);
    alias->aliasto = NULL;
    alias->_free = free_alias;
    if (strchr(alias->cmdline, '%'))
        alias->flags |= COMMAND_PARAMETERIZED;
    return alias;
}

/* determine to what command alias is aliased */
static char *alias_to(struct poclidek_ctx *cctx, const char *cmdline)
{
    tn_array *ents;
    char *p, *cmd = NULL;

    if (strchr(cmdline, '|') == NULL) {
        cmd = n_strdup(cmdline);

    } else if ((ents = poclidek_prepare_cmdline(cctx, cmdline))) {
        struct cmd_chain_ent *ent = n_array_nth(ents, 0);
        while (ent->next_piped)
            ent = ent->next_piped;

        cmd = n_strdup(ent->cmd->name);
        n_array_free(ents);
    }

    if (cmd == NULL)
        return NULL;

    if ((p = strchr(cmd, ' ')))
        *p = '\0';

    return cmd;
}

static
int add_alias(struct poclidek_ctx *cctx,
              const char *aliasname, const char *cmdline)
{
    struct poclidek_cmd *cmd, tmpcmd;
    unsigned flags = 0;
    int ok = 1, seqno = 0;
    char *to;

    /* find aliased command and if interactive mark alias respectively  */
    to = alias_to(cctx, cmdline);
    if (to == NULL) {
        logn(LOGWARN, _("%s: could not determine aliased command"),
             aliasname);
    } else {
        msgn(3, _("%s => aliased %s"), aliasname, to);
        /* external or quit is dynamically added */
        if (*to == '!' || strcmp(to, "quit") == 0) {
            flags |= COMMAND_INTERACTIVE;
        } else {
            tmpcmd.name = to;
            cmd = n_array_bsearch(cctx->commands, &tmpcmd);
            if (cmd == NULL) {
                logn(LOGWARN, _("%s: could not determine aliased command"), to);

            } else {
                if (cmd->flags & COMMAND_INTERACTIVE)
                    flags |= COMMAND_INTERACTIVE;

                seqno = cmd->_seqno;
            }
        }
    }

    tmpcmd.name = (char*)aliasname;
    if ((cmd = n_array_bsearch(cctx->commands, &tmpcmd)) == NULL) {
        cmd = new_command_alias(aliasname, cmdline);
        cmd->flags |= flags;
        cmd->_seqno = seqno;
        cmd->aliasto = to;
        n_array_push(cctx->commands, cmd);

    } else {
        if ((cmd->flags & COMMAND_IS_ALIAS) == 0) {
            logn(LOGWARN, _("%s: alias could not shadow a command"), aliasname);
            ok = 0;

        } else {
            if (poldek_verbose() > 1)
                logn(LOGWARN, _("%s (%s) overwrites %s"), aliasname, cmdline,
                     cmd->cmdline);

            cmd->flags |= flags;

            free(cmd->name);
            cmd->name = n_strdup(aliasname);

            free(cmd->cmdline);
            cmd->cmdline = n_strdup(cmdline);

            n_cfree(&cmd->aliasto);
            cmd->aliasto = to;
        }
    }

    n_array_sort(cctx->commands);
    return ok;
}

int poclidek__load_aliases(struct poclidek_ctx *cctx)
{
    char *homedir, *sysconfdir = "/etc", path[PATH_MAX];

#ifdef SYSCONFDIR
    if (n_str_ne(sysconfdir, SYSCONFDIR) && access(SYSCONFDIR, R_OK) == 0)
        sysconfdir = SYSCONFDIR;
#endif

    n_snprintf(path, sizeof(path), "%s/poldek/aliases.conf", sysconfdir);
    if (access(path, R_OK) == 0) {
        tn_hash *htcnf = poldek_conf_load(path, POLDEK_LDCONF_FOREIGN);
        if (htcnf) {
            tn_hash *aliases = poldek_conf_get_section(htcnf, "global");
            poclidek__add_aliases(cctx, aliases);
            n_hash_free(htcnf);
        }
    }

    if ((homedir = getenv("HOME")) != NULL) {
        tn_hash *htcnf;
        int load = 1;

        snprintf(path, sizeof(path), "%s/.poldek-aliases.conf", homedir);

        if (access(path, R_OK) != 0) {
            snprintf(path, sizeof(path), "%s/.poldek.alias", homedir);
            if (access(path, R_OK) != 0)
                load = 0;
        }
        if (load && (htcnf = poldek_conf_load(path, POLDEK_LDCONF_FOREIGN))) {
            tn_hash *aliases = poldek_conf_get_section(htcnf, "global");
            poclidek__add_aliases(cctx, aliases);
            n_hash_free(htcnf);
        }
    }

    return 1;
}

int poclidek__add_aliases(struct poclidek_ctx *cctx, tn_hash *htcnf)
{
    tn_array *keys;
    int i, n = 0;

    keys = n_hash_keys(htcnf);

    for (i=0; i < n_array_size(keys); i++) {
        const char *name, *cmdline;

        name = n_array_nth(keys, i);
        if (*name == '_')       /* config macro */
            continue;

        if ((cmdline = poldek_conf_get(htcnf, name, NULL)))
            if (add_alias(cctx, name, cmdline))
                n++;
    }

    n_array_free(keys);

    return n;
}
