/* $Id$ */
#ifndef POLDEK_RPM_H
#define POLDEK_RPM_H

#include <rpm/rpmlib.h>
#include <trurl/narray.h>

#include "dbpkg.h"
#include "capreq.h"
#include "rpmdb_it.h"

#define RPM_DBPATH  "/var/lib/rpm"

int rpm_initlib(tn_array *macros);
void rpm_define(const char *name, const char *val);
rpmdb rpm_opendb(const char *dbpath, const char *rootdir, mode_t mode);
void rpm_closedb(rpmdb db);
char *rpm_get_dbpath(void);
time_t rpm_dbmtime(const char *dbfull_path);

int rpm_dbmap(rpmdb db,
              void (*mapfn)(unsigned recno, void *header, void *arg),
              void *arg);


tn_array *rpm_get_conflicted_dbpkgs(rpmdb db, const struct capreq *cap,
                                    tn_array *unistdbpkgs, unsigned ldflags);

tn_array *rpm_get_provides_dbpkgs(rpmdb db, const struct capreq *cap,
                                  tn_array *unistdbpkgs, unsigned ldflags);

/* returns installed packages which conflicts with given path */
tn_array *rpm_get_file_conflicted_dbpkgs(rpmdb db, const char *path,
                                         tn_array *cnfldbpkgs, 
                                         tn_array *unistdbpkgs,
                                         unsigned ldflags);


/* is req matched by db packages? */
int rpm_dbmatch_req(rpmdb db, const struct capreq *req, int strict,
                    tn_array *unistdbpkgs);


/*
  returns number of packages installed (-1 on error),
  in cmprc compare result of versions, fill dbpkg
 */

int rpm_is_pkg_installed(rpmdb db, const struct pkg *pkg, int *cmprc,
                         struct dbrec *dbrecp);


tn_array *rpm_get_packages(rpmdb db, const struct pkg *pkg, unsigned ldflags);


int rpm_get_pkgs_requires_capn(rpmdb db, tn_array *dbpkgs, const char *capname,
                               tn_array *unistdbpkgs, unsigned ldflags);


/*
  adds to dbpkgs packages obsoleted by pkg
*/
int rpm_get_obsoletedby_pkg(rpmdb db, tn_array *dbpkgs, const struct pkg *pkg,
                            unsigned ldflags);




int rpm_install(rpmdb db, const char *rootdir, const char *path,
                unsigned filterflags, unsigned transflags, unsigned instflags);

#endif
