/* 
  Copyright (C) 2000, 2001 Pawel A. Gajda (mis@k2.net.pl)
 
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

static
int alias_cmp(struct command_alias *a1, struct command_alias *a2)
{
    return strcmp(a1->name, a2->name);
}

static
int alias_cmd_cmp(struct command_alias *p1, struct command_alias *p2)
{
	if (p1->cmd == p2->cmd)
		return 0;
	else 
		return -1;
}

static void alias_free(struct command_alias *alias) 
{
    n_cfree(&alias->name);
    n_cfree(&alias->cmdline);
    alias->cmd = NULL;
    free(alias);
}

static
int add_alias(struct poldekcli_ctx *cctx,
              const char *aliasname, const char *cmdline)
{
	struct command          *cmd;
    struct command          tmpcmd;
	struct command_alias    *alias, tmpalias;
	char                    *cmdname;
    const char              *p;
    int                     i, len;

    

    p = cmdline;
	while (isalnum(*p))
		p++;

    len = p - cmdline + 1;
    cmdname = alloca(len + 1);
	n_strncpy(cmdname, cmdline, len);
    
	tmpcmd.name = (char*)cmdname;
    
	if ((cmd = n_array_bsearch(cctx->commands, &tmpcmd)) == NULL) {
        logn(LOGWARN, _("%s: %s: no such command"), aliasname, cmdname);
		return 0;
	}

    alias = n_malloc(sizeof(*alias));
	alias->name = n_strdup(aliasname);
	alias->cmdline = n_strdup(cmdline);
	alias->cmd = cmd;

	tmpalias.name = aliasname;

	if ((i = n_array_bsearch_idx(cctx->aliases, &tmpalias)) >= 0) {
		n_array_set_nth(cctx->aliases, i, alias);
		i = n_array_bsearch_idx(cctx->all_commands, aliasname);
		n_array_set_nth(cctx->all_commands, i, alias->name);

	} else {
		n_array_push(cctx->aliases, alias);
        n_array_push(cctx->all_commands, alias->name);
	}

	n_array_sort(cctx->aliases);
	n_array_sort(cctx->all_commands);

	return 1;
}

void poldekcli_load_aliases(struct poldekcli_ctx *cctx, const char *path) 
{
    tn_hash *aliases_htcnf, *ht;
    tn_array *keys;
    int i;

    if (cctx->aliases == NULL)
        cctx->aliases  = n_array_new(16, (tn_fn_free)alias_free,
                                     (tn_fn_cmp)alias_cmp);

    
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

