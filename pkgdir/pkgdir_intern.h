#ifndef POLDEK_PKGDIR_INTERNAL_H
#define POLDEK_PKGDIR_INTERNAL_H

int pkgdirmodule_init(void);
extern const char *poldek_conf_PKGDIR_DEFAULT_TYPE;

#include <trurl/nbuf.h>

struct pkgdir_avlang {
    uint32_t count;
    char     lang[0];
};

tn_hash *pkgdir__avlangs_new(void);
void pkgdir__update_avlangs(struct pkgdir *pkgdir, const char *lang, int count);

void pkgdir__setup_langs(struct pkgdir *pkgdir);
void pkgdir__setup_depdirs(struct pkgdir *pkgdir);
char *pkgdir__setup_pkgprefix(const char *path);

int  pkgdir__uniq(struct pkgdir *pkgdir);
int pkgdir__rmf(const char *dirpath, const char *mask);
//int pkgdir_make_idxpath(char *dpath, int size, const char *type,
//                        const char *path, const char *fn, const char *ext);
char *pkgdir__make_idxpath(char *dpath, int dsize,
                     const char *path, const char *type, const char *compress);

int pkgdir__cache_clean(const char *path, const char *mask);

const char *pkgdir_localidxpath(struct pkgdir *pkgdir);

#include "pkg_store.h"

/* internal module capabilities */
#define PKGDIR_CAP_INTERNALTYPE  (1 << 8) /* do not show it outside  */
#define PKGDIR_CAP_NOSAVAFTUP    (1 << 9) /* needn't saving after update() */
#define PKGDIR_CAP_HANDLEIGNORE  (1 << 10) /* handles ign_patterns internally */


/*  module methods */

typedef int (*pkgdir_fn_open)(struct pkgdir *pkgdir, unsigned flags);
typedef int (*pkgdir_fn_load)(struct pkgdir *pkgdir, unsigned ldflags);

typedef int (*pkgdir_fn_create)(struct pkgdir *pkgdir,
                                const char *path, unsigned flags);

enum pkgdir_uprc {
    PKGDIR_UPRC_NIL = 0,
    PKGDIR_UPRC_UPTODATE = 1,
    PKGDIR_UPRC_UPDATED  = 2,
    PKGDIR_UPRC_ERR_DESYNCHRONIZED = -1,
    PKGDIR_UPRC_ERR_UNKNOWN = -2, 
};


typedef int (*pkgdir_fn_update)(struct pkgdir *pkgdir, enum pkgdir_uprc *uprc);
typedef int (*pkgdir_fn_update_a)(const struct source *src,
                                  const char *idxpath, enum pkgdir_uprc *uprc);

typedef int (*pkgdir_fn_unlink)(const char *path, int allfiles);
typedef void (*pkgdir_fn_free)(struct pkgdir *pkgdir);

typedef const char *(*pkgdir_fn_localidxpath)(struct pkgdir *pkgdir);
typedef int (*pkgdir_fn_setpaths)(struct pkgdir *pkgdir,
                                  const char *path, const char *pkg_prefix);

struct pkgdir_module {
    struct pkgdir_module* (*init_module)(struct pkgdir_module *);
    unsigned                    cap_flags;
    char                        *name;
    char                        **aliases;
    char                        *description;
    char                        *default_fn;
    char                        *default_compr;

    pkgdir_fn_open          open;
    pkgdir_fn_load          load;
    pkgdir_fn_create        create;
    pkgdir_fn_update        update;
    pkgdir_fn_update_a      update_a;
    pkgdir_fn_unlink        unlink;
    pkgdir_fn_free          free;
    
    pkgdir_fn_localidxpath  localidxpath;
    int (*posthook_diff) (struct pkgdir*, struct pkgdir*, struct pkgdir*);
};

//int pkgdir_mod_register(const struct pkgdir_module *mod);
const struct pkgdir_module *pkgdir_mod_find(const char *name);

#endif
