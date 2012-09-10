/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <trurl/nmalloc.h>
#include <trurl/n_snprintf.h>

#include "compiler.h"
#include "cli.h"
#include "op.h"

#include "i18n.h"

struct poclidek_op_ctx {
    tn_hash *modeh;
};

struct poclidek_op_ctx *poclidek_op_ctx_new(void)
{
    struct poclidek_op_ctx *opctx = n_malloc(sizeof(*opctx));
    memset(opctx, 0, sizeof(*opctx));
    opctx->modeh = n_hash_new(16, free);
    return opctx;
}

void poclidek_op_ctx_free(struct poclidek_op_ctx *opctx)
{
    n_hash_free(opctx->modeh);
    free(opctx);
}

static
int poclidek_op_ctx_set_major_mode(struct poclidek_op_ctx *opctx,
                                   const char *mode, const char *cmd)
{
    n_hash_replace(opctx->modeh, mode, n_strdup(cmd ? cmd : mode));
    return 1;
}

int poclidek_op_ctx_verify_major_mode(struct poclidek_op_ctx *opctx)
{
    tn_array *majormodes;
    char tmp[1024], *sp;
    int i, n;

    majormodes = n_hash_keys(opctx->modeh);
    n_array_sort(majormodes);

    if (n_array_size(majormodes) < 2) {
        n_array_free(majormodes);
        return 1;
    }
    
    
    n = 0;
    sp = ", ";
    if (n_array_size(majormodes) == 2) {
        sp = _(" and ");
        // it is ok if both modes are same eg. --install and --upgrade are install mode
        if (strcmp(n_hash_get(opctx->modeh, n_array_nth(majormodes, 0)), n_hash_get(opctx->modeh, n_array_nth(majormodes, 1)))) {
            n_array_free(majormodes);
            return 1;
        }
    }

    for (i=0; i < n_array_size(majormodes); i++) {
        if (n_array_size(majormodes) > 2 && i == n_array_size(majormodes) - 2)
            sp = _(" and ");
        
        n += n_snprintf(&tmp[n], sizeof(tmp) - n, "'--%s'%s",
                        n_hash_get(opctx->modeh, n_array_nth(majormodes, i)),
                        i < n_array_size(majormodes) - 1 ? sp : "");
    }
    
    logn(LOGERR, _("%s options are exclusive"), tmp);
    n_array_free(majormodes);
    return 0;
}


static int opgroup_rt_set_major_mode(struct poclidek_opgroup_rt *rt,
                                     const char *mode, const char *cmd)
{
    if (rt->opctx)
        return poclidek_op_ctx_set_major_mode(rt->opctx, mode, cmd);
    return 0;
}

struct poclidek_opgroup_rt *poclidek_opgroup_rt_new(struct poldek_ts *ts,
                                                    struct poclidek_op_ctx *opctx)
{
    struct poclidek_opgroup_rt *rt;

    rt = n_malloc(sizeof(*rt));
    memset(rt, 0, sizeof(*rt));
    rt->ctx = ts->ctx;
    rt->ts = ts;
    rt->opctx = opctx;
    rt->set_major_mode = opgroup_rt_set_major_mode;
    rt->_opdata = NULL;
    rt->_opdata_free = NULL;
    return rt;
}

void poclidek_opgroup_rt_free(struct poclidek_opgroup_rt *rt)
{
    n_assert(rt->_opdata_free);
    if (rt->_opdata) {
        if (rt->_opdata_free)
            rt->_opdata_free(rt->_opdata);
        else
            n_die("memleak, no _opdata_free\n");
        
        rt->_opdata = NULL;
    }
    
    rt->ctx = NULL;
}


