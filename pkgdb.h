/* $Id$ */
#ifndef POLDEK_PKGDB_H
#define POLDEK_PKGDB_H

#include <sys/types.h>
#include <fcntl.h>

#include "capreq.h"
#include "pkg.h"
#include "rpm/rpm.h"

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

struct poldek_ts;
int pkgdb_install(struct pkgdb *db, const char *path,
                  const struct poldek_ts *ts);

int pkgdb_match_req(struct pkgdb *db, const struct capreq *req, int strict,
                    tn_array *excloffs);

int pkgdb_map(struct pkgdb *db,
              void (*mapfn)(unsigned recno, void *header, void *arg),
              void *arg);

#endif
