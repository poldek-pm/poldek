/* $Id$ */
#ifndef  POLDEK_PKGDIR_H
#define  POLDEK_PKGDIR_H

#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <vfile/vfile.h>

#include "source.h"

int pkgdirmodule_init(void);


#define PKGDIR_DEFAULT_TYPE       "pndir"

#define PKGDIR_LOADED             (1 << 1)  /* for idx */
#define PKGDIR_VERIFIED           (1 << 2)  /* to avoid double verification
                                               during --update */
#define PKGDIR_DIFF               (1 << 3)  /* is patch */
#define PKGDIR_PATCHED            (1 << 4)  /* patched  */
#define PKGDIR_UNIQED             (1 << 5) /* passed through pkgdir_uniq() */

#define PKGDIR_VRFY_GPG            (1 << 10) /* verify package GPG signatures */
#define PKGDIR_VRFY_PGP            (1 << 11) /* verify package PGP signatures */

#define PKGDIR_VRFYSIGN            (PKGDIR_VRFY_GPG | PKGDIR_VRFY_PGP)

#define PKGDIR_NODESC              (1 << 12)

struct pkgdir_module;

struct pkgdir {
    char                 *type;
    char                 *name;            /* name  */
    char                 *path;            /* path | URL        */
    char                 *idxpath;         /* path | URL        */
    tn_array             *pkgs;            /* struct *pkg[]     */

    int                  pri;              /* pri of pkgdir source */
    
    tn_array             *depdirs;         /* char *[]          */
    tn_array             *foreign_depdirs; /* depdirs not presented in depdirs,
                                            but presented in other pkgdirs */
    struct pkgroup_idx  *pkgroups;
    unsigned            flags;            /* PKGDIR_* */
    time_t              ts;               /* timestamp */

    tn_array            *removed_pkgs;    /* for diffs, removed packages */
    time_t              ts_orig;          /* for pathes, ts of .orig idx */


    char                *lc_lang;         /* configured languages ($LC_LANG format) */
//    tn_array            *avlangs;         /* available languages */
    tn_hash             *avlangs_h; 
    tn_array            *langs;           /* used languages      */
    

    const struct pkgdir_module  *mod;
    void                        *mod_data;
};

#define pkgdir_is_type(pkgdir, t) (strcmp((pkgdir)->type, t) == 0)

#define pkgdir_pr_path(pkgdir) \
   (pkgdir->path ? vf_url_hidepasswd_s(pkgdir->path) : NULL)

#define pkgdir_pr_idxpath(pkgdir) \
   (pkgdir->idxpath ? vf_url_hidepasswd_s(pkgdir->idxpath) : NULL)


struct pkgdir *pkgdir_malloc(void);
void pkgdir_free(struct pkgdir *pkgdir);

/*
  if pkgdir source is poldeksindex then loading is 2-phase:
   1) pkgdir_open() opens index and reads its header
   2) pkgdir_load() loads index confents
*/
#define PKGDIR_OPEN_REFRESH   (1 << 0) /* don't look into cache */
#define PKGDIR_OPEN_DIFF      (1 << 1) /* diff is expected      */
#define PKGDIR_OPEN_ALLANGS   (1 << 2)

struct pkgdir *pkgdir_srcopen(const struct source *src, unsigned flags);

struct pkgdir *pkgdir_open_ext(const char *path, const char *pkg_prefix,
                               const char *type, const char *name,
                               unsigned flags, const char *lc_lang);

struct pkgdir *pkgdir_open(const char *path, const char *pkg_prefix,
                           const char *type, const char *name);

/* ldflags */
//#define PKGDIR_LD_SKIPBASTS   (1 << 0) /* don't load capreqs added by poldek */
#define PKGDIR_LD_FULLFLIST   (1 << 1) /* load full file list */
#define PKGDIR_LD_DESC        (1 << 2) /* load pkg info to memory */
#define PKGDIR_LD_NOUNIQ      (1 << 3) /* don't perform pkgdir_uniq() */

int pkgdir_load(struct pkgdir *pkgdir, tn_array *depdirs, unsigned ldflags);


//struct pkgdir *pkgdir_load_db(const char *rootdir, const char *path);
//struct pkgdir *pkgdir_load_dir(const char *name, const char *path);
//struct pkgdir *pkgdir_load_hdl(const char *name, const char *path,
//                               const char *prefix);

#define PKGDIR_CREAT_NODESC     (1 << 0) /* don't save packages user level info */
#define PKGDIR_CREAT_NOFL       (1 << 1) /* don't save packages file list       */
#define PKGDIR_CREAT_MINi18n    (1 << 2) /* */
#define PKGDIR_CREAT_NOUNIQ     (1 << 4)
#define PKGDIR_CREAT_NOPATCH    (1 << 5) /* don't create diff */
#define PKGDIR_CREAT_NOCOMPR    (1 << 6) /* create uncompressed index (NIY) */

int pkgdir_save(struct pkgdir *pkgdir, const char *type,
                const char *path, unsigned flags);

#define pkgdir_create_idx(p, t, path, f) pkgdir_save(p, t, path, f)

struct pkgdir *pkgdir_diff(struct pkgdir *pkgdir, struct pkgdir *pkgdir2);
struct pkgdir *pkgdir_patch(struct pkgdir *pkgdir, struct pkgdir *pkgdir2);

int pkgdir_update(struct pkgdir *pkgdir, int *npatches);
int pkgdir_update_a(const struct source *src);


#define PKGDIR_CAP_NOPREFIX       (1 << 0)
#define PKGDIR_CAP_UPDATEABLE_INC (1 << 1)
#define PKGDIR_CAP_UPDATEABLE     (1 << 2)

#define pkgdir_is_type(p, t) (strcmp((p)->type, t) == 0)

int pkgdir_type_info(const char *type);
const char *pkgdir_type_idxfn(const char *type);
int pkgdir_type_make_idxpath(const char *type, char *path, size_t size,
                             const char *url);

int pkgdir_isremote(struct pkgdir *pkgdir);

#define PKGDIR_CLEAN_IDX    (1 << 0)
#define PKGDIR_CLEAN_CACHE  (1 << 1)

int pkgdir_clean_cache(const char *type, const char *path, unsigned flags);


#ifdef PKGDIR_INTERNAL
#include <trurl/nbuf.h>

void pkgdir_setup_langs(struct pkgdir *pkgdir);
void pkgdir_setup_depdirs(struct pkgdir *pkgdir);
int  pkgdir_uniq(struct pkgdir *pkgdir);
char *pkgdir_setup_pkgprefix(const char *path);
int pkgdir_rmf(const char *dirpath, const char *mask);
int pkgdir_make_idx_url(char *durl, int size,
                        const char *url, const char *filename);

struct pkg;

extern const char *pkgstore_DEFAULT_ARCH;
extern const char *pkgstore_DEFAULT_OS;

#define PKGSTORE_NODESC            (1 << 0)


#define PKGDIR_CREAT_PKG_NOEVR     (1 << 10)
#define PKGDIR_CREAT_PKG_NOARCH    (1 << 11)
#define PKGDIR_CREAT_PKG_NOOS      (1 << 12)
#define PKGDIR_CREAT_PKG_Fv017     (1 << 13)

int pkg_store(const struct pkg *pkg, tn_buf *nbuf, tn_array *depdirs,
              unsigned flags);

int pkg_store_st(const struct pkg *pkg, tn_stream *st, tn_array *depdirs,
                 unsigned flags);

struct pkg_offs {
    off_t  nodep_files_offs;  /* no dep files offset in index */
    off_t  pkguinf_offs;
};


struct pkg *pkg_restore(tn_stream *st, struct pkg *pkg, 
                        tn_array *depdirs, unsigned ldflags,
                        struct pkg_offs *pkgo, const char *fn);


/*  module methods */

typedef int (*pkgdir_fn_open)(struct pkgdir *pkgdir, unsigned flags);
typedef int (*pkgdir_fn_load)(struct pkgdir *pkgdir, unsigned ldflags);

typedef int (*pkgdir_fn_create)(struct pkgdir *pkgdir,
                                const char *path, unsigned flags);

typedef int (*pkgdir_fn_update)(struct pkgdir *pkgdir, int *npatches);
typedef int (*pkgdir_fn_update_a)(const struct source *src);

typedef int (*pkgdir_fn_unlink)(const char *path, int allfiles);
typedef void (*pkgdir_fn_free)(struct pkgdir *pkgdir);

struct pkgdir_module {
    unsigned                    cap_flags;
    char                        *name;
    char                        **aliases;
    char                        *idx_filename;

    pkgdir_fn_open         open;
    pkgdir_fn_load         load;
    pkgdir_fn_create       create;
    pkgdir_fn_update       update;
    pkgdir_fn_update_a     update_a;
    pkgdir_fn_unlink       unlink;
    pkgdir_fn_free         free;
	
    int (*posthook_diff) (struct pkgdir*, struct pkgdir*, struct pkgdir*);
};

int pkgdir_mod_register(const struct pkgdir_module *mod);
const struct pkgdir_module *pkgdir_mod_find(const char *name);



#endif /* PKGDIR_INTERNAL */

#endif /* POLDEK_PKGDIR_H*/
