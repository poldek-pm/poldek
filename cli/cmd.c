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
#include "log.h"
#include "cli.h"
#include "cmd.h"


struct cmd_pipe *cmd_pipe_new(int type) 
{
    struct cmd_pipe *p;
    p = n_malloc(sizeof(*p));
    memset(p, 0, sizeof(*p));
    p->type = type;
    p->pkgs = NULL;
    if (p->type == CMD_PIPE_INTERN)
        p->pkgs = pkgs_array_new(64);
    return p;
}

int cmd_pipe_writepkg(struct cmd_pipe *p, struct pkg *pkg) 
{
    if (p->type == CMD_PIPE_INTERN) {
        n_array_push(p->pkgs, pkg_link(pkg));
        
    } else {
        fprintf(p->stream_in, "%s", pkg_snprintf_s(pkg));
    }
    return 1;
}


char *cmd_pipe_getline(struct cmd_pipe *p, char *line, int size)
{
    if (p->type == CMD_PIPE_PIPE) {
        fgets(line, size - 1, p->stream_out);
        p->_lineno++;
        return line;
        
    } else if (p->type == CMD_PIPE_INTERN) {
        if (p->_lineno >= n_array_size(p->pkgs))
            return NULL;
        
        pkg_snprintf(line, size, n_array_nth(p->pkgs, p->_lineno));
        p->_lineno++;
        return line;
    }
    
    return NULL;
}

            

static
struct cmd_chain_ent *cmd_chain_ent_new(unsigned flags,
                                        struct poclidek_cmd *cmd, tn_array *a_argv)
{
    struct cmd_chain_ent *ent;
    ent = n_malloc(sizeof(*ent));
    ent->flags = flags;
    ent->cmd = cmd;
    ent->next_piped = NULL;
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
            if (*arg == '!' && strlen(arg) > 1) {
                n_array_push(tl, n_strdup("!"));
                arg++;
            }
            
            n_array_push(tl, n_strdup(arg));
            
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
        return cmd;
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
                prepare_a_argv(cctx, cmd_chain, a_argv);
                
            } else {
                tn_array *arr = a_argv_split(a_argv, ";|");
                
                for (i=0; i < n_array_size(arr); i++) {
                    struct a_argv_ent *ent = n_array_nth(arr, i);
                    if (ent->a_argv)
                        prepare_a_argv(cctx, cmd_chain, a_argv);
                    
                    else {
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


tn_array *poclidek_prepare_cmdline(struct poclidek_ctx *cctx, const char *line)
{
    tn_array  *cmd_chain, *arr, *tl, *cmd_chain2;
    int       i;

    
    if ((tl = n_str_etokl(line)) == NULL) {
        logn(LOGERR, _("%s: parse error"), line);
        return NULL;
    }

    printf("line = (%s)\n", line);
    for (i=0; i<n_array_size(tl); i++)
        printf("tl[%d] = %s\n", i, (char*)n_array_nth(tl, i));

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
                    int j;
                    
                    for (j=0; j < n_array_size(ent->a_argv); j++)
                        printf("tl[%d][%d] = %s\n", i, j,
                               (char*)n_array_nth(ent->a_argv, j));
                    
                    prepare_a_argv(cctx, cmd_chain, ent->a_argv);
                    break;
                }
                                /* no break */

            default:
                n_assert(0);
                break;
        }
        
    }
    //n_array_free(arr);
    //n_array_free(tl);

    cmd_chain2 = n_array_new(2, (tn_fn_free)cmd_chain_ent_free, NULL);
    i = 0;
    while (n_array_size(cmd_chain)) {
        struct cmd_chain_ent *ent = n_array_shift(cmd_chain);

        printf("ent %p %d\n", ent, n_array_size(cmd_chain));
        if (ent->flags & CMD_CHAIN_ENT_SEMICOLON)
            continue;
        
        if (ent->flags & CMD_CHAIN_ENT_PIPE) {
            struct cmd_chain_ent *prev;
            prev = n_array_nth(cmd_chain2, n_array_size(cmd_chain2) - 1);
            printf("prev = %p %p\n", prev, prev->next_piped);
            while (prev->next_piped)
                prev = prev->next_piped;
            printf("prev2 = %p %p\n", prev, prev->next_piped);
            prev->next_piped = n_array_shift(cmd_chain);
            printf("prev3= %p %p\n", prev, prev->next_piped);
            continue;
        }

        printf("push ent[%d] %s, %d\n", i, ent->cmd->name, n_array_size(ent->a_argv));
        n_array_push(cmd_chain2, ent);
        i++;
    }
    printf("end\n");
    n_array_free(cmd_chain);
    for (i=0; i < n_array_size(cmd_chain2); i++) {
        struct cmd_chain_ent *ent = n_array_nth(cmd_chain2, i);
        printf("ent[%d]:  %s, %d", i, ent->cmd->name, n_array_size(ent->a_argv));
        while (ent->next_piped) {
            ent = ent->next_piped;
            printf(" | %s, %d", ent->cmd->name, n_array_size(ent->a_argv));
        }
        printf("\n");
    }
    return cmd_chain2;
}

tn_array *get_pipeline(tn_array *cmd_chain, int *i) 
{
    
}

