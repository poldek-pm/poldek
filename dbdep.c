/* 
  Copyright (C) 2000 - 2002 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation (see file COPYING for
  details).

*/

/*
  $Id$
  Module used in pkgset-install.c only
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/nlist.h>

#define ENABLE_TRACE 0
#include "i18n.h"
#include "log.h"
#include "term.h"
#include "pkg.h"
#include "misc.h"
#include "dbdep.h"


static
void db_dep_free(struct db_dep *db_dep) 
{
    if (db_dep->pkgs)
        n_array_free(db_dep->pkgs);
    db_dep->req = NULL;
    db_dep->pkgs = NULL;
    db_dep->spkg = NULL;
    free(db_dep);
}

tn_hash *db_deps_new(void) 
{
    tn_hash *h;
    h = n_hash_new(213, (tn_fn_free)n_list_free);
    return h;
}


void db_deps_add(tn_hash *db_deph, struct capreq *req, struct pkg *pkg,
                 struct pkg *spkg, unsigned flags) 
{
    char           *key;
    int            found;
    tn_list        *l;

    
    DBGF("%s from %s stby %s [%s]\n", capreq_snprintf_s(req),
         pkg_snprintf_s(pkg), spkg ? pkg_snprintf_s0(spkg) : "NULL",
         (flags & DBDEP_FOREIGN) ? "foreign" :
         (flags & DBDEP_DBSATISFIED) ? "db" : "UNKNOWN");

    key = capreq_name(req);

    found = 0;
    if ((l = n_hash_get(db_deph, key))) {
        struct db_dep     *dep;
        tn_list_iterator  it;
        
        n_list_iterator_start(l, &it);
        while ((dep = n_list_iterator_get(&it))) {
            if (capreq_strcmp_evr(dep->req, req) == 0) {
                n_array_push(dep->pkgs, pkg);
                dep->flags |= flags;
                break;
            }
        }
        if (dep)
            found = 1;
    }

    if (!found) {
        struct db_dep *new_dep;
        
        new_dep = malloc(sizeof(*new_dep));
        new_dep->req = req;
        new_dep->spkg = spkg;
        new_dep->pkgs = n_array_new(4, NULL, (tn_fn_cmp)pkg_cmp_name_evr);
        n_array_push(new_dep->pkgs, pkg);
        new_dep->flags = flags;

        if (l)         /* new entry */
            n_list_push(l, new_dep);
        
        else {
            l = n_list_new(0, (tn_fn_free)db_dep_free, NULL);
            n_hash_insert(db_deph, key, l);
        }
    }
}


void db_deps_remove_pkg(tn_hash *db_deph, struct pkg *pkg)
{
    int i;
    return;
    DBGF("%s\n", pkg_snprintf_s(pkg));
    
    if (pkg->reqs == NULL)
        return;
        
    for (i=0; i < n_array_size(pkg->reqs); i++) {
        struct db_dep     *dep;
        struct capreq     *req;
        char              *key;
        tn_list_iterator  it;
        tn_list           *l;

        req = n_array_nth(pkg->reqs, i);
        key = capreq_name(req);

        if ((dep = n_hash_get(db_deph, key)) == NULL)
            continue;

        n_list_iterator_start(l, &it);
        while ((dep = n_list_iterator_get(&it))) {
            DBGF("rm %s (%s: ", pkg_snprintf_s(pkg), capreq_snprintf_s(req));
            n_array_remove(dep->pkgs, pkg);
#if ENABLE_TRACE
            {
                int j;
                for (j=0; j<n_array_size(dep->pkgs); j++) 
                    DBG("%s, ", pkg_snprintf_s(n_array_nth(dep->pkgs, j)));
                DBG("\n");
            }
#endif
            if (n_array_size(dep->pkgs) == 0)
                dep->req = NULL; /* mark for removal */
        }
    }
    
}


void db_deps_remove_pkg_caps(tn_hash *db_deph, struct pkg *pkg)
{
    int i;

    DBGF("%s\n", pkg_snprintf_s(pkg));
    if (pkg->caps == NULL)
        return;
    
    for (i=0; i < n_array_size(pkg->caps); i++) {
        struct capreq     *cap;
        tn_list           *l;
        tn_list_iterator  it;
        struct db_dep     *dep;

        
        cap = n_array_nth(pkg->caps, i);
        if ((l = n_hash_get(db_deph, capreq_name(cap))) == NULL)
            continue;

        n_list_iterator_start(l, &it);
        while ((dep = n_list_iterator_get(&it))) {
            if (dep->req && cap_match_req(cap, dep->req, 1)) {
                DBGF("rmcap %s (%s)\n", capreq_snprintf_s(cap),
                     capreq_snprintf_s0(dep->req));
                dep->req = NULL;
                n_array_free(dep->pkgs);
                dep->pkgs = NULL;
            }
        }
    }
}


#define DBDEP_PROVIDES_PROVIDES (1 << 0)
#define DBDEP_PROVIDES_CONTAINS (1 << 1)

static
struct db_dep *provides_cap(tn_hash *db_deph, struct capreq *cap,
                            unsigned depflags, unsigned flags) 
{
    struct db_dep     *dep;
    tn_list           *l;
    

    DBGF("%s\n", capreq_snprintf_s(cap));

    if ((l = n_hash_get(db_deph, capreq_name(cap)))) {
        tn_list_iterator  it;
        
        n_list_iterator_start(l, &it);
        while ((dep = n_list_iterator_get(&it))) {
            int matched = 0;

            if (dep->req == NULL) /* removed */
                continue;

            if (flags & DBDEP_PROVIDES_PROVIDES) 
                matched = cap_match_req(dep->req, cap, 1);
            else if (flags & DBDEP_PROVIDES_CONTAINS) 
                matched = cap_match_req(cap, dep->req, 1);
            else
                n_assert(0);
            
            if (matched) {
                if (depflags && (dep->flags & depflags) == 0)
                    continue;
                return dep;
            }
        }
    }
    
    return NULL;
}



struct db_dep *db_deps_provides(tn_hash *db_deph, struct capreq *cap,
                                unsigned flags) 
{
    return provides_cap(db_deph, cap, flags, DBDEP_PROVIDES_PROVIDES);
}


struct db_dep *db_deps_contains(tn_hash *db_deph, struct capreq *cap,
                                unsigned flags) 
{
    return provides_cap(db_deph, cap, flags, DBDEP_PROVIDES_CONTAINS);
}

