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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nhash.h>

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pkgdir.h"

static tn_hash *modules_h = NULL;

extern struct pkgdir_module pkgdir_module_pndir;
extern struct pkgdir_module pkgdir_module_pdir;
extern struct pkgdir_module pkgdir_module_dir;
extern struct pkgdir_module pkgdir_module_hdrl;
extern struct pkgdir_module pkgdir_module_rpmdb;

int pkgdirmodule_init(void) 
{
    int i;
    
    struct pkgdir_module *mod_tab[] = {
        &pkgdir_module_pndir,
        &pkgdir_module_pdir,
        &pkgdir_module_rpmdb,
        &pkgdir_module_dir,
        &pkgdir_module_hdrl,
        NULL
    };

    i = 0;
    while (mod_tab[i]) {
        pkgdir_mod_register(mod_tab[i]);
        i++;
    }
    
    return i;
}


int pkgdir_mod_register(const struct pkgdir_module *mod) 
{
    if (modules_h == NULL) {
        modules_h = n_hash_new(21, NULL);
        n_hash_ctl(modules_h, TN_HASH_NOCPKEY);
    }
    
    n_assert(mod->name);
    if (n_hash_exists(modules_h, mod->name)) {
        logn(LOGERR, "%s: module is already registered", mod->name);
        return 0;
    }

    n_hash_insert(modules_h, mod->name, mod);

    if (mod->aliases) {
        int i = 0;
        
        while (mod->aliases[i] != NULL) {
            if (n_hash_exists(modules_h, mod->aliases[i])) {
                logn(LOGERR, "%s: module alias is already defined, skipped",
                     mod->aliases[i]);
                i++;
                continue;
            }
            n_hash_insert(modules_h, mod->aliases[i], mod);
            i++;
        }
    }
    
    return 1;
}


const struct pkgdir_module *pkgdir_mod_find(const char *name)
{
    if (modules_h == NULL)
        return NULL;
    
    return n_hash_get(modules_h, name);
}
