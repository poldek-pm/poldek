/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_PM_RPM_MODULE_H
#define POLDEK_PM_RPM_MODULE_H

#include <stdio.h>              /* FILE* */
#include <unistd.h>             /* size_t */
#include <stdint.h>             /* uint32_t */
#include <sys/time.h>           /* timeval */

#include <rpm/rpmlib.h>
#include <rpm/rpmurl.h>

#include <rpm/rpmds.h>
#include <rpm/rpmcli.h>
#include <rpm/rpmio.h>
#include <rpm/rpmts.h>
#include <rpm/rpmps.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmmacro.h>

/* RPMTAG_COPYRIGHT disappears in 4.4.3, not sure when RPMTAG_LICENSE begins */
#ifdef RPMTAG_COPYRIGHT
# define PM_RPMTAG_LICENSE RPMTAG_COPYRIGHT
#else
# define PM_RPMTAG_LICENSE RPMTAG_LICENSE
#endif

#include <trurl/trurl.h>
#include "poldek.h"
#include "pm/pm.h"

#define PM_RPM_CMDSETUP_DONE (1 << 0)
struct pm_rpm {
    unsigned flags;
    char *rpm;
    char *sudo;
    char *default_dbpath;
    tn_array *caps;             /* rpmlib() and friends */
};

void *pm_rpm_init(void);
void pm_rpm_destroy(void *pm_rpm);
int pm_rpm_configure(void *modh, const char *key, void *val);
int pm_rpm_conf_get(void *pm_rpm, const char *key, char *value, int vsize);

tn_array *pm_rpm_rpmlib_caps(void *pm_rpm);
int pm_rpm_satisfies(void *pm_rpm, const struct capreq *req);

char *pm_rpm_dbpath(void *pm_rpm, char *path, size_t size);
time_t pm_rpm_dbmtime(void *pm_rpm, const char *dbfull_path);
int pm_rpm_dbdepdirs(void *pm_rpm, const char *rootdir, const char *dbpath,
                     tn_array *depdirs);

int pm_rpm_packages_install(struct pkgdb *db, const tn_array *pkgs,
                            const tn_array *pkgs_toremove, struct poldek_ts *ts);

int pm_rpm_packages_uninstall(struct pkgdb *db, const tn_array *pkgs,
                              struct poldek_ts *ts);

#include <rpm/rpmcli.h>

int pm_rpm_verify_signature(void *pm_rpm, const char *path, unsigned flags);

struct rpmorg_db {
    rpmts ts;
    rpmdb db;
};

struct rpmorg_db *pm_rpm_opendb(void *pm_rpm, void *dbh,
                                const char *dbpath, const char *rootdir, mode_t mode,
                                tn_hash *kw);
void pm_rpm_closedb(struct rpmorg_db *db);


int pm_rpm_db_it_init(struct pkgdb_it *it, int tag, const char *arg);
int pm_rpm_install_package(struct pkgdb *db, const char *path,
                           const struct poldek_ts *ts);

int pm_rpm_vercmp(const char *one, const char *two);

/************/
/*  rpmhdr  */
int pm_rpmhdr_loadfdt(FD_t fdt, Header *hdr, const char *path);
int pm_rpmhdr_loadfile(const char *path, Header *hdr);
Header pm_rpmhdr_readfdt(void *fdt); /* headerRead */

int pm_rpmhdr_nevr(void *h, const char **name, int32_t *epoch,
                   const char **version, const char **release,
                   const char **arch, uint32_t *color);

tn_array *pm_rpmhdr_langs(Header h);
int pm_rpmhdr_get_entry(Header h, int32_t tag, void *buf, int32_t *type, int32_t *cnt);
int pm_rpmhdr_get_raw_entry(Header h, int32_t tag, void *buf, int32_t *cnt);
void pm_rpmhdr_free_entry(void *e, int type);

int pm_rpmhdr_get_string(Header h, int32_t tag, char *value, int size);
int pm_rpmhdr_get_int(Header h, int32_t tag, uint32_t *value);

int pm_rpmhdr_issource(Header h);

void *pm_rpmhdr_link(void *h);
void pm_rpmhdr_free(void *h);

char *pm_rpmhdr_snprintf(char *buf, size_t size, Header h);

#include <rpm/rpmtd.h>
struct rpmhdr_ent {
    int32_t tag;
    int32_t type;
    void *val;
    int32_t cnt;
    struct rpmtd_s *td;
};

#define pm_rpmhdr_ent_as_str(ent) (char*)(ent)->val
#define pm_rpmhdr_ent_as_strarr(ent) (char**)(ent)->val
#define pm_rpmhdr_ent_as_intarr(ent) (uint32_t*)(ent)->val

int pm_rpmhdr_ent_get(struct rpmhdr_ent *ent, Header h, int32_t tag);
void pm_rpmhdr_ent_free(struct rpmhdr_ent *ent);


struct pkg *pm_rpm_ldhdr(tn_alloc *na, Header h,
                         const char *fname, unsigned fsize,
                         unsigned ldflags);

struct pkg *pm_rpm_ldpkg(void *pm_rpm,
                         tn_alloc *na, const char *path, unsigned ldflags);

int pm_rpm_ldhdr_fl(tn_alloc *na, tn_tuple **fl,
                    Header h, int which, const char *pkgname);

tn_array *pm_rpm_ldhdr_capreqs(tn_array *arr, const Header h, int crtype);
int pm_rpm_machine_score(void *pm_rpm, int tag, const char *val);

struct pkgdir;
struct pkgdir *pm_rpm_db_to_pkgdir(void *pm_rpm, const char *rootdir,
                                   const char *dbpath, unsigned pkgdir_ldflags,
                                   tn_hash *kw);

int pm_rpm_arch_score(const char *arch);
int pm_rpm_vercmp(const char *one, const char *two);


void pm_rpm_setup_commands(struct pm_rpm *pm);

#endif
