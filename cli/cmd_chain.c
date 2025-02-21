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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <trurl/trurl.h>
#include <sigint/sigint.h>

#include "compiler.h"
#include "i18n.h"
#include "poldek_util.h"
#include "log.h"
#include "cli.h"
#include "cmd_chain.h"
#include "cmd_pipe.h"

static
tn_array *prepare_cmdline(struct poclidek_ctx *cctx, tn_array *cmd_chain,
                          const char *line);

static
struct cmd_chain_ent *cmd_chain_ent_new(unsigned flags,
                                        struct poclidek_cmd *cmd,
                                        tn_array *a_argv)
{
    struct cmd_chain_ent *ent;
    ent = n_malloc(sizeof(*ent));
    ent->flags = flags;
    ent->cmd = cmd;
    ent->next_piped = NULL;
    ent->prev_piped = NULL;
    ent->pipe_right = NULL;
    if (a_argv)
        ent->a_argv = n_ref(a_argv);
    else
        ent->a_argv = NULL;

    return ent;
}

static
void cmd_chain_ent_free(struct cmd_chain_ent *ent)
{
    if (ent->a_argv)
        n_array_free(ent->a_argv);

    if (ent->pipe_right)
        cmd_pipe_free(ent->pipe_right);

    if (ent->next_piped)
        cmd_chain_ent_free(ent->next_piped);

    free(ent);
}

static
int a_argv_contains_break(tn_array *a_argv)
{
    char              *brk = ";|";
    int               i;

    for (i=0; i < n_array_size(a_argv); i++) {
        char *arg = n_array_nth(a_argv, i);
        if (strchr(brk, *arg)) {
            n_assert(*(arg + 1) == '\0');
            return 1;
        }
    }
    return 0;
}

struct a_argv_ent {
    char      brk;
    tn_array  *a_argv;
};

static
void a_argv_ent_free(struct a_argv_ent *ent)
{
    if (ent->a_argv)
        n_array_free(ent->a_argv);
    free(ent);
}

static int is_external(const char *arg)
{
    return *arg == '!' && strlen(arg) > 1;
}

static
tn_array *a_argv_split(tn_array *a_argv, const char *brk)
{
    int               i, rc = 1;
    tn_array          *cmds, *tl;


    cmds = n_array_new(2, (tn_fn_free)a_argv_ent_free, NULL);
    tl = n_array_new(4, free, NULL);

    for (i=0; i < n_array_size(a_argv); i++) {
        char *arg = n_array_nth(a_argv, i);

        if (strchr(brk, *arg) == NULL) {
            int external = 0;
            if (is_external(arg)) {
                n_array_push(tl, n_strdup("!"));
                arg++;
                external = 1;
            }

            n_array_push(tl, n_strdup(arg));

            if (external)
                n_array_push(tl, n_strdup("--")); /* pass opt to external cmd */

        } else {
            n_assert(*(arg + 1) == '\0');
            if (n_array_size(tl)) {
                struct a_argv_ent *ent;

                ent = n_malloc(sizeof(*ent));
                ent->brk = 0;
                ent->a_argv = tl;
                n_array_push(cmds, ent);

                ent = n_malloc(sizeof(*ent));
                ent->brk = *arg;
                ent->a_argv = NULL;
                n_array_push(cmds, ent);

                tl = n_array_new(4, free, NULL);
            }
        }
    }

    if (rc && n_array_size(tl)) {
        struct a_argv_ent *ent;

        ent = n_malloc(sizeof(*ent));
        ent->brk = 0;
        ent->a_argv = tl;
        n_array_push(cmds, ent);

    } else
        n_array_free(tl);

    if (!rc || n_array_size(cmds) == 0) {
        n_array_free(cmds);
        cmds = NULL;
    }

    return cmds;
}

static
struct poclidek_cmd *find_command(struct poclidek_ctx *cctx, const char *name, int quiet)
{
    struct poclidek_cmd *cmd, tmpcmd;
    int  i, j, n;

    n_array_sort(cctx->commands);
    tmpcmd.name = (char*)name;

    if ((cmd = n_array_bsearch(cctx->commands, &tmpcmd)))
        return cmd;

    i = j = n_array_bsearch_idx_ex(cctx->commands, &tmpcmd,
                                   (tn_fn_cmp)poclidek_cmd_ncmp);

    if (i < 0) {
        logn(LOGERR, _("%s: no such command"), name);
        return NULL;
    }


    n = 1;
    j++;
    while (j < n_array_size(cctx->commands) &&
           poclidek_cmd_ncmp(n_array_nth(cctx->commands, j), &tmpcmd) == 0) {
        n++;
        j++;
    }

    if (n == 1)
        cmd = n_array_nth(cctx->commands, i);
    else if (!quiet)
        logn(LOGERR, _("%s: ambiguous command"), name);

    return cmd;
}

static const char *apply_params(const char *cmdline, tn_array *a_argv)
{
    tn_hash *vars;
    tn_array *new_a_argv = NULL;
    char newline[1024];
    int i;

    n_assert(n_array_size(a_argv) > 0);

    vars = n_hash_new(n_array_size(a_argv) < 16 ? 16:n_array_size(a_argv) * 2,
                      NULL);

    for (i=0; i < n_array_size(a_argv); i++) {
        char no[64];
        n_snprintf(no, sizeof(no), "%d", i);

        n_hash_insert(vars, no, n_array_nth(a_argv, i));
    }

    cmdline = poldek_util_expand_vars(newline, sizeof(newline),
                                      cmdline, '%', vars,
                                      POLDEK_UTIL_EXPANDVARS_RMUSED);

    if (strchr(cmdline, '%')) {/* still unexpanded vars */
        cmdline = NULL;
        goto l_end;
    }

    /* remove used args from a_argv  */
    new_a_argv = n_array_clone(a_argv);
    n_assert(n_array_ctl_get_freefn(a_argv) == free);

    i = 0;
    while (n_array_size(a_argv) > 0) {
        char no[64], *arg = n_array_shift(a_argv);

        n_snprintf(no, sizeof(no), "%d", i++);
        if (n_hash_exists(vars, no))
            n_array_push(new_a_argv, arg);
        else
            free(arg);
    }
    n_assert(n_array_size(a_argv) == 0);
    while (n_array_size(new_a_argv) > 0)
        n_array_push(a_argv, n_array_shift(new_a_argv));
    n_array_free(new_a_argv);


 l_end:
    n_hash_free(vars);
    return cmdline;
}

static
tn_array *prepare_a_argv(struct poclidek_ctx *cctx, tn_array *cmd_chain,
                         tn_array *a_argv)
{
    struct poclidek_cmd *cmd;
    const char          *acmd;

    n_assert(a_argv_contains_break(a_argv) == 0);

    acmd = n_array_nth(a_argv, 0);
    if (is_external(acmd)) {
        char *p = n_array_shift(a_argv);
        n_array_unshift(a_argv, n_strdup("--"));
        n_array_unshift(a_argv, n_strdup(p + 1));
        n_array_unshift(a_argv, n_strdup("!"));
        acmd = n_array_nth(a_argv, 0);
    }

    if ((cmd = find_command(cctx, acmd, 0)) == NULL)
        return NULL;

    if ((cmd->flags & COMMAND_IS_ALIAS) == 0) {
        struct cmd_chain_ent *ent;
        ent = cmd_chain_ent_new(CMD_CHAIN_ENT_CMD, cmd, a_argv);
        n_array_push(cmd_chain, ent);

    } else {                    /* alias */
        const char *cmdline;

        n_assert(cmd->cmdline);
        cmdline = cmd->cmdline;

        if (cmd->flags & COMMAND_PARAMETERIZED) {
            if (n_array_size(a_argv) == 0) {
                logn(LOGERR, _("%s: alias needs an arguments"), acmd);
                return NULL;
            }

            if ((cmdline = apply_params(cmdline, a_argv)) == NULL) {
                logn(LOGERR,
                     _("%s: apply arguments failed (not enough arguments?)"),
                     acmd);
                return NULL;
            }
        }
        msgn(2, "%s => %s\n", acmd, cmdline);

        if (n_array_size(a_argv) > 1) { /* any arguments? -> pass them */
            char *line;
            int i, n, len;

            len = strlen(cmd->cmdline) + 1;
            /* from 1 -- skip alias */
            for (i=1; i < n_array_size(a_argv); i++)
                /* + quotes + space */
                len += strlen((char*)n_array_nth(a_argv, i)) + 2 + 1;
            len++;

            line = alloca(len);
            n = n_snprintf(line, len, "%s", cmd->cmdline);

            for (i=1; i < n_array_size(a_argv); i++)
                n += n_snprintf(&line[n], len - n, " '%s'",
                                (char*)n_array_nth(a_argv, i));
            cmdline = line;
        }

        if (prepare_cmdline(cctx, cmd_chain, cmdline) == NULL)
            return NULL;
    }

    return cmd_chain;
}

static
tn_array *prepare_cmdline(struct poclidek_ctx *cctx, tn_array *cmd_chain,
                          const char *line)
{
    tn_array  *arr = NULL, *tl = NULL;
    int       i, is_err = 0;

    if ((tl = n_str_etokl_ext(line, "\t ", ";|", "\"'", '\\')) == NULL) {
        logn(LOGERR, _("%s: parse error"), line);
        return NULL;
    }

    if (n_array_size(tl) == 0)  /* empty token list */
        goto l_end;

#if ENABLE_TRACE
    printf("line = (%s)\n", line);
    for (i=0; i<n_array_size(tl); i++)
        printf("tl[%d] = %s\n", i, (char*)n_array_nth(tl, i));
#endif

    if ((arr = a_argv_split(tl, ";|")) == NULL)
        return NULL;

    for (i=0; i < n_array_size(arr); i++) {
        struct a_argv_ent *ent = n_array_nth(arr, i);
        struct cmd_chain_ent *cent = NULL;

        switch (ent->brk) {
            case ';':
                cent = cmd_chain_ent_new(CMD_CHAIN_ENT_SEMICOLON, NULL, NULL);
                n_array_push(cmd_chain, cent);
                break;

            case '|':
                cent = cmd_chain_ent_new(CMD_CHAIN_ENT_PIPE, NULL, NULL);
                n_array_push(cmd_chain, cent);
                break;

            case 0:
                if (ent->a_argv) {
#if ENABLE_TRACE
                    int j;
                    for (j=0; j < n_array_size(ent->a_argv); j++)
                        printf("tl[%d][%d] = %s\n", i, j,
                               (char*)n_array_nth(ent->a_argv, j));
#endif
                    if (prepare_a_argv(cctx, cmd_chain, ent->a_argv) == NULL) {
                        is_err = 1;
                        goto l_end;
                    }

                    break;
                }
                /* fallthru */

            default:
                n_assert(0);
                break;
        }

    }

l_end:

    n_array_cfree(&arr);
    n_array_cfree(&tl);
    return is_err ? NULL : cmd_chain;
}


static
tn_array *divide_into_pipelines(tn_array *cmd_chain)
{
    tn_array *cmd_chain2;
    int i, is_err = 0;

    cmd_chain2 = n_array_new(2, (tn_fn_free)cmd_chain_ent_free, NULL);
    i = 0;
    while (n_array_size(cmd_chain)) {
        struct cmd_chain_ent *ent = n_array_shift(cmd_chain);
        if (ent->flags & CMD_CHAIN_ENT_SEMICOLON)
            continue;

        if (ent->flags & CMD_CHAIN_ENT_PIPE) {
            struct cmd_chain_ent *prev;
            prev = n_array_nth(cmd_chain2, n_array_size(cmd_chain2) - 1);
            n_assert(prev);
            n_assert(prev->cmd);

            //printf("prev = %p %p\n", prev, prev->next_piped);
            if ((prev->cmd->flags & COMMAND_PIPEABLE_RIGTH) == 0) {
                logn(LOGERR, _("%s: not a right pipe-able command"),
                     prev->cmd->name);
                is_err = 1;
                break;
            }

            while (prev->next_piped)
                prev = prev->next_piped;
            //printf("prev2 = %p %p\n", prev, prev->next_piped);

            if (n_array_size(cmd_chain) == 0) { /* lonely pipe */
                logn(LOGERR, _("%s: where is the pipe going?"),
                     prev->cmd->name);
                is_err = 1;
                break;
            }

            prev->next_piped = n_array_shift(cmd_chain);
            if ((prev->next_piped->cmd->flags & COMMAND_PIPEABLE_LEFT) == 0) {
                logn(LOGERR, _("%s: not a left pipe-able command"),
                     prev->next_piped->cmd->name);
                is_err = 1;
                break;

            }
            prev->next_piped->prev_piped = prev; /* double-linked list */
            //printf("prev3= %p %p\n", prev, prev->next_piped);
            continue;
        }
        //printf("push ent[%d] %s, %d\n", i, ent->cmd->name,
        //       n_array_size(ent->a_argv));
        n_array_push(cmd_chain2, ent);
        i++;
    }

#if ENABLE_TRACE
    for (i=0; i < n_array_size(cmd_chain2); i++) {
        struct cmd_chain_ent *ent = n_array_nth(cmd_chain2, i);
        printf("ent[%d]:  %s, %d", i, ent->cmd->name, n_array_size(ent->a_argv));
        while (ent->next_piped) {
            ent = ent->next_piped;
            printf(" | %s, %d", ent->cmd->name, n_array_size(ent->a_argv));
        }
        printf("\n");
    }
#endif
    if (is_err) {
        n_array_free(cmd_chain2);
        cmd_chain2 = NULL;
    }

    return cmd_chain2;
}

tn_array *poclidek_prepare_cmdline(struct poclidek_ctx *cctx, const char *line)
{
    tn_array  *cmd_chain;
    int       is_err = 0;

    cmd_chain = n_array_new(2, (tn_fn_free)cmd_chain_ent_free, NULL);
    if (prepare_cmdline(cctx, cmd_chain, line) == NULL)
        is_err = 1;

    if (is_err) {
        n_array_free(cmd_chain);
        cmd_chain = NULL;

    } else {
        tn_array *chain = divide_into_pipelines(cmd_chain);
        n_array_free(cmd_chain);
        cmd_chain = chain;
    }

    return cmd_chain;
}

int poclidek_has_batch_command(struct poclidek_ctx *cctx, const char *name) {
    struct poclidek_cmd *cmd = find_command(cctx, name, 1);

    if (cmd && (cmd->flags & COMMAND_INTERACTIVE) == 0)
        return 1;

    return 0;
}
