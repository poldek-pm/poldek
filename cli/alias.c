/* 
  Copyright (C) 2000 - 2003 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nmalloc.h>
#include <trurl/nstr.h>

#include "i18n.h"
#include "log.h"
#include "cli.h"
#include "conf.h"


static int alias(struct cmdctx *cmdctx);

struct poclidek_cmd command_alias = {
    COMMAND_NOARGS | COMMAND_NOOPTS, 
    "alias", NULL, N_("Print defined command aliases"), 
    NULL, NULL, NULL, alias,
    NULL, NULL, NULL, NULL, 0
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



static
struct poclidek_cmd *command_new_alias(const char *name, const char *cmdline) 
{
    struct poclidek_cmd *alias;

    alias = n_malloc(sizeof(*alias));
    memset(alias, 0, sizeof(*alias));
    alias->flags = COMMAND_IS_ALIAS;
    alias->name = n_strdup(name);
    alias->cmdline = n_strdup(cmdline);
    return alias;
}


static
int add_alias(struct poclidek_ctx *cctx,
              const char *aliasname, const char *cmdline)
{
	struct poclidek_cmd          *cmd;
    struct poclidek_cmd          tmpcmd;

    
	tmpcmd.name = (char*)aliasname;
	if ((cmd = n_array_bsearch(cctx->commands, &tmpcmd)) == NULL) {
        n_array_push(cctx->commands, command_new_alias(aliasname, cmdline));
        
    } else {
        if ((cmd->flags & COMMAND_IS_ALIAS) == 0) {
            logn(LOGWARN, _("%s: alias could not shadow a command"), aliasname);
            
        } else {
            if (verbose > 1)
                logn(LOGWARN, _("%s (%s) overwrites %s"), aliasname, cmdline,
                     cmd->cmdline);
            free(cmd->name);
            cmd->name = n_strdup(aliasname);

            free(cmd->cmdline);
            cmd->cmdline = n_strdup(cmdline);
        }
	}
    
	n_array_sort(cctx->commands);
	return 1;
}


void poclidek_load_aliases(struct poclidek_ctx *cctx, const char *path) 
{
    tn_hash *aliases_htcnf, *ht;
    tn_array *keys;
    int i;
    
    if (access(path, R_OK) != 0)
        return;
    
    aliases_htcnf = poldek_ldconf(path, POLDEK_LDCONF_NOVRFY);
    
    if (aliases_htcnf == NULL)
        return;
    
    ht = poldek_conf_get_section_ht(aliases_htcnf, "global");
    keys = n_hash_keys(ht);

    for (i=0; i < n_array_size(keys); i++) {
        char *name, *cmdline;

        name = n_array_nth(keys, i);
        if ((cmdline = poldek_conf_get(ht, name, NULL)))
            add_alias(cctx, name, cmdline);
    }
    
    n_array_free(keys);
    n_hash_free(aliases_htcnf);
}





