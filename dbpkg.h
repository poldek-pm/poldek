/* $Id$ */
#ifndef POLDEK_DBPKG_H
#define POLDEK_DBPKG_H

#include <stdint.h>
#include <rpm/rpmlib.h>
#include <trurl/narray.h>

#define DBPKG_ORPHANS_PROCESSED  (1 << 15) /* is its orphan processed ?*/
#define DBPKG_DEPS_PROCESSED     (1 << 16) /* is its deps processed? */

struct dbpkg {
    uint32_t    flags;
    uint32_t    recno;         /* rec offset in rpmdb */
    unsigned    ldflags;       /* PKG_LD* */
    struct pkg  *pkg;
};


struct dbpkg *dbpkg_new(uint32_t recno, Header h, unsigned ldflags);
struct dbpkg *dbpkg_new_part(uint32_t recno, Header h, unsigned ldflags);
void dbpkg_free(struct dbpkg *dbpkg);
void dbpkg_clean(struct dbpkg *dbpkg);

int dbpkg_cmp(const struct dbpkg *p1, const struct dbpkg *p2);

int dbpkg_pkg_cmp_evr(const struct dbpkg *dbpkg, const struct pkg *pkg);

char *dbpkg_snprintf(char *buf, size_t size, const struct dbpkg *dbpkg);
char *dbpkg_snprintf_s(const struct dbpkg *dbpkg);


tn_array *dbpkg_array_new(int size);
int dbpkg_array_has(tn_array *dbpkgs, unsigned recno);

#endif    
