/* $Id$ */
#ifndef  POLDEK_PKGDIR_H
#define  POLDEK_PKGDIR_H

#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/nmalloc.h>
#include <vfile/vfile.h>

#include "source.h"

int pkgdirmodule_init(void);

extern const char *poldek_conf_PKGDIR_DEFAULT_TYPE;

#define PKGDIR_NAMED              (1 << 0)
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
struct pm_ctx;

struct pkgdir {
    char                 *type;
    char                 *name;            /* name  */
    char                 *path;            /* path | URL        */
    char                 *idxpath;         /* path | URL        */
    char                 *compress;        /* compresion */
    tn_array             *pkgs;            /* struct *pkg[]     */

    int                  _idx_version;     /* internal, handled by particular
                                              modules */
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
    tn_hash             *avlangs_h; 
    tn_array            *langs;           /* used languages      */

    struct pkgdir       *prev_pkgdir;

    struct source       *src;            /* reference to its source (if any) */
    tn_alloc            *na;
    const struct pkgdir_module  *mod;
    void                        *mod_data;
};

#define pkgdir_pr_path(pkgdir) \
   (pkgdir->path ? vf_url_hidepasswd_s(pkgdir->path) : NULL)

#define pkgdir_pr_idxpath(pkgdir) \
   (pkgdir->idxpath ? vf_url_hidepasswd_s(pkgdir->idxpath) : NULL)

#define pkgdir_idstr(p) \
 (((p)->flags & PKGDIR_NAMED) ? (p)->name : vf_url_slim_s((p)->idxpath, 0))

struct pkgdir *pkgdir_malloc(void);
void pkgdir_free(struct pkgdir *pkgdir);



/*
  if pkgdir source is poldeksindex then loading is 2-phase:
   1) pkgdir_open() opens index and reads its header
   2) pkgdir_load() loads index content
*/
#define PKGDIR_OPEN_REFRESH   (1 << 0) /* don't look into cache */
#define PKGDIR_OPEN_DIFF      (1 << 1) /* diff is expected      */
#define PKGDIR_OPEN_ALLANGS   (1 << 2)

struct pkgdir *pkgdir_srcopen(const struct source *src, unsigned flags);

struct pkgdir *pkgdir_open_ext(const char *path, const char *pkg_prefix,
                               const char *type, const char *name,
                               const char *compress,
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

#define PKGDIR_CREAT_NODESC     (1 << 0) /* don't save pkg's user level info */
#define PKGDIR_CREAT_NOFL       (1 << 1) /* don't save pkg's file list       */
#define PKGDIR_CREAT_MINi18n    (1 << 2) /* */
#define PKGDIR_CREAT_NOUNIQ     (1 << 4)
#define PKGDIR_CREAT_NOPATCH    (1 << 5) /* don't create diff */
#define PKGDIR_CREAT_NOCOMPR    (1 << 6) /* create uncompressed index (NFY) */

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
const char *pkgdir_type_default_idxfn(const char *type);
const char *pkgdir_type_default_compr(const char *type);

int pkgdir_isremote(struct pkgdir *pkgdir);

#define PKGDIR_CLEAN_IDX    (1 << 0)
#define PKGDIR_CLEAN_CACHE  (1 << 1)

int pkgdir_clean_cache(const char *type, const char *path, unsigned flags);


struct pkgdir_type_uinf {
    char name[32];
    char aliases[64];
    char description[62];
    char mode[8];
};

tn_array *pkgdir_typelist(void);

#endif /* POLDEK_PKGDIR_H*/
