/* $Id$ */
#ifndef  POLDEK_PKG_H
#define  POLDEK_PKG_H

#include <stdint.h>
#include <string.h>
#include <trurl/narray.h>
#include <trurl/nmalloc.h>
#include <trurl/ntuple.h>

struct capreq;                  /* defined in capreq.h */
struct pkguinf;                 /* defined in pkgu.h   */
struct pkgdir;                  /* defined in pkgdir/pkgdir.h */


#define PKG_HAS_PKGUINF     (1 << 5)
#define PKG_HAS_SELFCAP     (1 << 6)

#if 0
#define PKG_DIRMARK         (1 << 8)  /* marked directly, i.e. by the user*/
#define PKG_INDIRMARK       (1 << 9)  /* marked by deps */
#endif

#define PKG_BADREQS         (1 << 10) /* has unsatisfied dependencies? */

#if 0
#define PKG_RM_MARK         (1 << 11) /* marked for removal */
#endif

#define PKG_HELD            (1 << 12) /* non upgradable */
#define PKG_IGNORED         (1 << 13) /* invisible      */

#define PKG_ORDER_PREREQ    (1 << 14) /* see pkgset-order.c */

#define PKG_DBPKG           (1 << 15) /* loaded from database, i.e. installed */

/* DAG node colours */
#define PKG_COLOR_WHITE    (1 << 20)
#define PKG_COLOR_GRAY     (1 << 21)
#define PKG_COLOR_BLACK    (1 << 22)
#define PKG_ALL_COLORS     PKG_COLOR_WHITE | PKG_COLOR_GRAY | PKG_COLOR_BLACK

#define PKG_INTERNALMARK    (1 << 23)



/* colours */
#define pkg_set_color(pkg, color) \
   ((pkg)->flags &= ~(PKG_ALL_COLORS), (pkg)->flags |= (color))

#define pkg_is_color(pkg, color) \
   ((pkg)->flags & color)


#define pkg_set_prereqed(pkg) ((pkg)->flags |= PKG_ORDER_PREREQ)
#define pkg_clr_prereqed(pkg)  ((pkg)->flags &= ~PKG_ORDER_PREREQ) 
#define pkg_is_prereqed(pkg)  ((pkg)->flags & PKG_ORDER_PREREQ)

#if 0
#define pkg_hand_mark(pkg)  ((pkg)->flags |= PKG_DIRMARK)
#define pkg_dep_mark(pkg)   ((pkg)->flags |= PKG_INDIRMARK)
#define pkg_unmark(pkg)     ((pkg)->flags &= ~(PKG_DIRMARK | PKG_INDIRMARK))

#define pkg_is_dep_marked(pkg)  ((pkg)->flags & PKG_INDIRMARK)
#define pkg_is_hand_marked(pkg) ((pkg)->flags & PKG_DIRMARK)
#define pkg_is_marked(pkg)      ((pkg)->flags & (PKG_DIRMARK | PKG_INDIRMARK))
#define pkg_isnot_marked(pkg)   (!pkg_is_marked((pkg)))

#define pkg_mark_i(pkg)       ((pkg)->flags |= PKG_INTERNALMARK)
#define pkg_unmark_i(pkg)     ((pkg)->flags &= ~PKG_INTERNALMARK)
#define pkg_is_marked_i(pkg)  ((pkg)->flags & PKG_INTERNALMARK)

#define pkg_rm_mark(pkg)      ((pkg)->flags |= PKG_RM_MARK)
#define pkg_is_rm_marked(pkg) ((pkg)->flags & PKG_RM_MARK)
#define pkg_rm_unmark(pkg)    ((pkg)->flags &= ~(PKG_RM_MARK))
#endif

#define pkg_has_badreqs(pkg) ((pkg)->flags & PKG_BADREQS)
#define pkg_set_badreqs(pkg) ((pkg)->flags |= PKG_BADREQS)
#define pkg_clr_badreqs(pkg) ((pkg)->flags &= (~PKG_BADREQS))
                             
//#define pkg_upgrade(pkg)      ((pkg)->flags & PKG_UPGRADE)
//#define pkg_mark_upgrade(pkg) ((pkg)->flags |= PKG_UPGRADE)


//#define pkg_mark_hold(pkg) ((pkg)->flags |= PKG_HELD)
//#define pkg_is_hold(pkg) ((pkg)->flags & PKG_HELD)

#define pkg_score(pkg, v) ((pkg)->flags |= v)
#define pkg_is_scored(pkg, v) ((pkg)->flags & v)
#define pkg_clr_score(pkg, v) ((pkg)->flags &= ~(v))

#define pkg_has_ldpkguinf(pkg) ((pkg)->flags & PKG_HAS_PKGUINF)
#define pkg_set_ldpkguinf(pkg) ((pkg)->flags |= PKG_HAS_PKGUINF)
#define pkg_clr_ldpkguinf(pkg) ((pkg)->flags &= (~PKG_HAS_PKGUINF))

#define pkg_is_installed(pkg)  ((pkg)->flags & PKG_DBPKG)

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
    char         *nvr;        /* NAME-VERSION-RELEASE */

    const char   *arch;
    const char   *os;
    
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
    struct pkguinf   *(*load_pkguinf)(tn_alloc *na, const struct pkg *pkg, void*);
    tn_tuple         *(*load_nodep_fl)(tn_alloc *na, const struct pkg *pkg,
                                       void*, tn_array*);

    struct pkguinf *pkg_pkguinf; 

    int pri;                  /* used for split */
    int groupid;              /* package group id (see pkgroups.c) */

    /* for installed packages */
    int32_t      recno;        /* db's ID of the header */
    int32_t      itime;        /* date of installation  */

    int32_t      _ldflags;
    int32_t      _rt_dbflags;

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
struct pkg *pkg_link(struct pkg *pkg);
#else 
static inline struct pkg *pkg_link(struct pkg *pkg)
{
    pkg->_refcnt++;
    //n_assert(pkg->_refcnt < 3);
    return pkg;
}
#endif

/* add self name-evr to caps */
int pkg_add_selfcap(struct pkg *pkg);

#ifdef SWIG
int pkg_cmp_name(const struct pkg *p1, const struct pkg *p2);
#else
static inline int pkg_cmp_name(const struct pkg *p1, const struct pkg *p2) 
{
    return strcmp(p1->name, p2->name);
}
#endif

static inline const char *pkg_id(const struct pkg *p) 
{
    return p->nvr;
}


int pkg_cmp_ver(const struct pkg *p1, const struct pkg *p2);
int pkg_cmp_evr(const struct pkg *p1, const struct pkg *p2);
int pkg_cmp_name_evr(const struct pkg *p1, const struct pkg *p2);
int pkg_cmp_name_ver(const struct pkg *p1, const struct pkg *p2);
int pkg_cmp_name_evr_rev(const struct pkg *p1, const struct pkg *p2);

int pkg_cmp_name_srcpri(const struct pkg *p1, const struct pkg *p2);
int pkg_cmp_name_evr_rev_srcpri(const struct pkg *p1, const struct pkg *p2);

int pkg_cmp_pri(struct pkg *p1, struct pkg *p2);

int pkg_cmp_btime(struct pkg *p1, struct pkg *p2);
int pkg_cmp_btime_rev(struct pkg *p1, struct pkg *p2);

int pkg_cmp_recno(const struct pkg *p1, const struct pkg *p2);

int pkg_deepstrcmp_name_evr(const struct pkg *p1, const struct pkg *p2);

/* strncmp(p1->name, p2->name, strlen(p2->name))*/
int pkg_strncmp_name(const struct pkg *p1, const struct pkg *p2);

int pkg_strcmp_name_evr(const struct pkg *p1, const struct pkg *p2);

/* with warn message */
int pkg_cmp_uniq(const struct pkg *p1, const struct pkg *p2);
int pkg_cmp_name_uniq(const struct pkg *p1, const struct pkg *p2);

int pkg_deepcmp_name_evr_rev(const struct pkg *p1, const struct pkg *p2);
int pkg_deepcmp_name_evr_rev_verify(const struct pkg *p1, const struct pkg *p2);

int pkg_eq_name_prefix(const struct pkg *pkg1, const struct pkg *pkg2);

int pkg_eq_capreq(const struct pkg *pkg, const struct capreq *cr);

/* look up into package caps only */
int pkg_caps_match_req(const struct pkg *pkg, const struct capreq *req,
                       int strict);

int pkg_evr_match_req(const struct pkg *pkg, const struct capreq *req,
                      int strict);

int cap_match_req(const struct capreq *cap, const struct capreq *req,
                  int strict);

/* CAUTION: looks into NEVR and caps only */
int pkg_match_req(const struct pkg *pkg, const struct capreq *req,
                  int strict);

int pkg_has_path(const struct pkg *pkg,
                 const char *dirname, const char *basename);

/* match with caps && files */
int pkg_statisfies_req(const struct pkg *pkg, const struct capreq *req,
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


int pkg_printf(const struct pkg *pkg, const char *str);
int pkg_snprintf(char *str, size_t size, const struct pkg *pkg);
char *pkg_snprintf_s(const struct pkg *pkg);
char *pkg_snprintf_s0(const struct pkg *pkg);
char *pkg_snprintf_s1(const struct pkg *pkg);

int pkg_evr_snprintf(char *str, size_t size, const struct pkg *pkg);
char *pkg_evr_snprintf_s(const struct pkg *pkg);
char *pkg_snprintf_epoch_s(const struct pkg *pkg);


struct pkguinf *pkg_info(const struct pkg *pkg);

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



int pkg_nvr_strcmp(struct pkg *p1, struct pkg *p2);
int pkg_nvr_strcmp_rev(struct pkg *p1, struct pkg *p2);
int pkg_nvr_strncmp(struct pkg *pkg, const char *name);
int pkg_nvr_strcmp_btime(struct pkg *p1, struct pkg *p2);
int pkg_nvr_strcmp_btime_rev(struct pkg *p1, struct pkg *p2);
int pkg_nvr_strcmp_bday(struct pkg *p1, struct pkg *p2);
int pkg_nvr_strcmp_bday_rev(struct pkg *p1, struct pkg *p2);

char *pkg_strsize(char *buf, int size, const struct pkg *pkg);
char *pkg_strbtime(char *buf, int size, const struct pkg *pkg);


#endif /* POLDEK_PKG_H */
