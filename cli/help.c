/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@k2.net.pl>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
  $Id$
*/

#include <string.h>
#include <time.h>

#include <trurl/trurl.h>

#include "i18n.h"
#include "misc.h"
#include "cli.h"

static int cmd_help(struct cmdctx *cmdctx);

struct poclidek_cmd command_help = {
    COMMAND_NOOPTS | COMMAND_NOHELP | COMMAND_NOARGS, 
    "help", NULL, N_("Display this help"), 
    NULL, NULL, NULL, cmd_help,
    NULL, NULL, NULL, NULL, 0
};

static
int cmd_cmp_seqno(struct poclidek_cmd *c1, struct poclidek_cmd *c2)
{
    return c1->_seqno - c2->_seqno;
}


static
int cmd_help(struct cmdctx *cmdctx)
{
    int i;
    
    printf("%s\n", poldek_BANNER);
    n_array_sort_ex(cmdctx->cctx->commands, (tn_fn_cmp)cmd_cmp_seqno);
    for (i=0; i < n_array_size(cmdctx->cctx->commands); i++) {
        struct poclidek_cmd *cmd = n_array_nth(cmdctx->cctx->commands, i);
        char buf[256], *p;

        if (cmd->flags & (COMMAND_IS_ALIAS | COMMAND_HIDDEN))
            continue;
        
        p = cmd->arg ? cmd->arg : "";
        if (cmd->argp_opts) {
            snprintf(buf, sizeof(buf), _("[OPTION...] %s"), cmd->arg);
            p = buf;
        }
        printf("%-9s %-36s %s\n", cmd->name, p, cmd->doc);
    }
    printf(_("\nType COMMAND -? for details.\n"));
    return 0;
}
