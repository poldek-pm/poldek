/* $Id$ */
#ifndef  POLDEK_PKGDIR_H
#define  POLDEK_PKGDIR_H

#include <trurl/narray.h>

#include <vfile/vfile.h>

extern const char *default_pkgidx_name;

#define PKGDIR_LDFROM_DIR         (1 << 0)
#define PKGDIR_LDFROM_IDX         (1 << 1)
#define PKGDIR_DIFF               (1 << 4)

struct pkgdir {
    char                *name;
    char                *path;            /* path | URL        */
    char                *idxpath;         /* path | URL        */
    tn_array            *pkgs;            /* struct *pkg[]     */
    
    tn_array            *depdirs;         /* char *[]          */
    tn_array            *foreign_depdirs; /* depdirs not presented in depdirs,
                                            but presented in other pkgdirs */
    struct pkgroup_idx  *pkgroups;
    struct vfile        *vf;              /* packages.dir.gz handle   */
    unsigned            flags;
    time_t              ts;               /* timestamp */

    tn_array            *removed_pkgs;    /* for diffs, removed packages since
                                             ts_orig index */
    time_t              ts_orig;          /* for diffs, ts of .orig idx */
};

struct pkgdir *pkgdir_new(const char *name, const char *path,
                          const char *pkg_prefix);
void pkgdir_free(struct pkgdir *pkgdir);

/* ldflags */
#define PKGDIR_LD_SKIPBASTS   (1 << 10) /* don't load capreqs added by poldek */
#define PKGDIR_LD_FULLFLIST   (1 << 11) /* load full file list */
#define PKGDIR_LD_DESC        (1 << 12) /* load pkg info to memory */


/* for mkidx */
#define PKGDIR_LD_RAW        (PKGDIR_LD_FULLFLIST | \
                             PKGDIR_LD_SKIPBASTS | \
                             PKGDIR_LD_DESC)

/* for verification */
#define PKGDIR_LD_VERIFY   (PKGDIR_LD_FULLFLIST | PKGDIR_LD_SKIPBASTS)

int pkgdir_load(struct pkgdir *pkgdir, tn_array *depdirs, unsigned ldflags);
struct pkgdir *pkgdir_load_dir(const char *name, const char *path);

#define PKGDIR_CREAT_NODESC (1 << 0)
#define PKGDIR_CREAT_INCR   (1 << 1)

int pkgdir_create_idx(struct pkgdir *pkgdir, const char *pathname,
                      unsigned flags);

struct pkgdir *pkgdir_diff(struct pkgdir *pkgdir, struct pkgdir *pkgdir2);

int update_pkgdir_idx(const char *path);

int pkgdir_isremote(struct pkgdir *pkgdir);

#endif /* POLDEK_PKGDIR_H*/
