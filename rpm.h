/* $Id$ */
#ifndef POLDEK_RPM_H
#define POLDEK_RPM_H

#include <rpm/rpmlib.h>
#include <trurl/narray.h>

#include "dbpkg.h"
#include "capreq.h"

#define RPM_DBPATH  "/var/lib/rpm"

int rpm_initlib(tn_array *macros);

rpmdb rpm_opendb(const char *dbpath, const char *rootdir, mode_t mode);
void rpm_closedb(rpmdb db);

int rpm_dbmap(rpmdb db,
              void (*mapfn)(unsigned recno, void *header, void *arg),
              void *arg);


tn_array *rpm_get_conflicted_dbpkgs(rpmdb db, const struct capreq *cap,
                                    tn_array *unistdbpkgs);

tn_array *rpm_get_provides_dbpkgs(rpmdb db, const struct capreq *cap,
                                  tn_array *unistdbpkgs);

/* returns installed packages which conflicts with given path */
tn_array *rpm_get_file_conflicted_dbpkgs(rpmdb db, const char *path,
                                         tn_array *unistdbpkgs);


/* is req matched by db packages? */
int rpm_dbmatch_req(rpmdb db, const struct capreq *req, int strict,
                    tn_array *unistdbpkgs);


/*
  returns number of packages installed (-1 on error),
  in cmprc compare result of versions, fill dbpkg
 */
int rpm_is_pkg_installed(rpmdb db, const struct pkg *pkg, int *cmprc,
                         struct dbpkg *dbpkg);


tn_array *rpm_get_packages(rpmdb db, const struct pkg *pkg);


/*
  adds to dbpkgs packages which requires dbpkg, omit unistdbpkgs
 */
int rpm_get_pkg_pkgreqs(rpmdb db, tn_array *dbpkgs, struct dbpkg *dbpkg,
                        tn_array *unistdbpkgs);


/*
  adds to dbpkgs packages obsoleted by cap
*/
int rpm_get_obsoletedby_cap(rpmdb db, tn_array *dbpkgs, struct capreq *cap);


int rpm_install(rpmdb db, const char *rootdir, const char *path,
                unsigned filterflags, unsigned transflags, unsigned instflags);

#endif
