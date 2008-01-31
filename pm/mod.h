#ifndef POLDEK_PMMOD_H
#define POLDEK_PMMOD_H

#include <stdint.h>
#include <stdlib.h>
#include <time.h>               /* for time_t */
#include <sys/types.h>          /* for mode_t */

#include <trurl/trurl.h>
#include <trurl/nmalloc.h>

struct pkgdir;
struct capreq;
struct pkgdb;
struct poldek_ts; 
struct pkgdb_it;
struct pm_dbrec;

struct pm_module {
    unsigned                    cap_flags;
    char                        *name;

    void *(*init)(void);
    void (*destroy)(void *modh);
    int  (*configure)(void *modh, const char *key, void *val);
    int  (*conf_get)(void *modh, const char *key, char *value, int vsize);
    
    int (*pm_satisfies)(void *modh, const struct capreq *req);
    
    char *(*dbpath)(void *modh, char *path, size_t size);
    time_t (*dbmtime)(void *modh, const char *path);
    int (*dbdepdirs)(void *modh, const char *rootdir, const char *dbpath, 
                     tn_array *depdirs);
    
    void *(*dbopen)(void *modh, void *dbh, const char *rootdir,
                    const char *path, mode_t mode, tn_hash *kw);
    void (*dbclose)(void *dbh);

    void (*dbtxbegin)(void *dbh, struct poldek_ts *ts);
    int  (*dbtxcommit)(void *dbh);

    void (*dbfree)(void *dbh);
    
    int (*db_it_init)(struct pkgdb_it *it, int tag, const char *arg);

    int (*dbinstall)(struct pkgdb *db, const char *path,
                     const struct poldek_ts *ts);
    
    int (*pkg_vercmp)(const char *one, const char *two);

    int (*pm_install)(struct pkgdb *db, const tn_array *pkgs,
                      const tn_array *pkgs_toremove, struct poldek_ts *ts);
    
    int (*pm_uninstall)(struct pkgdb *db, const tn_array *pkgs,
                        struct poldek_ts *ts);
    
    int (*pkg_verify_sign)(void *modh, const char *path, unsigned flags);
    

    int (*hdr_nevr)(void *hdr, char **name,
                    int32_t *epoch, char **ver, char **rel,
                    char **arch, int *color);

    void *(*hdr_link)(void *hdr);
    void  (*hdr_free)(void *hdr);


    struct pkg *(*hdr_ld)(tn_alloc *na, void *hdr,
                          const char *fname, unsigned fsize,
                          unsigned ldflags);
    
    tn_array *(*hdr_ld_capreqs)(tn_array *caps, void *hdr, int captype);

    struct pkg *(*ldpkg)(void *modh, tn_alloc *na, const char *path,
                         unsigned ldflags);

    struct pkgdir *(*db_to_pkgdir)(void *pm_rpm, const char *rootdir,
                                   const char *dbpath, unsigned pkgdir_ldflags,
                                   tn_hash *kw);
    int (*machine_score)(void *modh, int tag, const char *val);
};

int pm_module_register(const struct pm_module *mod);
const struct pm_module *pm_module_find(const char *name);

#endif 
