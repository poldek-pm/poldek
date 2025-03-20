/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef  POLDEK_PKGDIR_H
#define  POLDEK_PKGDIR_H

#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/nmalloc.h>
#include <vfile/vfile.h>

#include "source.h"

#ifndef EXPORT
#  define EXPORT extern
#endif

#define PKGDIR_NAMED              (1 << 0)
#define PKGDIR_LOADED             (1 << 1)  /* for idx */
#define PKGDIR_VERIFIED           (1 << 2)  /* to avoid double verification
                                               during --update */
#define PKGDIR_DIFF               (1 << 3) /* is patch */
#define PKGDIR_PATCHED            (1 << 4) /* patched  */
#define PKGDIR_UNIQED             (1 << 5) /* passed through pkgdir_uniq() */
#define PKGDIR_CHANGED            (1 << 6) /* added/removed packages */
#define PKGDIR_DIFFED             (1 << 7) /*  */

#define PKGDIR_VRFY_GPG            (1 << 10) /* verify package GPG signatures */
#define PKGDIR_VRFY_PGP            (1 << 11) /* verify package PGP signatures */

#define PKGDIR_VRFYSIGN            (PKGDIR_VRFY_GPG | PKGDIR_VRFY_PGP)

struct pkgdir_module;
struct pm_ctx;

struct pkgdir {
    char                 *type;
    char                 *name;            /* name  */
    char                 *path;            /* path | URL        */
    char                 *idxpath;         /* path | URL        */
    char                 *compr;           /* compresion */
    tn_array             *pkgs;            /* struct *pkg[]     */
    tn_array             *_unsorted_pkgs;  /* struct *pkg[], to iterate in 'offset' order */

    int                  _idx_version;     /* internal, handled by particular
                                              modules */
    int                  pri;              /* pri of pkgdir source */

    tn_array             *depdirs;         /* char *[]          */
    tn_array             *foreign_depdirs; /* depdirs not in depdirs[],
                                              but presented in other pkgdirs */
    struct pkgroup_idx  *pkgroups;
    unsigned            flags;            /* PKGDIR_* */
    time_t              ts;               /* timestamp */
    size_t              size;             /* no. of packages */

    tn_array            *removed_pkgs;    /* for diffs, removed packages */
    time_t              orig_ts;          /* for pathes, ts of .orig idx */
    char                *orig_idxpath;

    char                *lc_lang;         /* configured languages ($LC_LANG format) */
    tn_hash             *avlangs_h;       /* all available languages */
    tn_array            *langs;           /* used languages      */

    struct pkgdir_dirindex *dirindex;
    struct pkgdir       *prev_pkgdir;

    struct source       *src;            /* reference to its source (if any) */
    unsigned            _ldflags;        /* internal, to remember ldflags    */
    tn_alloc            *na;

    const struct pkgdir_module  *mod;
    void                        *mod_data;
};

#define pkgdir_pr_path(pkgdir) \
   (pkgdir->path ? vf_url_hidepasswd_s(pkgdir->path) : NULL)

#define pkgdir_pr_idxpath(pkgdir) \
   (pkgdir->idxpath ? vf_url_hidepasswd_s(pkgdir->idxpath) : NULL)

#define pkgdir_idstr(p, buf, size) \
 (((p)->flags & PKGDIR_NAMED) ? (p)->name : vf_url_slim(buf, size, \
    (p)->idxpath ? (p)->idxpath : (p)->path ? (p)->path : "anon", 0))

#define pkgdir_idstr_s(p) \
 (((p)->flags & PKGDIR_NAMED) ? (p)->name : vf_url_slim_s((p)->idxpath ? \
 (p)->idxpath : (p)->path ? (p)->path : "anon", 0))

EXPORT struct pkgdir *pkgdir_malloc(void);
EXPORT void pkgdir_free(struct pkgdir *pkgdir);


/*
  pkgdir loading is 2-phase:
   1) pkgdir_open() opens index and reads its header
   2) pkgdir_load() loads index content
*/
#define PKGDIR_OPEN_REFRESH   (1 << 0) /* don't look into cache   */
#define PKGDIR_OPEN_DIFF      (1 << 1) /* diff is expected        */
#define PKGDIR_OPEN_NODESC    (1 << 2) /* don't open descriptions */
#define PKGDIR_OPEN_ALLDESC   (1 << 3) /* open all i18n descriptions
                                          reasonable for types with
                                          separated i18ns (pndir)
                                        */

EXPORT struct pkgdir *pkgdir_srcopen(const struct source *src, unsigned flags);

EXPORT struct pkgdir *pkgdir_open_ext(const char *path, const char *pkg_prefix,
                               const char *type, const char *name,
                               const char *compress,
                               unsigned flags, const char *lc_lang);

EXPORT struct pkgdir *pkgdir_open(const char *path, const char *pkg_prefix,
                           const char *type, const char *name);

/* ldflags */
#define PKGDIR_LD_FULLFLIST          (1 << 1) /* load full file list */
#define PKGDIR_LD_DESC               (1 << 2) /* load pkg info to memory */
#define PKGDIR_LD_NOUNIQ             (1 << 3) /* don't perform pkgdir_uniq() */
#define PKGDIR_LD_DOIGNORE           (1 << 4) /* honour src->ign_patterns */
#define PKGDIR_LD_DIRINDEX           (1 << 5) /* handle rpm 4.4.6 auto deps */
#define PKGDIR_LD_DIRINDEX_NOCREATE  (1 << 6) /* do not auto-create dirindex */
#define PKGDIR_LD_UPDATE_STUBINDEX   (1 << 7) /* update stub index */
#define PKGDIR_LD_ALLDESC            (1 << 8) /* load all i18n descriptions
				                  (see PKGDIR_OPEN_ALLDESC)
				               */

EXPORT int pkgdir_load(struct pkgdir *pkgdir, const tn_array *depdirs, unsigned ldflags);

#define PKGDIR_CREAT_NODESC   (1 << 0) /* don't save pkg's user level info */
#define PKGDIR_CREAT_NOFL     (1 << 1) /* don't save pkg's file list       */
#define PKGDIR_CREAT_MINi18n  (1 << 2) /* strip i18n info to C and $LANG */
#define PKGDIR_CREAT_NOUNIQ   (1 << 4) /* don't remove duplicates (same NEVR-A)*/
#define PKGDIR_CREAT_NOPATCH  (1 << 5) /* don't create diff */
#define PKGDIR_CREAT_NOCOMPR  (1 << 6) /* create uncompressed index (NFY) */
#define PKGDIR_CREAT_wRECNO   (1 << 7) /* store packages recno if it exists;
                                          honored by pndir only */
#define PKGDIR_CREAT_IFORIGCHANGED  (1 << 8) /* do not recreate if previous
                                                index is up to date */

#define PKGDIR_CREAT_v018x    (1 << 9) /* pdir: do not store package timestamps
                                          cause it brokes inremental updates
                                          by 0.18.x */

EXPORT int pkgdir_save(struct pkgdir *pkgdir, unsigned flags);

EXPORT int pkgdir_save_as(struct pkgdir *pkgdir, const char *type,
                   const char *path, unsigned flags);

EXPORT struct pkgdir *pkgdir_diff(struct pkgdir *pkgdir, struct pkgdir *pkgdir2);
EXPORT struct pkgdir *pkgdir_patch(struct pkgdir *pkgdir, struct pkgdir *pkgdir2);

EXPORT int pkgdir_update(struct pkgdir *pkgdir);
EXPORT int pkgdir_update_a(const struct source *src);


#define PKGDIR_CAP_NOPREFIX       (1 << 0)
#define PKGDIR_CAP_UPDATEABLE_INC (1 << 1)
#define PKGDIR_CAP_UPDATEABLE     (1 << 2)
#define PKGDIR_CAP_SAVEABLE       (1 << 3)
/* before add PKGDIR_CAP_ check pkgdir_intern.h ones! */


#define pkgdir_is_type(p, t) (strcmp((p)->type, t) == 0)

EXPORT int pkgdir_type_info(const char *type);
EXPORT const char *pkgdir_type_default_idxfn(const char *type);
EXPORT const char *pkgdir_type_default_compr(const char *type);

EXPORT int pkgdir_isremote(struct pkgdir *pkgdir);

#if 0   /* not implemented, use source_clean() instead */
#define PKGDIR_CLEAN_IDX    (1 << 0)
#define PKGDIR_CLEAN_CACHE  (1 << 1)
EXPORT int pkgdir_clean_cache(const char *type, const char *path, unsigned flags);
#endif

struct pkgdir_type_uinf {
    char name[32];
    char aliases[64];
    char description[62];
    char mode[8];
};

EXPORT tn_array *pkgdir_typelist(void);

#ifndef SWIG
struct pkg;
EXPORT int pkgdir_add_package(struct pkgdir *pkgdir, struct pkg *pkg);
EXPORT int pkgdir_add_packages(struct pkgdir *pkgdir, tn_array *pkgs);
EXPORT int pkgdir_remove_package(struct pkgdir *pkgdir, struct pkg *pkg);


/* Prototypes of pkgdir_dirindex.c */
/* returns packages having path */
EXPORT tn_array *pkgdir_dirindex_get(const struct pkgdir *pkgdir,
                              tn_array *pkgs, const char *path);
/* path belongs to pkg? */
EXPORT int pkgdir_dirindex_pkg_has_path(const struct pkgdir *pkgdir,
                                 const struct pkg *pkg, const char *path);

/* directories required by package */
EXPORT tn_array *pkgdir_dirindex_get_required(const struct pkgdir *pkgdir,
                                       const struct pkg *pkg);

EXPORT tn_array *pkgdir_dirindex_get_provided(const struct pkgdir *pkgdir,
                                       const struct pkg *pkg);

#endif  /* SWIG */

#endif /* POLDEK_PKGDIR_H*/
