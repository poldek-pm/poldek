/*
  Copyright (C) 2000 - 2004 Pawel A. Gajda <mis@pld.org.pl>

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

#include <trurl/nmalloc.h>

#include "cli.h"
#include "op.h"

struct poclidek_opgroup_rt *poclidek_opgroup_rt_new(struct poldek_ts *ts)
{
    struct poclidek_opgroup_rt *rt;

    rt = n_malloc(sizeof(*rt));
    rt->ctx = ts->ctx;
    rt->ts = ts;
    rt->_opdata = NULL;
    rt->_opdata_free = NULL;
    return rt;
};

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
};
