
#include <stdlib.h>
#include <trurl/nassert.h>
#include <trurl/nmalloc.h>

#include "i18n.h"
#include "pkg.h"
#include "dbpkg.h"
#include "log.h"
#include "rpm/rpm.h"
#include "rpm/rpm_pkg_ld.h" 

struct dbpkg *dbpkg_new(uint32_t recno, Header h, unsigned ldflags) 
{
    struct dbpkg *dbpkg;

    n_assert(h);
    dbpkg = n_malloc(sizeof(*dbpkg));
    dbpkg->recno = recno;
    dbpkg->pkg = pkg_ldrpmhdr(h, "db", 0, ldflags);
    dbpkg->pkg->flags |= PKG_DBPKG;
    dbpkg->flags = 0;
    pkg_add_selfcap(dbpkg->pkg);
    return dbpkg;
}

void dbpkg_clean(struct dbpkg *dbpkg) 
{
    if (dbpkg->pkg)
        pkg_free(dbpkg->pkg);

    memset(dbpkg, 0, sizeof(*dbpkg));
}


void dbpkg_free(struct dbpkg *dbpkg) 
{
    dbpkg_clean(dbpkg);
    free(dbpkg);
}


int dbpkg_cmp(const struct dbpkg *p1, const struct dbpkg *p2) 
{
    return p1->recno - p2->recno;
}

char *dbpkg_snprintf(char *buf, size_t size, const struct dbpkg *dbpkg)
{
    pkg_snprintf(buf, size, dbpkg->pkg);
    return buf;
}


char *dbpkg_snprintf_s(const struct dbpkg *dbpkg)
{
    static char buf[256];
    pkg_snprintf(buf, sizeof(buf), dbpkg->pkg);
    return buf;
}


tn_array *dbpkg_array_new(int size) 
{
    tn_array *arr;
    
    arr = n_array_new(size, (tn_fn_free)dbpkg_free, (tn_fn_cmp)dbpkg_cmp);
    n_array_ctl(arr, TN_ARRAY_AUTOSORTED);
    return arr;
}


int dbpkg_array_has(tn_array *dbpkgs, unsigned recno) 
{

    struct dbpkg tmpkg;
    tmpkg.recno = recno;
    return n_array_bsearch(dbpkgs, &tmpkg) != NULL;
}


static void to_pkgs_map_fn(struct dbpkg *dbpkg, tn_array *pkgs)
{
    n_array_push(pkgs, pkg_link(dbpkg->pkg));
}


tn_array *dbpkgs_to_pkgs(tn_array *dbpkgs)
{
    tn_array *pkgs;

    pkgs = pkgs_array_new(n_array_size(dbpkgs));
    n_array_map_arg(dbpkgs, (tn_fn_map2)to_pkgs_map_fn, pkgs);
    n_array_sort(pkgs);
    return pkgs;
}

    
