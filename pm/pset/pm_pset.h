#ifndef POLDEK_PM_PSET_MODULE_H
#define POLDEK_PM_PSET_MODULE_H


#include <trurl/trurl.h>
#include "poldek.h"
#include "pm/pm.h"

struct pm_pset {
    char *installer_path;
};

void *pm_pset_init(void *null);
void pm_pset_destroy(void *pm_pset);

int pm_pset_packages_install(struct pkgdb *db,
                             tn_array *pkgs, tn_array *pkgs_toremove,
                             struct poldek_ts *ts);

int pm_pset_packages_uninstall(struct pkgdb *db,
                               tn_array *pkgs, struct poldek_ts *ts);

void *pm_pset_opendb(void *pm_pset, void *dbh,
                     const char *dbpath, const char *rootdir, mode_t mode,
                     tn_hash *kw);
void pm_pset_closedb(void *dbh);
void pm_pset_freedb(void *dbh);

void pm_pset_commitdb(void *dbh);

int pm_pset_db_it_init(struct pkgdb_it *it, int tag, const char *arg);

int pm_psethdr_nevr(void *h, char **name,
                    int32_t *epoch, char **version, char **release);

struct pkg *pm_pset_ldhdr(tn_alloc *na, void *hdr, const char *fname,
                          unsigned fsize, unsigned ldflags);

tn_array *pm_pset_ldhdr_capreqs(tn_array *arr, void *hdr, int crtype);

#endif
