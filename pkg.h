/* $Id$ */
#ifndef  POLDEK_PKG_H
#define  POLDEK_PKG_H

#include <stdint.h>

#include <rpm/rpmlib.h>
#include <trurl/narray.h>

#include "capreq.h"
#include "pkgfl.h"
#include "pkgu.h"
#include "pkgdir.h"

#define PKG_HAS_CAPS        (1 << 1)
#define PKG_HAS_REQS        (1 << 2)
#define PKG_HAS_CNFLS       (1 << 3)
#define PKG_HAS_FL          (1 << 4)
#define PKG_HAS_PKGUINF     (1 << 5)

#define PKG_HAS_SELFCAP     (1 << 6)

#define PKG_DIRMARK         (1 << 8) /* marked directly, i.e. by the user*/
#define PKG_INDIRMARK       (1 << 9) /* marked by poldek */

#define PKG_BADREQS         (1 << 10) /* has unsatisfied dependencies? */

#define PKG_OBSOLETED       (1 << 11)

#define PKG_HOLD            (1 << 12) /* non upgradable */

#define PKG_ORDER_PREREQ    (1 << 13) /* see pkgset-order.c */


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

#define pkg_mark_obsoleted(pkg) ((pkg)->flags |= PKG_OBSOLETED)
#define pkg_is_obsoleted(pkg) ((pkg)->flags & PKG_OBSOLETED)

#define pkg_mark_hold(pkg) ((pkg)->flags |= PKG_HOLD)
#define pkg_is_hold(pkg) ((pkg)->flags & PKG_HOLD)

#define pkg_has_ldpkguinf(pkg) ((pkg)->flags & PKG_HAS_PKGUINF)
#define pkg_set_ldpkguinf(pkg) ((pkg)->flags |= PKG_HAS_PKGUINF)
#define pkg_clr_ldpkguinf(pkg) ((pkg)->flags &= (~PKG_HAS_PKGUINF))


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
    off_t        other_files_offs;  /* no dep files offset in Packges */
    
    tn_array     *reqpkgs;    /* require packages  */
    tn_array     *revreqpkgs; /* packages which requires me */
    tn_array     *cnflpkgs;   /* conflict packages */

    struct pkgdir *pkgdir;    /* refrence to its own pkgdir */
    
    union {
        off_t           pkg_pkguinf_offs;
        struct pkguinf *pkg_pkguinf; 
    } package_uinf;


    int pri;                    /* used for split */
    int groupid;                /* package group id (see pkgroups.c) */
    /* private, don't touch */
    uint16_t     _refcnt;
    void         (*free)(void*); /* self free()  */

    int32_t      _buf_size;
    char         _buf[0];     /* private, store all string members */
};

#define	pkg_pkguinf_offs  package_uinf.pkg_pkguinf_offs
#define	pkg_pkguinf       package_uinf.pkg_pkguinf

struct pkg *pkg_new(const char *name, int32_t epoch,
                    const char *version, const char *release,
                    const char *arch, const char *os,
                    uint32_t size, uint32_t fsize,
                    uint32_t btime);

#define PKG_LDNEVR    0
#define PKG_LDCAPS    (1 << 0)
#define PKG_LDREQS    (1 << 1)
#define PKG_LDCNFLS   (1 << 2)
#define PKG_LDFL      (1 << 3)

#define PKG_LDCAPREQS PKG_LDCAPS | PKG_LDREQS | PKG_LDCNFLS
#define PKG_LDWHOLE   PKG_LDCAPREQS | PKG_LDFL

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
int pkg_cmp_pri(struct pkg *p1, struct pkg *p2);

int pkg_cmp_btime(struct pkg *p1, struct pkg *p2);
int pkg_cmp_btime_rev(struct pkg *p1, struct pkg *p2);


/* with warn message */
int pkg_cmp_uniq(const struct pkg *p1, const struct pkg *p2);

int pkg_deepcmp_name_evr_rev(const struct pkg *p1, const struct pkg *p2);
int pkg_deepcmp_name_evr_rev_verify(const struct pkg *p1, const struct pkg *p2);


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

/* load and returns not loaded file list (l: tag in package index) */
tn_array *pkg_other_fl(const struct pkg *pkg);

struct pkguinf *pkg_info(const struct pkg *pkg);
tn_array *pkg_info_files(const struct pkg *pkg);
const char *pkg_group(const struct pkg *pkg);

void set_pkg_allocfn(void *(*pkg_allocfn)(size_t), void (*pkg_freefn)(void*));

tn_array *pkgs_array_new(int size);

#endif /* POLDEK_PKG_H */
