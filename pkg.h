/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef  POLDEK_PKG_H
#define  POLDEK_PKG_H

#include <stdint.h>
#include <string.h>
#include <trurl/narray.h>
#include <trurl/nmalloc.h>
#include <trurl/ntuple.h>
#include <trurl/nbuf.h>

#include "pkgcmp.h"             /* compares functions */

#ifndef EXPORT
# define EXPORT extern
#endif

struct capreq;                  /* defined in capreq.h */
struct pkguinf;                 /* defined in pkgu.h   */
struct pkgdir;                  /* defined in pkgdir/pkgdir.h */

#define PKG_HAS_SRCFN       (1 << 4) /* set source package filename? */
#define PKG_HAS_PKGUINF     (1 << 5) /* user-level info (pkgu.c) */
#define PKG_HAS_SELFCAP     (1 << 6) /* name = e:v-r cap */

#define PKG_HELD            (1 << 12) /* non upgradable */
#define PKG_IGNORED         (1 << 13) /* invisible      */
#define PKG_IGNORED_UNIQ    (1 << 14) /* uniqued        */

#define PKG_ORDER_PREREQ    (1 << 15) /* see pkgset-order.c */

#define PKG_DBPKG           (1 << 16) /* loaded from database, i.e. installed */
#define PKG_INCLUDED_DIRREQS (1 << 17) /* auto-dir-reqs added directly to reqs */

#ifdef POLDEK_PKG_DAG_COLOURS
/* DAG node colours (pkgset-order.c, split.c) */
# define PKG_COLOR_WHITE    (1 << 20)
# define PKG_COLOR_GRAY     (1 << 21)
# define PKG_COLOR_BLACK    (1 << 22)
# define PKG_ALL_COLORS     PKG_COLOR_WHITE | PKG_COLOR_GRAY | PKG_COLOR_BLACK

/* colours */
# define pkg_set_color(pkg, color) \
   ((pkg)->flags &= ~(PKG_ALL_COLORS), (pkg)->flags |= (color))

# define pkg_is_color(pkg, color) \
   ((pkg)->flags & color)
#endif  /* POLDEK_PKG_DAG_COLOURS */

#define pkg_is_noarch(pkg)  (0 == strcmp(pkg_arch((pkg)), "noarch"))

#define pkg_set_prereqed(pkg) ((pkg)->flags |= PKG_ORDER_PREREQ)
#define pkg_clr_prereqed(pkg)  ((pkg)->flags &= ~PKG_ORDER_PREREQ) 
#define pkg_is_prereqed(pkg)  ((pkg)->flags & PKG_ORDER_PREREQ)

#define pkg_score(pkg, v) ((pkg)->flags |= v)
#define pkg_is_scored(pkg, v) ((pkg)->flags & v)
#define pkg_clr_score(pkg, v) ((pkg)->flags &= ~(v))

#define pkg_has_ldpkguinf(pkg) ((pkg)->flags & PKG_HAS_PKGUINF)
#define pkg_set_ldpkguinf(pkg) ((pkg)->flags |= PKG_HAS_PKGUINF)
#define pkg_clr_ldpkguinf(pkg) ((pkg)->flags &= (~PKG_HAS_PKGUINF))

struct pkg {
    uint32_t     flags;
    uint32_t     size;        /* install size      */
    uint32_t     fsize;       /* package file size */
    uint32_t     btime;       /* build time        */
    uint32_t     color;       /* rpm's pkg color   */
    
    char         *name;
    int32_t      epoch;
    char         *ver;
    char         *rel;

    char         *fn;         /* package filename */
    char         *srcfn;      /* package filename */
    
    uint32_t     fmtime;      /* package file mtime */
    char         *_nvr;       /* NAME-VERSION-RELEASE */

    uint16_t      _arch;
    uint16_t      _os;
    
    tn_array     *caps;       /* capabilities     */
    tn_array     *reqs;       /* requirements     */
    tn_array     *cnfls;      /* conflicts (with obsoletes)  */
    tn_array     *sugs;       /* suggests */
    
    tn_tuple     *fl;         /* file list, see pkgfl.h  */
    
    tn_array     *reqpkgs;    /* required packages  */
    tn_array     *revreqpkgs; /* packages which requires me */
    tn_array     *cnflpkgs;   /* conflicted packages */

    struct pkgdir    *pkgdir;    /* reference to its own pkgdir */
    void             *pkgdir_data;
    void             (*pkgdir_data_free)(tn_alloc *na, void*);
    
    struct pkguinf   *(*load_pkguinf)(tn_alloc *na, const struct pkg *pkg,
                                      void *pkgdir_data, tn_array *langs);
    tn_tuple         *(*load_nodep_fl)(tn_alloc *na, const struct pkg *pkg,
                                       void *pkgdir_data, tn_array*);

    struct pkguinf *pkg_pkguinf; 

    int pri;                  /* used for split */
    int groupid;              /* package group id (see pkgroups.c) */

    /* for installed packages */
    uint32_t      recno;        /* db's ID of the header */
    int32_t      itime;        /* date of installation  */

    /* private, don't touch */
    uint16_t      _refcnt;
    tn_alloc     *na;
    int16_t      _buf_size;
    char         _buf[0];  /* private, store all string members */
};


EXPORT struct pkg *pkg_new_ext(tn_alloc *na,
                        const char *name, int32_t epoch,
                        const char *version, const char *release,
                        const char *arch, const char *os,
                        const char *fn, const char *srcfn,
                        uint32_t size, uint32_t fsize,
                        uint32_t btime);

#define pkg_new(n, e, v, r, a, o) \
    pkg_new_ext(NULL, n, e, v, r, a, o, NULL, NULL, 0, 0, 0)


#define PKG_LDNEVR       0
#define PKG_LDCAPS       (1 << 0)
#define PKG_LDREQS       (1 << 1)
#define PKG_LDCNFLS      (1 << 2)
#define PKG_LDFL_DEPDIRS (1 << 3)
#define PKG_LDFL_WHOLE   (1 << 4)

#define PKG_LDCAPREQS PKG_LDCAPS | PKG_LDREQS | PKG_LDCNFLS
#define PKG_LDWHOLE   PKG_LDCAPREQS | PKG_LDFL_WHOLE
#define PKG_LDWHOLE_FLDEPDIRS PKG_LDCAPREQS | PKG_LDFL_DEPDIRS

EXPORT void pkg_free(struct pkg *pkg);

#ifdef SWIG
# define extern__inline
#else
# define extern__inline inline
#endif

EXPORT struct pkg *pkg_link(struct pkg *pkg);

EXPORT int pkg_set_arch(struct pkg *pkg, const char *arch);
EXPORT const char *pkg_arch(const struct pkg *pkg);
EXPORT int pkg_arch_score(const struct pkg *pkg);

EXPORT const char *pkg_os(const struct pkg *pkg);
EXPORT int pkg_set_os(struct pkg *pkg, const char *os);

#define POLDEK_MA_PROMOTE_VERSION    (1 << 0) /* old strict = 0 */
#define POLDEK_MA_PROMOTE_REQEPOCH   (1 << 2)
#define POLDEK_MA_PROMOTE_CAPEPOCH   (1 << 3)
#define POLDEK_MA_PROMOTE_EPOCH      ((1 << 2) | (1 << 3))

/* look up into package caps only */
EXPORT int pkg_caps_match_req(const struct pkg *pkg, const struct capreq *req,
                       unsigned flags);

EXPORT int pkg_evr_match_req(const struct pkg *pkg, const struct capreq *req,
                      unsigned flags);


EXPORT int cap_xmatch_req(const struct capreq *cap, const struct capreq *req,
                   unsigned ma_flags);

/* obsoleted */
EXPORT int cap_match_req(const struct capreq *cap, const struct capreq *req,
                  int strict);


/* CAUTION: looks into NEVR and caps only */
EXPORT int pkg_xmatch_req(const struct pkg *pkg, const struct capreq *req,
                   unsigned flags);

/* obsoleted */
EXPORT int pkg_match_req(const struct pkg *pkg, const struct capreq *req, int strict);

EXPORT int pkg_has_path(const struct pkg *pkg,
                 const char *dirname, const char *basename);

/* match with caps && files */
EXPORT int pkg_satisfies_req(const struct pkg *pkg, const struct capreq *req,
                       int strict);

EXPORT int pkg_obsoletes_pkg(const struct pkg *pkg, const struct pkg *opkg);
EXPORT int pkg_caps_obsoletes_pkg_caps(const struct pkg *pkg, const struct pkg *opkg);

EXPORT const struct capreq *pkg_requires_cap(const struct pkg *pkg,
                                      const struct capreq *cap);

EXPORT int pkg_add_pkgcnfl(struct pkg *pkg, struct pkg *cpkg, int isbastard);
EXPORT int pkg_has_pkgcnfl(struct pkg *pkg, struct pkg *cpkg);

/* src.rpm */
EXPORT char *pkg_srcfilename(const struct pkg *pkg, char *buf, size_t size);
EXPORT char *pkg_srcfilename_s(const struct pkg *pkg);

/* RET %path/%name-%version-%release.%arch.rpm  */
EXPORT char *pkg_filename(const struct pkg *pkg, char *buf, size_t size);
EXPORT char *pkg_filename_s(const struct pkg *pkg);

EXPORT char *pkg_path(const struct pkg *pkg, char *buf, size_t size);
EXPORT char *pkg_path_s(const struct pkg *pkg);

EXPORT char *pkg_localpath(const struct pkg *pkg, char *path, size_t size,
                    const char *cachedir);
EXPORT const char *pkg_pkgdirpath(const struct pkg *pkg);
EXPORT unsigned pkg_file_url_type(const struct pkg *pkg);


EXPORT extern__inline const char *pkg_id(const struct pkg *p);
EXPORT int pkg_id_snprintf(char *str, size_t size, const struct pkg *pkg);
EXPORT int pkg_idevr_snprintf(char *str, size_t size, const struct pkg *pkg);


EXPORT int pkg_printf(const struct pkg *pkg, const char *str);
EXPORT int pkg_snprintf(char *str, size_t size, const struct pkg *pkg);
EXPORT char *pkg_snprintf_s(const struct pkg *pkg);
EXPORT char *pkg_snprintf_s0(const struct pkg *pkg);
EXPORT char *pkg_snprintf_s1(const struct pkg *pkg);
EXPORT int pkg_evr_snprintf(char *str, size_t size, const struct pkg *pkg);
EXPORT char *pkg_evr_snprintf_s(const struct pkg *pkg);


/* must be free()d by pkguinf_free(); see pkgu.h */
EXPORT struct pkguinf *pkg_uinf(const struct pkg *pkg);
EXPORT struct pkguinf *pkg_xuinf(const struct pkg *pkg, tn_array *langs);

/* directories required by package */
EXPORT tn_array *pkg_required_dirs(const struct pkg *pkg);

struct pkgflist {
    tn_tuple *fl;
    tn_alloc *_na;
};

/* load and returns not loaded file list (l: tag in package index) */
EXPORT struct pkgflist *pkg_get_nodep_flist(const struct pkg *pkg);

/* returns whole file list, (L: + l:) */
EXPORT struct pkgflist *pkg_get_flist(const struct pkg *pkg);

EXPORT void pkgflist_free(struct pkgflist *flist);


/* whole file list as iterator */
struct pkgflist_it;
EXPORT struct pkgflist_it *pkg_get_flist_it(const struct pkg *pkg);

struct flfile;
EXPORT const char *pkgflist_it_get(struct pkgflist_it *it, struct flfile **flfile);
  /* to simplify python wrapper */
EXPORT const char *pkgflist_it_get_rawargs(struct pkgflist_it *it, uint32_t *size,
                                    uint16_t *mode, const char **basename);
EXPORT void pkgflist_it_free(struct pkgflist_it *it);


EXPORT const char *pkg_group(const struct pkg *pkg);


EXPORT char *pkg_strsize(char *buf, int size, const struct pkg *pkg);
EXPORT char *pkg_strbtime(char *buf, int size, const struct pkg *pkg);
EXPORT char *pkg_stritime(char *buf, int size, const struct pkg *pkg);
/* add self name-evr to caps */
EXPORT int pkg_add_selfcap(struct pkg *pkg);

#ifndef SWIG
EXPORT tn_array *pkgs_array_new(int size);
EXPORT tn_array *pkgs_array_new_ex(int size,
                            int (*cmpfn)(const struct pkg *p1,
                                         const struct pkg *p2));
EXPORT void pkgs_array_dump(tn_array *pkgs, const char *prefix); /* for debugging */

EXPORT tn_buf *pkgs_array_join(tn_array *pkgs, tn_buf *nbuf, const char *sep);

/* caps & reqs iterators */

EXPORT struct pkg_cap_iter *pkg_cap_iter_new(struct pkg *pkg);
EXPORT void pkg_cap_iter_free(struct pkg_cap_iter *it);
EXPORT const struct capreq *pkg_cap_iter_get(struct pkg_cap_iter *it);


#define PKG_ITER_REQIN  (1 << 0) /* Requires + Requires(pre) */
#define PKG_ITER_REQUN  (1 << 1) /* Requires(un) */
#define PKG_ITER_REQDIR (1 << 2) /* Requires(dir) */
#define PKG_ITER_REQSUG (1 << 3) /* Suggests  */

EXPORT struct pkg_req_iter *pkg_req_iter_new(const struct pkg *pkg, unsigned flags);
EXPORT void pkg_req_iter_free(struct pkg_req_iter *it);

/* get next requirement */
EXPORT const struct capreq *pkg_req_iter_get(struct pkg_req_iter *it);

/* and its type PKG_ITER_* */
EXPORT unsigned pkg_req_iter_current_req_type(const struct pkg_req_iter *it);


#endif

#endif /* POLDEK_PKG_H */
