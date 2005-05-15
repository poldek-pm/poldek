/*
  Copyright (C) 2000 - 2005 Pawel A. Gajda <mis@k2.net.pl>

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

#define ENABLE_TRACE 0

#include <stdlib.h>
#include <string.h>
#include <sys/param.h>          /* for PATH_MAX */

#include <trurl/nassert.h>
#include <trurl/nhash.h>
#include <trurl/narray.h>
#include <trurl/nmalloc.h>
#include <trurl/nstr.h>

#include "i18n.h"
#include "pkg.h"
#include "capreq.h"
#include "dbpkgset.h"
#include "log.h"

#define DBPKGSET_CHANGED (1 << 0)

struct dbpkg_set *dbpkg_set_new(void) 
{
    struct dbpkg_set *dbpkg_set; 

    dbpkg_set = n_malloc(sizeof(*dbpkg_set));
    dbpkg_set->dbpkgs = pkgs_array_new_ex(128, pkg_cmp_recno);
    dbpkg_set->capcache = n_hash_new(128, NULL);
    return dbpkg_set;
}


void dbpkg_set_free(struct dbpkg_set *dbpkg_set) 
{
    n_array_free(dbpkg_set->dbpkgs);
    n_hash_free(dbpkg_set->capcache);
    free(dbpkg_set);
}

void dbpkg_set_add(struct dbpkg_set *dbpkg_set, struct pkg *dbpkg) 
{
    n_array_push(dbpkg_set->dbpkgs, dbpkg);
}

int dbpkg_set_has_pkg_recno(struct dbpkg_set *dbpkg_set, int recno) 
{
    struct pkg tmpkg;
    tmpkg.recno = recno;
    return n_array_bsearch(dbpkg_set->dbpkgs, &tmpkg) != NULL;
}


int dbpkg_set_has_pkg(struct dbpkg_set *dbpkg_set, const struct pkg *pkg)
{
    int i;

    for (i=0; i<n_array_size(dbpkg_set->dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(dbpkg_set->dbpkgs, i);
        if (pkg_cmp_name_evr(dbpkg, pkg) == 0)
            return 1;
    }
    return 0;
}


const struct pkg *dbpkg_set_provides(struct dbpkg_set *dbpkg_set,
                                     const struct capreq *cap)
{
    char             *dirname, *basename, path[PATH_MAX];
    char             *capnvr = NULL, *capname = NULL;
    int              i, is_file = 0;
    struct pkg       *pkg = NULL;
    

    if (!capreq_has_ver(cap)) {
        capname = (char*)capreq_name(cap);
        
    } else {
        capname = capnvr = alloca(256);
        capreq_snprintf(capnvr, 256, cap);
    }
    
    if ((pkg = n_hash_get(dbpkg_set->capcache, capname))) {
        DBGF("cache hit %s\n", capreq_snprintf_s(cap));
        return pkg;
    }
    
    if (capreq_is_file(cap)) {
        is_file = 1;
        strncpy(path, capreq_name(cap), sizeof(path));
        path[PATH_MAX - 1] = '\0';
        n_basedirnam(path, &dirname, &basename);
        n_assert(dirname);
        n_assert(*dirname);
        if (*dirname == '/' && *(dirname + 1) != '\0')
            dirname++;
    }

    pkg = NULL;
    for (i=0; i < n_array_size(dbpkg_set->dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(dbpkg_set->dbpkgs, i);
        if (is_file && pkg_has_path(dbpkg, dirname, basename))
            pkg = dbpkg;
        
        else if (pkg_match_req(dbpkg, cap, 0))
            pkg = dbpkg;

        DBGF("  -%s provides %s -> %s\n", pkg_snprintf_s(dbpkg),
             capreq_snprintf_s(cap), pkg ? "YES" : "NO");
        
        if (pkg)
            break;
    }

    if (pkg != NULL) {
        DBGF("addto cache %s\n", capnvr ? capnvr : capname);
        if (capnvr == NULL) {
            n_hash_insert(dbpkg_set->capcache, capname, pkg);
            
        } else {
            n_hash_insert(dbpkg_set->capcache, capnvr, pkg);
            if (!n_hash_exists(dbpkg_set->capcache, capname))
                n_hash_insert(dbpkg_set->capcache, capname, pkg);
        }
    }
    DBGF("%s -> %s\n", capreq_snprintf_s(cap),
         pkg ? pkg_snprintf_s(pkg) : "NO");
    return pkg;
}

    
void dbpkg_set_dump(struct dbpkg_set *dbpkg_set)
{
    int i;

    printf("dbpkg_set dump %p: ",  dbpkg_set);
    
        
    for (i=0; i<n_array_size(dbpkg_set->dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(dbpkg_set->dbpkgs, i);
        printf("%s, ", pkg_snprintf_s(dbpkg));
    }
    printf("\n");
}
