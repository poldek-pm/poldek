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

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pkgdir.h"

extern struct pkgdir_module pkgdir_module_pndir;
extern struct pkgdir_module pkgdir_module_pdir;
extern struct pkgdir_module pkgdir_module_dir;
extern struct pkgdir_module pkgdir_module_hdrl;
extern struct pkgdir_module pkgdir_module_rpmdb;

static
struct pkgdir_module *pkgdir_mod_tab[] = {
    &pkgdir_module_pndir,
    &pkgdir_module_pdir,
    &pkgdir_module_rpmdb,
    &pkgdir_module_dir,
    &pkgdir_module_hdrl,
    NULL
};


const struct pkgdir_module *pkgdir_find_mod(const char *name)
{
    int i;

    i = 0;
    while (pkgdir_mod_tab[i] != NULL) {
        struct pkgdir_module *mod = pkgdir_mod_tab[i];
        
        if (strcmp(mod->name, name) == 0)
            return mod;

        if (mod->aliases) {
            int j = 0;
            while (mod->aliases[j] != NULL) {
                if (strcmp(mod->aliases[j], name) == 0)
                    return mod;
                j++;
            }
        }
        i++;
    }
    
    return NULL;
}
