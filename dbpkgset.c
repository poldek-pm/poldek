/* 
  Copyright (C) 2000, 2001 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#define ENABLE_TRACE 0

#include <stdlib.h>
#include <string.h>

#include <trurl/nassert.h>
#include <trurl/nhash.h>
#include <trurl/narray.h>

#include "i18n.h"
#include "pkg.h"
#include "dbpkg.h"
#include "dbpkgset.h"
#include "rpmadds.h"
#include "log.h"

#define DBPKGSET_CHANGED (1 << 0)

struct dbpkg_set *dbpkg_set_new(void) 
{
    struct dbpkg_set *dbpkg_set; 

    dbpkg_set = malloc(sizeof(*dbpkg_set));
    dbpkg_set->dbpkgs = dbpkg_array_new(32);
    dbpkg_set->capcache = n_hash_new(103, NULL);
    return dbpkg_set;
}


void dbpkg_set_free(struct dbpkg_set *dbpkg_set) 
{
    n_array_free(dbpkg_set->dbpkgs);
    n_hash_free(dbpkg_set->capcache);
    free(dbpkg_set);
}

void dbpkg_set_add(struct dbpkg_set *dbpkg_set, struct dbpkg *dbpkg) 
{
    n_array_push(dbpkg_set->dbpkgs, dbpkg);
}

int dbpkg_set_has_pkg_recno(struct dbpkg_set *dbpkg_set, int recno) 
{
    return dbpkg_array_has(dbpkg_set->dbpkgs, recno);
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
        DBGMSG_F("cache hit %s\n", capreq_snprintf_s(cap));
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
        struct dbpkg *dbpkg = n_array_nth(dbpkg_set->dbpkgs, i);

        if (is_file && pkg_has_path(dbpkg->pkg, dirname, basename)) {
            pkg = dbpkg->pkg;
            break;
            
        } else if (pkg_match_req(dbpkg->pkg, cap, 0)) {
            pkg = dbpkg->pkg;
            break;
        }
    }

    if (pkg != NULL) {
        DBGMSG_F("addto cache %s\n", capreq_name(cap));
        if (capnvr == NULL) {
            n_hash_insert(dbpkg_set->capcache, capname, pkg);
            
        } else {
            n_hash_insert(dbpkg_set->capcache, capnvr, pkg);
            if (!n_hash_exists(dbpkg_set->capcache, capname))
                n_hash_insert(dbpkg_set->capcache, capname, pkg);
        }
    }
    
    return pkg;
}

    
