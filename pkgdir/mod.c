/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pkgdir.h"
#include "pkgdir_intern.h"

static tn_hash *modules_h = NULL;

extern struct pkgdir_module pkgdir_module_pndir;
//extern struct pkgdir_module pkgdir_module_pdir;
extern struct pkgdir_module pkgdir_module_dir;
//extern struct pkgdir_module pkgdir_module_hdrl;
extern struct pkgdir_module pkgdir_module_rpmdb;
//extern struct pkgdir_module pkgdir_module_yum;
extern struct pkgdir_module pkgdir_module_rpmdbcache;

#if WITH_METADATA_REPOSITORY
extern struct pkgdir_module pkgdir_module_metadata;
#endif

static struct pkgdir_module *mod_tab[] = {
    &pkgdir_module_pndir,
    //    &pkgdir_module_pdir,
    &pkgdir_module_rpmdb,
    &pkgdir_module_dir,
    //    &pkgdir_module_hdrl,
    //    &pkgdir_module_yum,
#if WITH_METADATA_REPOSITORY
    &pkgdir_module_metadata,
#endif
    &pkgdir_module_rpmdbcache,
    NULL
};

static
int pkgdir_mod_register(struct pkgdir_module *mod);

static int pkgdir_type_uinf_cmp(struct pkgdir_type_uinf *inf1,
                                struct pkgdir_type_uinf *inf2)
{
    return strcmp(inf1->name, inf2->name);
}


tn_array *pkgdir_typelist(void)
{
    tn_array *list;
    struct pkgdir_type_uinf *inf;
    int i = 0;

    list = n_array_new(16, free, (tn_fn_cmp)pkgdir_type_uinf_cmp);
    while (mod_tab[i]) {
        int n;
        struct pkgdir_module *mod = mod_tab[i++];

        if (mod->cap_flags & PKGDIR_CAP_INTERNALTYPE)
            continue;

        inf = n_malloc(sizeof(*inf));
        snprintf(inf->name, sizeof(inf->name), "%s", mod->name);

        n = 0;
        inf->mode[n++] = mod->load ? 'r' : '-';
        inf->mode[n++] = mod->create ? 'w' : '-';
        inf->mode[n++] = mod->update || mod->update_a ? 'u' : '-';
        inf->mode[n] = '\0';
        n_assert(n < (int)sizeof(inf->mode));

        inf->aliases[0] = '\0';
        if (mod->aliases) {
            int ii = 0, n = 0;
            while (mod->aliases[ii] != NULL) {
                n += n_snprintf(&inf->aliases[n], sizeof(inf->aliases) - n, "%s%s",
                                mod->aliases[ii],
                                mod->aliases[ii + 1] ? ", ": "");
                ii++;
            }
        }

        snprintf(inf->description, sizeof(inf->description), "%s",
                 mod->description);

        n_array_push(list, inf);
    }
    n_array_sort(list);
    return list;
}



int pkgdirmodule_init(void)
{
    int i;

    i = 0;
    while (mod_tab[i]) {
        DBGF("%s\n", mod_tab[i]->name);
        pkgdir_mod_register(mod_tab[i]);
        i++;
    }

    return i;
}

void setup_mod_cap_flags(struct pkgdir_module *mod)
{
    if (mod->update_a)
        mod->cap_flags |= PKGDIR_CAP_UPDATEABLE;
    else
        mod->cap_flags &= ~(PKGDIR_CAP_UPDATEABLE);

    if (mod->update)
        mod->cap_flags |= PKGDIR_CAP_UPDATEABLE_INC;
    else
        mod->cap_flags &= ~(PKGDIR_CAP_UPDATEABLE_INC);

    if (mod->create)
        mod->cap_flags |= PKGDIR_CAP_SAVEABLE;
    else
        mod->cap_flags &= ~(PKGDIR_CAP_SAVEABLE);
}

static
int pkgdir_mod_register(struct pkgdir_module *mod)
{
    if (modules_h == NULL) {
        modules_h = n_hash_new(21, NULL);
        n_hash_ctl(modules_h, TN_HASH_NOCPKEY);
    }

    if (mod->init_module) {
        const char *name = mod->name;
        mod = mod->init_module(mod);
        if (!mod) {
            logn(LOGERR, "%s: module initialization failed", name);
            return 0;
        }
    }

    n_assert(mod->name);
    if (n_hash_exists(modules_h, mod->name)) {
        logn(LOGERR, "%s: module is already registered", mod->name);
        return 0;
    }

    setup_mod_cap_flags(mod);
    n_hash_insert(modules_h, mod->name, mod);

    if (mod->aliases) {
        int i = 0;

        while (mod->aliases[i] != NULL) {
            if (n_hash_exists(modules_h, mod->aliases[i])) {
                logn(LOGWARN, "%s: module alias is already defined, skipped",
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
