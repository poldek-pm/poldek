/* $Id$ */
#ifndef  POLDEK_PKGDIR_H
#define  POLDEK_PKGDIR_H

#include <trurl/narray.h>

#include <vfile/vfile.h>

struct pkgdir {
    char               *path;       /* path | URL        */
    char               *idxpath;    /* path | URL        */
    tn_array           *depdirs;    /* char *[]          */
    tn_array           *pkgs;       /* struct *pkg[]     */
    struct vfile       *vf;         /* Packages handle   */
    unsigned           flags;
};

struct pkgdir *pkgdir_new(const char *path, const char *pkg_prefix);
void pkgdir_free(struct pkgdir *pkgdir);

/* flags */
#define PKGDIR_LDFROM_DIR         (1 << 0)
#define PKGDIR_LDFROM_IDX         (1 << 1)


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

struct pkgdir *pkgdir_load_dir(const char *path);

int pkgdir_create_idx(struct pkgdir *pkgdir, const char *pathname, int nodesc);


int update_pkgdir_idx(const char *path);


#endif /* POLDEK_PKGDIR_H*/
