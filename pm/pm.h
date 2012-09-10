/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_PM_H
#define POLDEK_PM_H

#include <sys/types.h>          /* for O_* */
#include <fcntl.h>

#include <trurl/trurl.h>
#include "poldek.h"

#ifndef EXPORT
#  define EXPORT extern
#endif

EXPORT int pmmodule_init(void);

struct pm_module;
struct pkgdb;

struct pm_ctx {
    const struct pm_module  *mod;
    void                    *modh;
};

EXPORT struct pm_ctx *pm_new(const char *name);
EXPORT void pm_free(struct pm_ctx *ctx);

EXPORT int pm_configure(struct pm_ctx *ctx, const char *key, void *val);
EXPORT int pm_conf_get(struct pm_ctx *ctx, const char *key, char *value, int vsize);

EXPORT const char *pm_get_name(struct pm_ctx *ctx);


EXPORT char *pm_dbpath(struct pm_ctx *ctx, char *path, size_t size);
EXPORT time_t pm_dbmtime(struct pm_ctx *ctx, const char *path);

EXPORT int pm_pminstall(struct pkgdb *db, const tn_array *pkgs,
                 const tn_array *pkgs_toremove, struct poldek_ts *ts);

EXPORT int pm_pmuninstall(struct pkgdb *db, const tn_array *pkgs, struct poldek_ts *ts);

EXPORT int pm_verify_signature(struct pm_ctx *ctx, const char *path, unsigned flags);

struct pm_dbrec *dbrec;
typedef int (*pkgdb_filter_fn) (struct pkgdb *db,
                                const struct pm_dbrec *dbrec, void *arg);

struct pkgdb {
    void     *dbh;
    char     *path;
    char     *rootdir;
    mode_t   mode;
    tn_hash  *kw;
    int16_t  _opened;
    uint16_t _txcnt;

    /* user filter fn */
    pkgdb_filter_fn _filter;
    void            *_filter_arg;
    
    struct pm_ctx *_ctx;
};

EXPORT struct pkgdb *pkgdb_open(struct pm_ctx *ctx, const char *rootdir,
                         const char *path, mode_t mode,
                         const char *key, ...);
#define pkgdb_creat(ctx, rootdir, path, key, args...) \
    pkgdb_open(ctx, rootdir, path, O_RDWR | O_CREAT | O_EXCL, key, ##args)

EXPORT int pkgdb_reopen(struct pkgdb *db, mode_t mode);

EXPORT pkgdb_filter_fn pkgdb_set_filter(struct pkgdb *db,
                                 pkgdb_filter_fn filter,
                                 void *filter_arg);

EXPORT void pkgdb_close(struct pkgdb *db);
EXPORT void pkgdb_free(struct pkgdb *db);

EXPORT int pkgdb_tx_begin(struct pkgdb *db, struct poldek_ts *ts);
EXPORT int pkgdb_tx_commit(struct pkgdb *db);

struct poldek_ts;
EXPORT int pkgdb_install(struct pkgdb *db, const char *path,
                  const struct poldek_ts *ts);

EXPORT int pkgdb_match_req(struct pkgdb *db,
                    const struct capreq *req, unsigned ma_flags,
                    const tn_array *exclude);

struct pm_dbrec {
    unsigned  recno;
    void      *hdr;
    struct pm_ctx *_ctx;
};

EXPORT int pm_dbrec_nevr(const struct pm_dbrec *dbrec, char **name, int32_t *epoch,
                  char **ver, char **rel, char **arch, int *color);

                  
EXPORT int pkgdb_is_pkg_installed(struct pkgdb *db, const struct pkg *pkg, int *cmprc);
EXPORT int pkgdb_get_package_hdr(struct pkgdb *db, const struct pkg *pkg,
                          struct pm_dbrec *dbrec);


enum capreq_type {
    PMCAP_CAP  = 1, 
    PMCAP_REQ  = 2, 
    PMCAP_CNFL = 3,
    PMCAP_OBSL = 4,
    PMCAP_SUG  = 5
};

/* pkgdb_it */
enum pkgdb_it_tag {
    PMTAG_RECNO = 0,
    PMTAG_NAME  = 1,
    PMTAG_CAP   = 2,
    PMTAG_REQ   = 3,
    PMTAG_CNFL  = 4,
    PMTAG_OBSL  = 5,
    PMTAG_FILE  = 6,
    PMTAG_DIRNAME  = 7
};

struct pkgdb_it {
    struct pkgdb   *_db;
    void           *_it;
    struct pkg     *pkg;
    
    /* user filter fn */
    pkgdb_filter_fn _filter;
    void            *_filter_arg;
    
    const struct pm_dbrec* (*_get)(struct pkgdb_it *it);
    int           (*_get_count)(struct pkgdb_it *it);
    void          (*_destroy)(struct pkgdb_it *it);
};

EXPORT int pkgdb_it_init(struct pkgdb *db, struct pkgdb_it *it,
                  int tag, const char *arg);

EXPORT pkgdb_filter_fn pkgdb_it_set_filter(struct pkgdb_it *it,
                                    pkgdb_filter_fn filter,
                                    void *filter_arg);

EXPORT void pkgdb_it_destroy(struct pkgdb_it *it);
EXPORT const struct pm_dbrec *pkgdb_it_get(struct pkgdb_it *it);
EXPORT int pkgdb_it_get_count(struct pkgdb_it *it);


/* Search database for value of a tag ignoring packages
   from 'exclude' array. Found packages are added to dbpkgs
   array (created if NULL), returns number of packages found */
EXPORT int pkgdb_search(struct pkgdb *db, tn_array **dbpkgs,
                 enum pkgdb_it_tag tag, const char *value,
                 const tn_array *exclude, unsigned ldflags);


EXPORT int pkgdb_q_what_requires(struct pkgdb *db, tn_array *dbpkgs,
                          const struct capreq *cap,
                          const tn_array *exclude, unsigned ldflags,
                          unsigned ma_flags);

EXPORT int pkgdb_q_is_required(struct pkgdb *db, const struct capreq *cap,
                        const tn_array *exclude);


#define PKGDB_GETF_OBSOLETEDBY_NEVR (1 << 0)  /* by NEVR only  */
#define PKGDB_GETF_OBSOLETEDBY_OBSL (1 << 1)  /* by Obsoletes  */
#define PKGDB_GETF_OBSOLETEDBY_REV  (1 << 10) /* reverse match */

/*
  adds to dbpkgs packages obsoleted by pkg
*/
EXPORT int pkgdb_q_obsoletedby_pkg(struct pkgdb *db, tn_array *dbpkgs,
                            const struct pkg *pkg, unsigned flags,
                            const tn_array *exclude, unsigned ldflags);


enum pm_machine_score_tag {
    PMMSTAG_ARCH = 1,
    PMMSTAG_OS = 2
};
/* RET 0 - different arch/os */
EXPORT int pm_machine_score(struct pm_ctx *ctx,
                     enum pm_machine_score_tag tag, const char *val);

EXPORT int pm_satisfies(struct pm_ctx *ctx, const struct capreq *req);

EXPORT int pm_get_dbdepdirs(struct pm_ctx *ctx,
                     const char *rootdir, const char *dbpath,
                     tn_array *depdirs);

EXPORT struct pkg *pm_load_package(struct pm_ctx *ctx,
                            tn_alloc *na, const char *path, unsigned ldflags);
EXPORT struct pkgdir;
EXPORT struct pkgdir *pkgdb_to_pkgdir(struct pm_ctx *ctx, const char *rootdir,
                               const char *path, unsigned pkgdir_ldflags,
                               const char *key, ...);

#endif
