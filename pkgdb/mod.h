#ifndef POLDEK_PKGDB_MOD_H
#define POLDEK_PKGDB_MOD_H

#include <trurl/trurl.h>

typedef int (*pkgdb_fn_open)(const char *rootdir, const char *path, mode_t mode);

typedef void (*pkgdb_fn_close)(struct pkgdb *db);
typedef int (*pkgdb_fn_reopen)(struct pkgdb *db);
typedef void (*pkgdb_fn_free)(struct pkgdb *db);


struct poldek_ts;

typedef int (*pkgdb_fn_install_package)(struct pkgdb *db, const char *path,
                                        const struct poldek_ts *ts);

struct capreq;

typedef
tn_array (*pkgdb_fn_what_requires_cap)(struct pkgdb *db, tn_array *dst,
                                       const struct capreq *cap,
                                       tn_array *skipl);


typedef
tn_array (*pkgdb_fn_what_provides_cap)(struct pkgdb *db, tn_array *dst,
                                       const struct capreq *cap,
                                       tn_array *skipl);


typedef
int (*pkgdb_fn_match_req)(struct pkgdb *db, const struct capreq *req, int strict,
                          tn_array *skipl);


typedef
tn_array (*pkgdb_fn_what_conflicted_w_cap)(struct pkgdb *db, tn_array *dst,
                                           const struct capreq *cap,
                                           tn_array *skipl);

typedef
tn_array (*pkgdb_fn_what_conflicted_w_path)(struct pkgdb *db, tn_array *dst,
                                         const char path *path,
                                         tn_array *skipl);



typedef
tn_array (*pkgdb_fn_what_obsoletedby_pkg)(struct pkgdb *db, tn_array *dst,
                                       const struct pkg *pkg,
                                       tn_array *skipl);


struct pkgdb_module {
    unsigned                    cap_flags;
    char                        *name;
    char                        *description;

    pkgdb_fn_open         open;
    pkgdb_fn_close        close;
    pkgdb_fn_reopen       reopen;
    pkgdb_fn_free         free;

    pkgdb_fn_install_package        install;
    pkgdb_fn_match_req              match_req;
    pkgdb_fn_what_requires_cap      what_requires_cap;
    pkgdb_fn_what_provides_cap      what_provides_cap;
    pkgdb_fn_what_conflicted_w_cap  what_conflicted_w_cap;
    pkgdb_fn_what_conflicted_w_path what_conflicted_w_path;
    pkgdb_fn_what_obsoletedby_pkg   what_obsoletedby_pkg;
    
};

int pkgdb_mod_register(const struct pkgdb_module *mod);
const struct pkgdb_module *pkgdb_mod_find(const char *name);




typedef
int (*pm_fn_verify_package_signature)(const char *path, unsigned flags);

typedef
tn_array *(*pm_fn_get_selfcaps)(void);

typedef
tn_array *(*pm_fn_version_compare)(struct pkg *p1, struct pkg *p2);

struct pm_module {
    unsigned                    cap_flags;
    char                        *name;
    char                        *description;

    pm_fn_verify_package_signature verify_package_signature;
    pm_fn_version_compare          version_compare;
    pm_fn_get_selfcaps             get_selfcaps;
};
