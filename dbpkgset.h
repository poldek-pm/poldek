/* $Id$ */
#ifndef POLDEK_DBPKGSET_H
#define POLDEK_DBPKGSET_H

#include <stdint.h>
//#include <rpm/rpmlib.h>
#include <trurl/narray.h>

struct dbpkg;

struct dbpkg_set {
    tn_array     *dbpkgs;                /* array of dbpkg* */
    tn_hash      *capcache;              /* cache of resolved packages caps */
};

struct dbpkg_set *dbpkg_set_new(void);
void dbpkg_set_free(struct dbpkg_set *dbpkg_set);

void dbpkg_set_add(struct dbpkg_set *dbpkg_set, struct dbpkg *dbpkg);
int dbpkg_set_remove(struct dbpkg_set *dbpkg_set, struct dbpkg *dbpkg);

int dbpkg_set_has_recno(struct dbpkg_set *dbpkg_set, int recno);
const struct pkg *dbpkg_set_provides(struct dbpkg_set *dbpkg_set,
                                     const struct capreq *cap);
int dbpkg_set_has_pkg(struct dbpkg_set *dbpkg_set, const struct pkg *pkg);


#endif    
