/* $Id$ */
#ifndef POLDEK_RPM_H
#define POLDEK_RPM_H

#include <rpm/rpmlib.h>
#include <trurl/narray.h>

#include "capreq.h"

#define RPM_DBPATH  "/var/lib/rpm"

int rpm_initlib(tn_array *macros);

rpmdb rpm_opendb(const char *dbpath, const char *rootdir, mode_t mode);
void rpm_closedb(rpmdb db);

/* is req matched by db packages? */
int rpm_dbmatch_req(rpmdb db, const struct capreq *req, int strict,
                    tn_array *exclrnos);

/*
  Add to pkgs packages which requires cap/header, omit
  packages wich recnos included in exclrnos
 */
int rpm_get_pkgs_requires_cap(rpmdb db, const char *capname,
                              tn_array *exclrnos, tn_array *pkgs);

int rpm_get_pkgs_requires_pkgh(rpmdb db, Header h, tn_array *exclrnos,
                               tn_array *pkgs);


/*
  Add to pkgs packages which requires packages obsoleted by obsl,
  omit packages which recnos included in exclrnos
 */
int rpm_get_pkgs_requires_obsl_pkg(rpmdb db, struct capreq *obsl,
                                   tn_array *exclrnos, tn_array *pkgs);



int rpm_dbmap(rpmdb db, void (*mapfn)(Header header, off_t offs, void *arg),
              void *arg);

int rpm_dbiterate(rpmdb db, tn_array *offs,
                  void (*mapfn)(void *header, off_t offs, void *arg),
                  void *arg);

int rpm_install(rpmdb db, const char *rootdir, const char *path,
                unsigned filterflags, unsigned transflags, unsigned instflags);

/*
  Returns number of packages installed (-1 on error),
  in cmprc compare result of versions
 */
int rpm_is_pkg_installed(rpmdb db, const struct pkg *pkg, int *cmprc);


#endif
