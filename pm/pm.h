#ifndef POLDEK_PM_H
#define POLDEK_PM_H

#include <sys/types.h>          /* for O_* */
#include <fcntl.h>

#include <trurl/trurl.h>
#include "poldek.h"

int pm_module_init(void);

struct pm_module;
struct pkgdb;

struct pm_ctx {
    const struct pm_module  *mod;
    void                    *modh;
};

struct pm_ctx *pm_new(const char *name);
void pm_free(struct pm_ctx *ctx);

int pm_configure(struct pm_ctx *ctx, const char *key, void *val);
const char *pm_get_name(struct pm_ctx *ctx);


char *pm_dbpath(struct pm_ctx *ctx, char *path, size_t size);
time_t pm_dbmtime(struct pm_ctx *ctx, const char *path);

int pm_pminstall(struct pkgdb *db, tn_array *pkgs, tn_array *pkgs_toremove,
                 struct poldek_ts *ts);

int pm_pmuninstall(struct pkgdb *db, tn_array *pkgs, struct poldek_ts *ts);

int pm_verify_signature(struct pm_ctx *ctx, const char *path, unsigned flags);


struct pkgdb {
    void     *dbh;
    char     *path;
    char     *rootdir;
    mode_t   mode;
    tn_hash  *kw;
    int16_t  _opened;
    uint16_t _txcnt;
    struct pm_ctx *_ctx;
};

struct pkgdb *pkgdb_open(struct pm_ctx *ctx, const char *rootdir,
                         const char *path, mode_t mode,
                         const char *key, ...);
#define pkgdb_creat(ctx, rootdir, path, key, args...) \
    pkgdb_open(ctx, rootdir, path, O_RDWR | O_CREAT | O_EXCL, key, ##args)

int pkgdb_reopen(struct pkgdb *db, mode_t mode);

void pkgdb_close(struct pkgdb *db);
void pkgdb_free(struct pkgdb *db);

int pkgdb_tx_begin(struct pkgdb *db);
int pkgdb_tx_commit(struct pkgdb *db);

struct poldek_ts;
int pkgdb_install(struct pkgdb *db, const char *path,
                  const struct poldek_ts *ts);

int pkgdb_match_req(struct pkgdb *db, const struct capreq *req, int strict,
                    tn_array *excloffs);

int pkgdb_map(struct pkgdb *db,
              void (*mapfn)(unsigned recno, void *header, void *arg),
              void *arg);



int pkgdb_map_nevr(struct pkgdb *db,
                   void (*mapfn)(const char *name, uint32_t epoch,
                                 const char *ver, const char *rel, void *arg),
                   void *arg);


struct pm_dbrec {
    unsigned  recno;
    void      *hdr;
    struct pm_ctx *_ctx;
};

int pkgdb_is_pkg_installed(struct pkgdb *db, const struct pkg *pkg, int *cmprc);


enum capreq_type {
    PMCAP_CAP  = 1, 
    PMCAP_REQ  = 2, 
    PMCAP_CNFL = 3,
    PMCAP_OBSL = 4
};

/* pkgdb_it */
enum pkgdb_it_tag {
    PMTAG_RECNO = 0,
    PMTAG_NAME  = 1,
    PMTAG_CAP   = 2,
    PMTAG_REQ   = 3,
    PMTAG_CNFL  = 4,
    PMTAG_OBSL  = 5,
    PMTAG_FILE  = 6
};

struct pkgdb_it {
    struct pkgdb   *_db;
    void           *_it;
    const struct pm_dbrec* (*_get)(struct pkgdb_it *it);
    int           (*_get_count)(struct pkgdb_it *it);
    void          (*_destroy)(struct pkgdb_it *it);
};

int pkgdb_it_init(struct pkgdb *db, struct pkgdb_it *it,
                  int tag, const char *arg);
void pkgdb_it_destroy(struct pkgdb_it *it);
const struct pm_dbrec *pkgdb_it_get(struct pkgdb_it *it);
int pkgdb_it_get_count(struct pkgdb_it *it);


int pkgdb_get_pkgs_requires_capn(struct pkgdb *db,
                                 tn_array *dbpkgs, const char *capname,
                                 tn_array *unistdbpkgs, unsigned ldflags);

#define PKGDB_GETF_OBSOLETEDBY_NEVR (1 << 0)  /* by NEVR only  */
#define PKGDB_GETF_OBSOLETEDBY_OBSL (1 << 1)  /* by Obsoletes  */
#define PKGDB_GETF_OBSOLETEDBY_REV  (1 << 10) /* reverse match */

/*
  adds to dbpkgs packages obsoleted by pkg
*/
int pkgdb_get_obsoletedby_pkg(struct pkgdb *db, tn_array *dbpkgs,
                              const struct pkg *pkg, unsigned getflags,
                              unsigned ldflags);

tn_array *pkgdb_get_conflicted_dbpkgs(struct pkgdb *db,
                                      const struct capreq *cap,
                                      tn_array *unistdbpkgs, unsigned ldflags);

tn_array *pkgdb_get_provides_dbpkgs(struct pkgdb *db, const struct capreq *cap,
                                    tn_array *unistdbpkgs, unsigned ldflags);

/* returns installed packages which conflicts with given path */
tn_array *pkgdb_get_file_conflicted_dbpkgs(struct pkgdb *db, const char *path,
                                           tn_array *cnfldbpkgs, 
                                           tn_array *unistdbpkgs,
                                           unsigned ldflags);


enum pm_machine_score_tag {
    PMMSTAG_ARCH = 1,
    PMMSTAG_OS = 2
};

int pm_machine_score(struct pm_ctx *ctx,
                     enum pm_machine_score_tag tag, const char *val);

tn_array *pm_get_pmcaps(struct pm_ctx *ctx);

int pm_get_dbdepdirs(struct pm_ctx *ctx,
                     const char *rootdir, const char *dbpath,
                     tn_array *depdirs);

struct pkg *pm_load_package(struct pm_ctx *ctx,
                            tn_alloc *na, const char *path, unsigned ldflags);
struct pkgdir;
struct pkgdir *pkgdb_to_pkgdir(struct pm_ctx *ctx, const char *rootdir,
                               const char *path, const char *key, ...);

#endif
