/* 
  Copyright (C) 2000 - 2003 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nmalloc.h>
#include <trurl/nstr.h>

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
    NULL, NULL, NULL, NULL, NULL, 0, 0
};

static int alias(struct cmdctx *cmdctx) 
{
    int i;

    for (i=0; i < n_array_size(cmdctx->cctx->commands); i++) {
        struct poclidek_cmd *cmd = n_array_nth(cmdctx->cctx->commands, i);
        if (cmd->flags & COMMAND_IS_ALIAS)
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
    }
}


static
struct poclidek_cmd *command_new_alias(const char *name, const char *cmdline) 
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


static
int add_alias(struct poclidek_ctx *cctx,
              const char *aliasname, const char *cmdline)
{
    struct poclidek_cmd *cmd, tmpcmd;
    int rc = 1;
    
	tmpcmd.name = (char*)aliasname;
    if ((cmd = n_array_bsearch(cctx->commands, &tmpcmd)) == NULL) {
        cmd = command_new_alias(aliasname, cmdline);
        n_array_push(cctx->commands, cmd);
        
    } else {
        if ((cmd->flags & COMMAND_IS_ALIAS) == 0) {
            logn(LOGWARN, _("%s: alias could not shadow a command"), aliasname);
            rc = 0;
            
        } else {
            if (poldek_verbose() > 1)
                logn(LOGWARN, _("%s (%s) overwrites %s"), aliasname, cmdline,
                     cmd->cmdline);
            free(cmd->name);
            cmd->name = n_strdup(aliasname);

            free(cmd->cmdline);
            cmd->cmdline = n_strdup(cmdline);
        }
	}
    
	n_array_sort(cctx->commands);
	return rc;
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

static void find_aliased_commands(struct poclidek_ctx *cctx) 
{
    struct poclidek_cmd *cmd;
    int i;
    
    for (i=0; i < n_array_size(cctx->commands); i++) {
        cmd = n_array_nth(cctx->commands, i);
        
        if ((cmd->flags & COMMAND_IS_ALIAS) == 0)
            continue;

        cmd->aliasto = alias_to(cctx, cmd->cmdline);
        if (cmd->aliasto == NULL)
            logn(LOGWARN, _("%s: could not determine aliased command"),
                 cmd->name);
        else
            msgn(3, "%s => aliased %s", cmd->name, cmd->aliasto);
    }
}

int poclidek_load_aliases(struct poclidek_ctx *cctx, const char *path) 
{
    tn_hash *aliases_htcnf, *ht;
    tn_array *keys;
    int i, n = 0;
    
    if (access(path, R_OK) != 0)
        return 0;
    
    aliases_htcnf = poldek_conf_load(path, POLDEK_LDCONF_NOVRFY);
    if (aliases_htcnf == NULL)
        return 0;
    
    ht = poldek_conf_get_section_ht(aliases_htcnf, "global");
    keys = n_hash_keys(ht);

    for (i=0; i < n_array_size(keys); i++) {
        const char *name, *cmdline;

        name = n_array_nth(keys, i);
        if (*name == '_')       /* config macro */
            continue;
        
        if ((cmdline = poldek_conf_get(ht, name, NULL)))
            if (add_alias(cctx, name, cmdline))
                n++;
    }
    
    n_array_free(keys);
    n_hash_free(aliases_htcnf);
    
    if (n)
        find_aliased_commands(cctx);
        
    return 1;
}



