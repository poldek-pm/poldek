
#include <stdlib.h>

#include "pkg.h"
#include "dbpkg.h"
#include "rpmadds.h"
#include "log.h"

struct dbpkg *dbpkg_new(uint32_t recno, Header h) 
{
    struct dbpkg *dbpkg;

    dbpkg = malloc(sizeof(*dbpkg));
    dbpkg->recno = recno;
    if (h) 
        dbpkg->h = headerLink(h);
    else
        dbpkg->h = NULL;

    dbpkg->pkg = NULL;
    dbpkg->flags = 0;
    return dbpkg;
}

void dbpkg_clean(struct dbpkg *dbpkg) 
{
    if (dbpkg->h)
        headerFree(dbpkg->h);
    
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
    if (dbpkg->h)
        rpmhdr_snprintf(buf, size, dbpkg->h);
    else 
        snprintf(buf, size, "(null dbpkg->h)");
    return buf;
}


char *dbpkg_snprintf_s(const struct dbpkg *dbpkg)
{
    static char buf[256];
    return dbpkg_snprintf(buf, sizeof(buf), dbpkg);
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



int dbpkg_pkg_cmp_evr(const struct dbpkg *dbpkg, const struct pkg *pkg)
{
    int rc;
    
    if (dbpkg->pkg != NULL) {
        rc = pkg_cmp_evr(dbpkg->pkg, pkg);
        
    } else {
        struct pkg  tmpkg;
        uint32_t    *epoch;
        
        headerNVR(dbpkg->h, (void*)&tmpkg.name, (void*)&tmpkg.ver,
                  (void*)&tmpkg.rel);
        
        if (tmpkg.name == NULL || tmpkg.ver == NULL || tmpkg.rel == NULL) {
            log(LOGERR, "headerNVR failed\n");
            return 0;
        }
        
        if (headerGetEntry(dbpkg->h, RPMTAG_EPOCH, &rc, (void *)&epoch, NULL))
            tmpkg.epoch = *epoch;
        else
            tmpkg.epoch = 0;
        
        rc = pkg_cmp_evr(&tmpkg, pkg);
    }
    
    return rc;
}

    
