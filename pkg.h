/* $Id$ */
#ifndef  POLDEK_PKG_H
#define  POLDEK_PKG_H

#include <stdint.h>
#include <string.h>
#include <trurl/narray.h>
#include <trurl/nmalloc.h>
#include <trurl/ntuple.h>

#include "pkgcmp.h"             /* compares functions */

struct capreq;                  /* defined in capreq.h */
struct pkguinf;                 /* defined in pkgu.h   */
struct pkgdir;                  /* defined in pkgdir/pkgdir.h */


#define PKG_HAS_PKGUINF     (1 << 5)
#define PKG_HAS_SELFCAP     (1 << 6)

#define PKG_HELD            (1 << 12) /* non upgradable */
#define PKG_IGNORED         (1 << 13) /* invisible      */
#define PKG_IGNORED_UNIQ    (1 << 14) /* uniqued        */

#define PKG_ORDER_PREREQ    (1 << 15) /* see pkgset-order.c */
#define PKG_DBPKG           (1 << 16) /* loaded from database, i.e. installed */

/* DAG node colours */
#define PKG_COLOR_WHITE    (1 << 20)
#define PKG_COLOR_GRAY     (1 << 21)
#define PKG_COLOR_BLACK    (1 << 22)
#define PKG_ALL_COLORS     PKG_COLOR_WHITE | PKG_COLOR_GRAY | PKG_COLOR_BLACK

/* colours */
#define pkg_set_color(pkg, color) \
   ((pkg)->flags &= ~(PKG_ALL_COLORS), (pkg)->flags |= (color))

#define pkg_is_color(pkg, color) \
   ((pkg)->flags & color)

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
    
    
    char         *name;
    int32_t      epoch;
    char         *ver;
    char         *rel;

    char         *fn;         /* package filename */
    uint32_t     fmtime;      /* package file mtime */
    char         *_nvr;        /* NAME-VERSION-RELEASE */

    uint16_t      _arch;
    uint16_t      _os;
    
    tn_array     *caps;       /* capabilities     */
    tn_array     *reqs;       /* requirements     */
    tn_array     *cnfls;      /* conflicts (with obsoletes)  */
    
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
    int32_t      recno;        /* db's ID of the header */
    int32_t      itime;        /* date of installation  */

    /* private, don't touch */
    int16_t      _refcnt;
    tn_alloc     *na;
    int16_t      _buf_size;
    char         _buf[0];  /* private, store all string members */
};


struct pkg *pkg_new_ext(tn_alloc *na,
                        const char *name, int32_t epoch,
                        const char *version, const char *release,
                        const char *arch, const char *os,
                        const char *fn,
                        uint32_t size, uint32_t fsize,
                        uint32_t btime);

#define pkg_new(n, e, v, r, a, o) \
    pkg_new_ext(NULL, n, e, v, r, a, o, NULL, 0, 0, 0)


#define PKG_LDNEVR       0
#define PKG_LDCAPS       (1 << 0)
#define PKG_LDREQS       (1 << 1)
#define PKG_LDCNFLS      (1 << 2)
#define PKG_LDFL_DEPDIRS (1 << 3)
#define PKG_LDFL_WHOLE   (1 << 4)

#define PKG_LDCAPREQS PKG_LDCAPS | PKG_LDREQS | PKG_LDCNFLS
#define PKG_LDWHOLE   PKG_LDCAPREQS | PKG_LDFL_WHOLE
#define PKG_LDWHOLE_FLDEPDIRS PKG_LDCAPREQS | PKG_LDFL_DEPDIRS

void pkg_free(struct pkg *pkg);

#ifdef SWIG
# define extern__inline
#else
# define extern__inline extern inline
#endif

extern__inline struct pkg *pkg_link(struct pkg *pkg);

int pkg_set_arch(struct pkg *pkg, const char *arch);
const char *pkg_arch(const struct pkg *pkg);
int pkg_arch_score(const struct pkg *pkg);

const char *pkg_os(const struct pkg *pkg);
int pkg_set_os(struct pkg *pkg, const char *os);

#define POLDEK_MA_PROMOTE_VERSION    (1 << 0) /* old strict = 0 */
#define POLDEK_MA_PROMOTE_REQEPOCH   (1 << 2)
#define POLDEK_MA_PROMOTE_CAPEPOCH   (1 << 3)
#define POLDEK_MA_PROMOTE_EPOCH      ((1 << 2) | (1 << 3))

/* look up into package caps only */
int pkg_caps_match_req(const struct pkg *pkg, const struct capreq *req,
                       unsigned flags);

int pkg_evr_match_req(const struct pkg *pkg, const struct capreq *req,
                      unsigned flags);


int cap_xmatch_req(const struct capreq *cap, const struct capreq *req,
                   unsigned ma_flags);

/* obsoleted */
int cap_match_req(const struct capreq *cap, const struct capreq *req,
                  int strict);


/* CAUTION: looks into NEVR and caps only */
int pkg_xmatch_req(const struct pkg *pkg, const struct capreq *req,
                   unsigned flags);

/* obsoleted */
int pkg_match_req(const struct pkg *pkg, const struct capreq *req, int strict);

int pkg_has_path(const struct pkg *pkg,
                 const char *dirname, const char *basename);

/* match with caps && files */
int pkg_satisfies_req(const struct pkg *pkg, const struct capreq *req,
                       int strict);

int pkg_obsoletes_pkg(const struct pkg *pkg, const struct pkg *opkg);
int pkg_caps_obsoletes_pkg_caps(const struct pkg *pkg, const struct pkg *opkg);

int pkg_add_pkgcnfl(struct pkg *pkg, struct pkg *cpkg, int isbastard);
int pkg_has_pkgcnfl(struct pkg *pkg, struct pkg *cpkg);


/* RET %path/%name-%version-%release.%arch.rpm  */
char *pkg_filename(const struct pkg *pkg, char *buf, size_t size);
char *pkg_filename_s(const struct pkg *pkg);

char *pkg_path(const struct pkg *pkg, char *buf, size_t size);
char *pkg_path_s(const struct pkg *pkg);

char *pkg_localpath(const struct pkg *pkg, char *path, size_t size,
                    const char *cachedir);
const char *pkg_pkgdirpath(const struct pkg *pkg);
unsigned pkg_file_url_type(const struct pkg *pkg);


extern__inline const char *pkg_id(const struct pkg *p);
int pkg_id_snprintf(char *str, size_t size, const struct pkg *pkg);
int pkg_idevr_snprintf(char *str, size_t size, const struct pkg *pkg);


int pkg_printf(const struct pkg *pkg, const char *str);
int pkg_snprintf(char *str, size_t size, const struct pkg *pkg);
char *pkg_snprintf_s(const struct pkg *pkg);
char *pkg_snprintf_s0(const struct pkg *pkg);
char *pkg_snprintf_s1(const struct pkg *pkg);
int pkg_evr_snprintf(char *str, size_t size, const struct pkg *pkg);
char *pkg_evr_snprintf_s(const struct pkg *pkg);


/* must be free()d by pkguinf_free(); see pkgu.h */
struct pkguinf *pkg_info(const struct pkg *pkg);
struct pkguinf *pkg_info_ex(const struct pkg *pkg, tn_array *langs);

struct pkgflist {
    tn_tuple *fl;
    tn_alloc *_na;
};

/* load and returns not loaded file list (l: tag in package index) */
struct pkgflist *pkg_info_get_nodep_flist(const struct pkg *pkg);
struct pkgflist *pkg_info_get_flist(const struct pkg *pkg);
void pkg_info_free_flist(struct pkgflist *flist);


const char *pkg_group(const struct pkg *pkg);

tn_array *pkgs_array_new(int size);
tn_array *pkgs_array_new_ex(int size,
                            int (*cmpfn)(const struct pkg *p1,
                                         const struct pkg *p2));

char *pkg_strsize(char *buf, int size, const struct pkg *pkg);
char *pkg_strbtime(char *buf, int size, const struct pkg *pkg);
char *pkg_stritime(char *buf, int size, const struct pkg *pkg);
/* add self name-evr to caps */
int pkg_add_selfcap(struct pkg *pkg);

#endif /* POLDEK_PKG_H */
