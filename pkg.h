/* $Id$ */
#ifndef  POLDEK_PKG_H
#define  POLDEK_PKG_H

#include <stdint.h>

#include <rpm/rpmlib.h>
#include <trurl/narray.h>

#include "capreq.h"
#include "pkgfl.h"
#include "pkgu.h"


//#define PKG_HAS_CAPS        (1 << 1)
//#define PKG_HAS_REQS        (1 << 2)
#define PKG_HAS_CNFLS       (1 << 3)
#define PKG_HAS_FL          (1 << 4)
#define PKG_HAS_PKGUINF     (1 << 5)

#define PKG_HAS_SELFCAP     (1 << 6)

#define PKG_DIRMARK         (1 << 8)  /* marked directly, i.e. by the user*/
#define PKG_INDIRMARK       (1 << 9)  /* marked by deps */

#define PKG_BADREQS         (1 << 10) /* has unsatisfied dependencies? */

#define PKG_RM_MARK         (1 << 11) /* marked for removal */

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

#define pkg_has_badreqs(pkg) ((pkg)->flags & PKG_BADREQS)
#define pkg_set_badreqs(pkg) ((pkg)->flags |= PKG_BADREQS)
#define pkg_clr_badreqs(pkg) ((pkg)->flags &= (~PKG_BADREQS))
                             
#define pkg_upgrade(pkg)      ((pkg)->flags & PKG_UPGRADE)
#define pkg_mark_upgrade(pkg) ((pkg)->flags |= PKG_UPGRADE)

#define pkg_rm_mark(pkg)      ((pkg)->flags |= PKG_RM_MARK)
#define pkg_is_rm_marked(pkg) ((pkg)->flags & PKG_RM_MARK)
#define pkg_rm_unmark(pkg)    ((pkg)->flags &= ~(PKG_RM_MARK))

#define pkg_mark_hold(pkg) ((pkg)->flags |= PKG_HELD)
#define pkg_is_hold(pkg) ((pkg)->flags & PKG_HELD)

#define pkg_score(pkg, v) ((pkg)->flags |= v)
#define pkg_is_scored(pkg, v) ((pkg)->flags & v)

#define pkg_has_ldpkguinf(pkg) ((pkg)->flags & PKG_HAS_PKGUINF)
#define pkg_set_ldpkguinf(pkg) ((pkg)->flags |= PKG_HAS_PKGUINF)
#define pkg_clr_ldpkguinf(pkg) ((pkg)->flags &= (~PKG_HAS_PKGUINF))

#define pkg_is_installed(pkg)  ((pkg)->flags & PKG_DBPKG)

struct pkgdir;

struct pkg {
    uint32_t     flags;
    uint32_t     size;        /* installed size    */
    uint32_t     fsize;       /* package file size */
    uint32_t     btime;       /* build time        */
    int32_t      epoch;
    char         *name;
    char         *ver;
    char         *rel;
    char         *arch;
    char         *os;
    
    tn_array     *caps;       /* capabilities     */
    tn_array     *reqs;       /* requirements     */
    tn_array     *cnfls;      /* conflicts (with obsoletes)  */
    
    tn_array     *fl;         /* files list, see pkgfl.h  */
    
    
    
    tn_array     *reqpkgs;    /* require packages  */
    tn_array     *revreqpkgs; /* packages which requires me */
    tn_array     *cnflpkgs;   /* conflict packages */

    struct pkgdir    *pkgdir;    /* refrence to its own pkgdir */
    void             *pkgdir_data;
    void             (*pkgdir_data_free)(void*);
    struct pkguinf   *(*load_pkguinf)(const struct pkg *pkg, void*);
    tn_array         *(*load_nodep_fl)(const struct pkg *pkg, void*, tn_array*);

    struct pkguinf *pkg_pkguinf; 

    int pri;                    /* used for split */
    int groupid;                /* package group id (see pkgroups.c) */
    /* private, don't touch */
    int16_t      _refcnt;
    void         (*free)(void*); /* self free()  */

    int32_t      _buf_size;
    char         _buf[0];     /* private, store all string members */
};

struct pkg *pkg_new_ext(const char *name, int32_t epoch,
                        const char *version, const char *release,
                        const char *arch, const char *os,
                        uint32_t size, uint32_t fsize,
                        uint32_t btime);

#define pkg_new(n, e, v, r, a, o) pkg_new_ext(n, e, v, r, a, o, 0, 0, 0)


#define PKG_LDNEVR       0
#define PKG_LDCAPS       (1 << 0)
#define PKG_LDREQS       (1 << 1)
#define PKG_LDCNFLS      (1 << 2)
#define PKG_LDFL_DEPDIRS (1 << 3)
#define PKG_LDFL_WHOLE   (1 << 4)

#define PKG_LDCAPREQS PKG_LDCAPS | PKG_LDREQS | PKG_LDCNFLS
#define PKG_LDWHOLE   PKG_LDCAPREQS | PKG_LDFL_WHOLE
#define PKG_LDWHOLE_FLDEPDIRS PKG_LDCAPREQS | PKG_LDFL_DEPDIRS

struct pkg *pkg_ldhdr(Header h, const char *fname, unsigned fsize,
                      unsigned ldflags);

struct pkg *pkg_ldrpm(const char *path, unsigned ldflags);

void pkg_free(struct pkg *pkg);
struct pkg *pkg_link(struct pkg *pkg);

/* add self name-evr to caps */
int pkg_add_selfcap(struct pkg *pkg);

int pkg_cmp_name(const struct pkg *p1, const struct pkg *p2);
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

int pkg_evr_match_req(const struct pkg *pkg, const struct capreq *req);


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


int pkg_printf(const struct pkg *pkg, const char *str);
int pkg_snprintf(char *str, size_t size, const struct pkg *pkg);
char *pkg_snprintf_s(const struct pkg *pkg);
char *pkg_snprintf_s0(const struct pkg *pkg);
char *pkg_snprintf_s1(const struct pkg *pkg);

int pkg_evr_snprintf(char *str, size_t size, const struct pkg *pkg);
char *pkg_evr_snprintf_s(const struct pkg *pkg);

/* load and returns not loaded file list (l: tag in package index) */
tn_array *pkg_other_fl(const struct pkg *pkg);

struct pkguinf *pkg_info(const struct pkg *pkg);

tn_array *pkg_info_get_fl(const struct pkg *pkg);
void pkg_info_free_fl(const struct pkg *pkg, tn_array *fl);

const char *pkg_group(const struct pkg *pkg);

void set_pkg_allocfn(void *(*pkg_allocfn)(size_t), void (*pkg_freefn)(void*));

tn_array *pkgs_array_new(int size);



#endif /* POLDEK_PKG_H */
