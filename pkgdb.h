/* $Id$ */
#ifndef POLDEK_PKGDB_H
#define POLDEK_PKGDB_H

#include <sys/types.h>
//#include <sys/stat.h>
#include <fcntl.h>

#include "capreq.h"
#include "pkg.h"
#include "rpm.h"

#define PKGINST_NODEPS        (1 << 1)
#define PKGINST_JUSTDB        (1 << 2)
#define PKGINST_TEST          (1 << 3)
#define PKGINST_FORCE         (1 << 6)    
#define PKGINST_UPGRADE       (1 << 7) 

struct pkgdb {
    void *dbh;
    char *path;
    char *rootdir;
};

struct pkgdb *pkgdb_open(const char *rootdir, const char *path, mode_t mode);
#define pkgdb_creat(path, rootdir) \
       pkgdb_open(path, rootdir, O_RDWR | O_CREAT | O_EXCL)

void pkgdb_closedb(struct pkgdb *db);
void pkgdb_free(struct pkgdb *db);

int pkgdb_install(struct pkgdb *db, const char *path, unsigned flags);

int pkgdb_match_req(struct pkgdb *db, const struct capreq *req, int strict,
                    tn_array *excloffs);

#define pkgdb_map(db, mapfn, arg) rpm_dbmap((db)->dbh, mapfn, arg);
#endif
