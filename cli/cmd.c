/* 
  Copyright (C) 2000 - 2004 Pawel A. Gajda (mis@k2.net.pl)
 
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
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/errno.h>

#include <trurl/trurl.h>
#include <sigint/sigint.h>

#include "i18n.h"
#define ENABLE_TRACE 0
#include "log.h"
#include "cli.h"
#include "cmd.h"
#include "cmd_pipe.h"

static
struct cmd_chain_ent *cmd_chain_ent_new(unsigned flags,
                                        struct poclidek_cmd *cmd, tn_array *a_argv)
{
    struct cmd_chain_ent *ent;
    ent = n_malloc(sizeof(*ent));
    ent->flags = flags;
    ent->cmd = cmd;
    ent->next_piped = NULL;
    ent->prev_piped = NULL;
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
struct poclidek_cmd *find_command(struct poclidek_ctx *cctx, const char *name)
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
    
    else 
        logn(LOGERR, _("%s: ambiguous command"), name);
        
    return cmd;
}


static
tn_array *prepare_a_argv(struct poclidek_ctx *cctx, tn_array *cmd_chain, 
                         tn_array *a_argv)
{
    struct poclidek_cmd *cmd;
    tn_array            *tl;
    int                 rc = 0;
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
    
    if ((cmd = find_command(cctx, acmd)) == NULL)
        return NULL;

    if ((cmd->flags & COMMAND_IS_ALIAS) == 0) {
        struct cmd_chain_ent *ent;
        ent = cmd_chain_ent_new(CMD_CHAIN_ENT_CMD, cmd, a_argv);
        n_array_push(cmd_chain, ent);
    
    } else {
        n_assert(cmd->cmdline);

        if ((tl = n_str_etokl(cmd->cmdline)) == NULL) {
            rc = 0;
            
        } else {
            int i;

            free(n_array_shift(a_argv));
            while (n_array_size(tl))
                n_array_unshift(a_argv, n_array_pop(tl));
            n_array_free(tl);
            
            if (!a_argv_contains_break(a_argv)) {
                if (prepare_a_argv(cctx, cmd_chain, a_argv) == NULL)
                    return NULL;
                
            } else {
                tn_array *arr = a_argv_split(a_argv, ";|");
                
                for (i=0; i < n_array_size(arr); i++) {
                    struct a_argv_ent *ent = n_array_nth(arr, i);
                    if (ent->a_argv) {
                        if (prepare_a_argv(cctx, cmd_chain, a_argv) == NULL)
                            return NULL;
                        
                    } else {
                        struct cmd_chain_ent *ent;
                        ent = cmd_chain_ent_new(CMD_CHAIN_ENT_CMD, cmd, a_argv);
                        n_array_push(cmd_chain, ent);
                    }
                }
                
                n_array_free(arr);
            }
        }
    }
    
    return cmd_chain;
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
    tn_array  *cmd_chain, *arr, *tl;
    int       i, is_err = 0;

    
    if ((tl = n_str_etokl(line)) == NULL) {
        logn(LOGERR, _("%s: parse error"), line);
        return NULL;
    }
    
#if ENABLE_TRACE
    printf("line = (%s)\n", line);
    for (i=0; i<n_array_size(tl); i++)
        printf("tl[%d] = %s\n", i, (char*)n_array_nth(tl, i));
#endif
    
    cmd_chain = n_array_new(2, (tn_fn_free)cmd_chain_ent_free, NULL);
    arr = a_argv_split(tl, ";|");
                
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
                                /* no break */

            default:
                n_assert(0);
                break;
        }
        
    }

l_end:
    n_array_free(arr);
    n_array_free(tl);
    
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

