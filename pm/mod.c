/*
  Copyright (C) 2000 - 2004 Pawel A. Gajda <mis@k2.net.pl>

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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nhash.h>
#include <trurl/n_snprintf.h>
#include <trurl/nmalloc.h>

#define PM_INTERNAL

#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pm.h"
#include "mod.h"

static tn_hash *modules_h = NULL;

extern struct pm_module pm_module_rpm;
extern struct pm_module pm_module_pset;

static struct pm_module *mod_tab[] = {
    &pm_module_rpm,
    &pm_module_pset,
    NULL
};

int pmmodule_init(void) 
{
    int i;

    i = 0;
    while (mod_tab[i]) {
        pm_module_register(mod_tab[i]);
        i++;
    }
    
    return i;
}


int pm_module_register(const struct pm_module *mod) 
{
    if (modules_h == NULL) {
        modules_h = n_hash_new(16, NULL);
        n_hash_ctl(modules_h, TN_HASH_NOCPKEY);
    }
    
    n_assert(mod->name);
    if (n_hash_exists(modules_h, mod->name)) {
        logn(LOGERR, "%s: module is already registered", mod->name);
        return 0;
    }

    n_hash_insert(modules_h, mod->name, mod);
    return 1;
}


const struct pm_module *pm_module_find(const char *name)
{
    if (modules_h == NULL)
        return NULL;
    
    return n_hash_get(modules_h, name);
}



