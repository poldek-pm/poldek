#ifndef POLDEK_PMMOD_H
#define POLDEK_PMMOD_H

#include <stdint.h>
#include <stdlib.h>

#include <trurl/trurl.h>
#include <trurl/nmalloc.h>

struct pkgdb;
struct poldek_ts; 
struct pkgdb_it;
struct pm_dbrec;

struct pm_confent {
    void *ent;
    void (*entfree)(void *);
};

struct pm_confent *pm_confent_new(void *data, void (*entfree)(void *));
void pm_confent_free(struct pm_confent *ent);

struct pm_module {
    unsigned                    cap_flags;
    char                        *name;

    void *(*init)(void *);
    void (*destroy)(void *modh);

    tn_array *(*pm_caps)(void *modh);
    char *(*dbpath)(void *modh, char *path, size_t size);
    time_t (*dbmtime)(void *modh, const char *path);
    int (*dbdepdirs)(void *modh, const char *rootdir, const char *dbpath, 
                     tn_array *depdirs);
    
    void *(*dbopen
           )(void *modh, const char *rootdir,
                    const char *path, mode_t mode);
    void (*dbclose)(void *dbh);
    
    int (*db_it_init)(struct pkgdb_it *it, int tag, const char *arg);

    int (*dbinstall)(struct pkgdb *db, const char *path,
                     const struct poldek_ts *ts);
    
    int (*pkg_vercmp)(const char *one, const char *two);

    int (*pm_install)(void *modh, tn_array *pkgs, tn_array *pkgs_toremove,
                      struct poldek_ts *ts);
    
    int (*pm_uninstall)(void *modh, tn_array *pkgs, struct poldek_ts *ts);
    int (*pkg_verify_sign)(void *modh, const char *path, unsigned flags);
    

    int (*hdr_nevr)(void *hdr, char **name,
                    int32_t *epoch, char **version, char **release);

//    void *(*hdr_link)(void *hdr);
//    void  (*hdr_free)(void *hdr);


    struct pkg *(*hdr_ld)(tn_alloc *na, void *hdr,
                          const char *fname, unsigned fsize,
                          unsigned ldflags);
    
    tn_array *(*hdr_ld_capreqs)(tn_array *caps, void *hdr, int captype);

    struct pkg *(*ldpkg)(void *modh, tn_alloc *na, const char *path, unsigned ldflags);
    int (*machine_score)(void *modh, int tag, const char *val);
};

int pm_module_register(const struct pm_module *mod);
const struct pm_module *pm_module_find(const char *name);

#endif 
