/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_PM_PSET_MODULE_H
#define POLDEK_PM_PSET_MODULE_H


#include <trurl/trurl.h>
#include "poldek.h"
#include "pm/pm.h"

struct pm_pset;

void *pm_pset_init(void);
void pm_pset_destroy(void *pm_pset);

int pm_pset_configure(void *pm_pset, const char *key, void *val);

int pm_pset_satisfies(void *pm_pset, const struct capreq *req);

int pm_pset_packages_install(struct pkgdb *db, const tn_array *pkgs,
                             const tn_array *pkgs_toremove,
                             struct poldek_ts *ts);

int pm_pset_packages_uninstall(struct pkgdb *db, const tn_array *pkgs,
                               struct poldek_ts *ts);

void *pm_pset_opendb(void *pm_pset, void *dbh,
                     const char *dbpath, const char *rootdir, mode_t mode,
                     tn_hash *kw);
void pm_pset_closedb(void *dbh);
void pm_pset_freedb(void *dbh);

void pm_pset_tx_begin(void *dbh, struct poldek_ts *ts);
int pm_pset_tx_commit(void *dbh);

int pm_pset_db_it_init(struct pkgdb_it *it, int tag, const char *arg);

int pm_pset_hdr_nevr(void *h, const char **name, int32_t *epoch,
                     const char **ver, const char **rel,
                     const char **arch, uint32_t *color);

void *pm_pset_hdr_link(void *h);
void pm_pset_hdr_free(void *h);

struct pkg *pm_pset_ldhdr(tn_alloc *na, void *hdr, const char *fname,
                          unsigned fsize, unsigned ldflags);

tn_array *pm_pset_ldhdr_capreqs(tn_array *arr, void *hdr, int crtype);

struct pkgdir *pm_pset_db_to_pkgdir(void *pm_pset, const char *rootdir,
                                    const char *dbpath, unsigned pkgdir_ldflags,
                                    tn_hash *kw);

#endif
