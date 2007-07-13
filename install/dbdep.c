/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda (mis@pld-linux.org)

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
  $Id$
  Module to track dependencies during resolving them; used
  by install/ subsys only
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/nlist.h>
#include <trurl/n_snprintf.h>
#include <trurl/nmalloc.h>


#define ENABLE_TRACE 0
#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "misc.h"
#include "dbdep.h"
#include "pkgfl.h"

extern int poldek_conf_MULTILIB;

static void db_dep_free_pkgs(struct db_dep *db_dep) 
{
    if (db_dep->pkgs) {
        n_array_free(db_dep->pkgs);
        db_dep->pkgs = NULL;
    }
}


static
void db_dep_free(struct db_dep *db_dep) 
{
    
    db_dep_free_pkgs(db_dep);
    db_dep->req = NULL;
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
    const char     *key;
    int            found = 0;
    tn_list        *l = NULL;


    //if (strcmp(capreq_name(req), "libglade2-devel") == 0) {
    //    printf("db_deps_add %s\n", capreq_snprintf_s(req));
    //}
    
    DBGF("%s requiredby=%s satisfiedby=%s [type=%s]\n", capreq_snprintf_s(req),
         pkg_id(pkg), spkg ? pkg_id(spkg) : "NONE",
         (flags & DBDEP_FOREIGN) ? "foreign" :
         (flags & DBDEP_DBSATISFIED) ? "db" : "UNKNOWN");

    key = capreq_name(req);

    found = 0;
    if ((l = n_hash_get(db_deph, key))) {
        struct db_dep     *dep;
        tn_list_iterator  it;
        
        n_list_iterator_start(l, &it);
        while ((dep = n_list_iterator_get(&it))) {
            if (dep->req == NULL) {
                dep->req = req;
                dep->spkg = spkg;
                dep->pkgs = n_array_new(4, NULL, (tn_fn_cmp)pkg_cmp_name_evr);
                n_array_push(dep->pkgs, pkg);
                dep->flags = flags;
                
            } else if (capreq_strcmp_evr(dep->req, req) == 0) {
                n_array_push(dep->pkgs, pkg);
                dep->flags |= flags;
                break;
            }
        }
        if (dep) {
            n_array_sort(dep->pkgs);
            n_array_uniq(dep->pkgs);
            found = 1;
        }
    }

    if (!found) {
        struct db_dep *new_dep;
        
        new_dep = n_malloc(sizeof(*new_dep));
        new_dep->req = req;
        new_dep->spkg = spkg;
        new_dep->pkgs = n_array_new(4, NULL, (tn_fn_cmp)pkg_cmp_id);
        n_array_push(new_dep->pkgs, pkg);
        new_dep->flags = flags;

        if (l == NULL) {         /* new entry */
            l = n_list_new(0, (tn_fn_free)db_dep_free, NULL);
            n_hash_insert(db_deph, key, l);
        }
        
        n_list_push(l, new_dep);
    }
    
#if ENABLE_TRACE    
    db_deps_dump(db_deph);
#endif    
}

void db_deps_dump(const tn_hash *db_deph)
{
    tn_array *keys;
    int i;

    msgn(0, "db_deps DUMP");
    keys = n_hash_keys(db_deph);
    n_array_sort(keys);
    
    for (i=0; i<n_array_size(keys); i++) {
        const char *key = n_array_nth(keys, i);
        tn_list *l;

        msgn_i(0, 2, "cap %s", key);
        if ((l = n_hash_get(db_deph, key))) {
            struct db_dep     *dep;
            tn_list_iterator  it;
            
            n_list_iterator_start(l, &it);
            while ((dep = n_list_iterator_get(&it))) {
                int j;
                
                msgn_i(0, 4, "* %s, satisfiedby=%s",
                       dep->req ? capreq_snprintf_s(dep->req) : "NULL",
                       dep->spkg ? pkg_id(dep->spkg) : "NONE");
                if (dep->pkgs)
                    for (j=0; j<n_array_size(dep->pkgs); j++)
                        msgn_i(0, 5, "- %s", pkg_id(n_array_nth(dep->pkgs, j)));
            }
        }
    }
}


static
void db_deps_remove_cap(tn_hash *db_deph, struct pkg *pkg,
                        struct capreq *cap, int requiredonly)
{
    tn_list           *l;
    tn_list_iterator  it;
    struct db_dep     *dep;

        
    if ((l = n_hash_get(db_deph, capreq_name(cap))) == NULL)
        return;
    
    n_list_iterator_start(l, &it);
    while ((dep = n_list_iterator_get(&it))) {
        DBGF("- %s (req=%s, pkg=%s)\n", capreq_snprintf_s(cap),
             dep->req ? capreq_snprintf_s0(dep->req) : "none", pkg_id(pkg));
        
        if (dep->req && cap_match_req(cap, dep->req, 1)) {
            int i, i_del = -1;

            if (!requiredonly) {
                DBGF("rmcap %s (%s) %s!\n", capreq_snprintf_s(cap),
                     capreq_snprintf_s0(dep->req), pkg_id(pkg));
                dep->req = NULL;
                db_dep_free_pkgs(dep);
                continue;
            }
            
            DBGF("rmcap %s (%s) %s?\n", capreq_snprintf_s(cap),
                 capreq_snprintf_s0(dep->req), pkg_id(pkg));
#if ENABLE_TRACE            
            pkgs_array_dump(dep->pkgs, "packages");
#endif            
            
            for (i=0; i < n_array_size(dep->pkgs); i++) {
                DBGF("  %s cmp %s\n", pkg_id(n_array_nth(dep->pkgs, i)),
                     pkg_id(pkg));


                if (pkg_cmp_id(n_array_nth(dep->pkgs, i), pkg) == 0) {
                    n_assert(i_del == -1);
                    i_del = i;
                    if (poldek_conf_MULTILIB &&
                        !pkg_is_colored_like(n_array_nth(dep->pkgs, i), pkg))
                        i_del = -1;
                }
            }
            if (i_del >= 0) {
                DBGF("  --> YES, rmcap %s (%s) %s!\n", capreq_snprintf_s(cap),
                     capreq_snprintf_s0(dep->req), pkg_id(pkg));
                n_array_remove_nth(dep->pkgs, i_del);
                if (n_array_size(dep->pkgs) == 0) {
                    DBGF(" cap %s COMPLETELY REMOVED\n", capreq_snprintf_s0(dep->req));
                    dep->req = NULL;
                    db_dep_free_pkgs(dep);

                }
            }
        }
    }
}


static void remove_files(tn_hash *db_deph, struct pkg *pkg, int load_full_fl) 
{
    tn_tuple         *fl;
    struct pkgflist  *flist = NULL;
    int              i, j;

    fl = pkg->fl;
    if (load_full_fl) {
        if ((flist = pkg_get_flist(pkg)) == NULL ||
            n_tuple_size(flist->fl) == 0)
            return;

        fl = flist->fl;
    }

    if (fl == NULL || n_tuple_size(fl) == 0)
        return;
    
    for (i=0; i < n_tuple_size(fl); i++) {
        struct pkgfl_ent    *flent = n_tuple_nth(fl, i);
        char                tmpbuf[PATH_MAX], *slash = "";
        
        for (j=0; j < flent->items; j++) {
            struct flfile     *f = flent->files[j];
            tn_list           *l;
            tn_list_iterator  it;
            struct db_dep     *dep;
            char              buf[1024];
            int               n;
            

            if (S_ISDIR(f->mode)) {
                struct pkgfl_ent tmpent;
                
                if (*flent->dirname != '/')
                    slash = "/";
                snprintf(tmpbuf, sizeof(tmpbuf), "%s/%s", flent->dirname,
                         f->basename);
                tmpent.dirname = tmpbuf;
                //if (n_array_bsearch(fl, &tmpent))
                //    continue;
            }
            
            n = n_snprintf(buf, sizeof(buf), "%s%s%s%s%s",
                         *flent->dirname == '/' ? "":"/",
                         flent->dirname,
                         *flent->dirname == '/' ? "":"/",
                         f->basename, slash);

            
            if ((l = n_hash_get(db_deph, buf)) == NULL)
                continue;
            
            n_list_iterator_start(l, &it);
            while ((dep = n_list_iterator_get(&it))) {
                if (dep->req && strcmp(capreq_name(dep->req), buf) == 0) {
                    DBGF("rmcap %s (%s)\n", buf, capreq_snprintf_s0(dep->req));
                    dep->req = NULL;
                    db_dep_free_pkgs(dep);
                }
            }
        }
    }
    
    if (flist)
        pkgflist_free(flist);
}


void db_deps_remove_pkg(tn_hash *db_deph, struct pkg *pkg)
{
    int i;

    DBGF("%s\n", pkg_id(pkg));
    
    if (pkg->reqs == NULL)
        return;
        
    for (i=0; i < n_array_size(pkg->reqs); i++)
        db_deps_remove_cap(db_deph, pkg, n_array_nth(pkg->reqs, i), 1);

}


void db_deps_remove_pkg_caps(tn_hash *db_deph, struct pkg *pkg, int load_full_fl)
{
    int i;

    DBGF("%s\n", pkg_id(pkg));
    remove_files(db_deph, pkg, load_full_fl);
    
    if (pkg->caps == NULL)
        return;
    
    for (i=0; i < n_array_size(pkg->caps); i++) {
        struct capreq     *cap = n_array_nth(pkg->caps, i);
        db_deps_remove_cap(db_deph, pkg, cap, 0);
    }
}


#define DBDEP_PROVIDES_PROVIDES (1 << 0)
#define DBDEP_PROVIDES_CONTAINS (1 << 1)

static
struct db_dep *provides_cap(tn_hash *db_deph, struct capreq *cap,
                            unsigned depflags, unsigned flags) 
{
    struct db_dep     *dep = NULL;
    tn_list           *l = NULL;
    
    DBGF("%p %s\n", cap, capreq_snprintf_s0(cap));

    if ((l = n_hash_get(db_deph, capreq_name(cap)))) {
        tn_list_iterator  it;
        
        n_list_iterator_start(l, &it);
        while ((dep = n_list_iterator_get(&it))) {
            int matched = 0;

            if (dep->req == NULL) /* removed */
                continue;

            if (flags & DBDEP_PROVIDES_PROVIDES) {
                matched = cap_match_req(dep->req, cap, 1);
                DBGF("cap_match_req(%s, %s) = %d\n", capreq_snprintf_s(dep->req),
                     capreq_snprintf_s0(cap), matched);
                //if (strcmp(capreq_name(cap), "libglade2-devel") == 0) {
                //    printf("cap_match_req %s %s = %d, %s\n", capreq_snprintf_s(dep->req),
                //           capreq_snprintf_s0(cap), matched,
                //           dep->spkg ? pkg_snprintf_s(dep->spkg) : "NULL");
                //    matched = 0;
                //}
                
                    
                
            } else if (flags & DBDEP_PROVIDES_CONTAINS) {
                matched = cap_xmatch_req(cap, dep->req, POLDEK_MA_PROMOTE_REQEPOCH);
                DBGF("cap_match_req(%s, %s) = %d\n", capreq_snprintf_s(cap),
                     capreq_snprintf_s0(dep->req), matched);
            } else
                n_assert(0);
            
            if (matched) {
                if (depflags && (dep->flags & depflags) == 0)
                    continue;
                break;
            }
        }
    }

    DBGF("%s %d (%d)\n", capreq_snprintf_s(cap), dep ? 1:0, l ? n_list_size(l):0);
    return dep;
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

