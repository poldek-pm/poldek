/* $Id$ */
#ifndef POLDEK_PKGDB_H
#define POLDEK_PKGDB_H

#include <sys/types.h>
#include <fcntl.h>

#include "capreq.h"
#include "pkg.h"
#include "rpm/rpm.h"

#define PKGINST_NODEPS        (1 << 1) /* rpm --nodeps */
#define PKGINST_JUSTDB        (1 << 2) /* rpm --justdb */
#define PKGINST_TEST          (1 << 3) /* rpm --test */
#define PKGINST_FORCE         (1 << 6) /* rpm --force */
#define PKGINST_UPGRADE       (1 << 7) /* rpm -U  */

struct pkgdb {
    void    *dbh;
    char    *path;
    char    *rootdir;
    mode_t  mode;
};

struct pkgdb *pkgdb_open(const char *rootdir, const char *path, mode_t mode);
#define pkgdb_creat(path, rootdir) \
       pkgdb_open(path, rootdir, O_RDWR | O_CREAT | O_EXCL)

void pkgdb_closedb(struct pkgdb *db);
int pkgdb_reopendb(struct pkgdb *db);
void pkgdb_free(struct pkgdb *db);

int pkgdb_install(struct pkgdb *db, const char *path, unsigned flags);

int pkgdb_match_req(struct pkgdb *db, const struct capreq *req, int strict,
                    tn_array *excloffs);

int pkgdb_map(struct pkgdb *db,
              void (*mapfn)(unsigned recno, void *header, void *arg),
              void *arg);

#endif
