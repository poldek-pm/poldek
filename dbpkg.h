/* $Id$ */
#ifndef POLDEK_DBPKG_H
#define POLDEK_DBPKG_H

#include <stdint.h>
#include <rpm/rpmlib.h>
#include <trurl/narray.h>

#define DBPKG_ORPHANS_PROCESSED  (1 << 15) /* is its orphan processed ?*/
#define DBPKG_DEPS_PROCESSED     (1 << 16) /* is its deps processed? */
#define DBPKG_TOUCHED            (1 << 17)
#define DBPKG_UNIST_NOTFOLLOW    (1 << 18) /* see uninstall.c */
#define DBPKG_UNIST_MATCHED      (1 << 19) /* see uninstall.c */

struct dbpkg {
    uint32_t    flags;
    uint32_t    recno;         /* rec offset in rpmdb */
    unsigned    ldflags;       /* PKG_LD* */
    struct pkg  *pkg;
};


struct dbpkg *dbpkg_new(uint32_t recno, Header h, unsigned ldflags);
void dbpkg_free(struct dbpkg *dbpkg);
void dbpkg_clean(struct dbpkg *dbpkg);

int dbpkg_cmp(const struct dbpkg *p1, const struct dbpkg *p2);

int dbpkg_pkg_cmp_evr(const struct dbpkg *dbpkg, const struct pkg *pkg);

char *dbpkg_snprintf(char *buf, size_t size, const struct dbpkg *dbpkg);
char *dbpkg_snprintf_s(const struct dbpkg *dbpkg);


tn_array *dbpkg_array_new(int size);
int dbpkg_array_has(tn_array *dbpkgs, unsigned recno);
int dbpkg_array_has_pkg(tn_array *dbpkgs, const struct pkg *pkg);

tn_array *dbpkgs_to_pkgs(tn_array *dbpkgs);

#endif    
