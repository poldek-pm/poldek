/* $Id$ */
#ifndef  POLDEK_PKG_H
#define  POLDEK_PKG_H

#include <stdint.h>

#include <rpm/rpmlib.h>
#include <trurl/narray.h>

#include "capreq.h"
#include "pkgfl.h"
#include "pkgu.h"

#define PKG_HAS_DN          (1 << 0)
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

/* DAG node colours */
#define PKG_COLOR_WHITE    (1 << 13)
#define PKG_COLOR_GRAY     (1 << 14)
#define PKG_COLOR_BLACK    (1 << 15)
#define PKG_ALL_COLORS     PKG_COLOR_WHITE | PKG_COLOR_GRAY | PKG_COLOR_BLACK

/* colours */
#define pkg_set_color(pkg, color) \
   ((pkg)->flags &= ~(PKG_ALL_COLORS), (pkg)->flags |= (color))

#define pkg_is_color(pkg, color) \
   ((pkg)->flags & color)
          

#define pkg_hand_mark(pkg)  ((pkg)->flags |= PKG_DIRMARK)
#define pkg_dep_mark(pkg)   ((pkg)->flags |= PKG_INDIRMARK)
#define pkg_unmark(pkg)     ((pkg)->flags &= ~(PKG_DIRMARK | PKG_INDIRMARK))
#define pkg_is_dep_marked(pkg)  ((pkg)->flags & PKG_INDIRMARK)
#define pkg_is_hand_marked(pkg) ((pkg)->flags & PKG_DIRMARK)
#define pkg_is_marked(pkg)      ((pkg)->flags & (PKG_DIRMARK | PKG_INDIRMARK))
#define pkg_isnot_marked(pkg)   (!pkg_is_marked((pkg)))

#define pkg_has_badreqs(pkg) ((pkg)->flags & PKG_BADREQS)
#define pkg_set_badreqs(pkg) ((pkg)->flags |= PKG_BADREQS)
#define pkg_clr_badreqs(pkg) ((pkg)->flags &= (~PKG_BADREQS))
                             
#define pkg_upgrade(pkg)      ((pkg)->flags & PKG_UPGRADE)
#define pkg_mark_upgrade(pkg) ((pkg)->flags |= PKG_UPGRADE)

#define pkg_mark_obsoleted(pkg) ((pkg)->flags |= PKG_OBSOLETED)
#define pkg_is_obsoleted(pkg) ((pkg)->flags & PKG_OBSOLETED)

#define pkg_has_ldpkguinf(pkg) ((pkg)->flags & PKG_HAS_PKGUINF)
#define pkg_set_ldpkguinf(pkg) ((pkg)->flags |= PKG_HAS_PKGUINF)
#define pkg_clr_ldpkguinf(pkg) ((pkg)->flags &= (~PKG_HAS_PKGUINF))


struct pkg {
    uint32_t     flags;
    uint32_t     size;
    uint32_t     btime;
    int32_t      epoch;
    char         *name;
    char         *ver;
    char         *rel;
    char         *arch;
    char         *dn;         /* dirname */

    tn_array     *caps;       /* capabilities     */
    tn_array     *reqs;       /* requirements     */
    tn_array     *cnfls;      /* conflicts (with obsoletes)  */
    
    tn_array     *fl;         /* files list, see pkgfl.h  */
    
    tn_array     *reqpkgs;    /* require packages  */
    tn_array     *revreqpkgs; /* packages which requires */
    tn_array     *cnflpkgs;   /* conflict packages */

    union {
        off_t           pkg_pkguinf_offs;
        struct pkguinf *pkg_pkguinf;
    } package_uinf;

    void         *udata;      /* for some additional, user level data */
    int32_t      _buf_size;
    char         _buf[0];     /* private, store all string members */
};

#define	pkg_pkguinf_offs  package_uinf.pkg_pkguinf_offs
#define	pkg_pkguinf       package_uinf.pkg_pkguinf

struct pkg *pkg_new_udata(const char *name, int32_t epoch,
                          const char *version, const char *release,
                          const char *arch, uint32_t size, uint32_t btime,
                          const char *fpath,
                          void *udata, size_t adsize);

#define pkg_new(name, epoch, version, release, arch, size, btime, fpath) \
 pkg_new_udata(name, epoch, version, release, arch, size, btime, fpath, NULL,0)

#define PKG_LDNEVR    0
#define PKG_LDCAPS    (1 << 0)
#define PKG_LDREQS    (1 << 1)
#define PKG_LDCNFLS   (1 << 2)
#define PKG_LDFL      (1 << 3)
//#define PKG_LDFL      (1 << 3)
//#define PKG_LDFL      (1 << 3)

#define PKG_LDCAPREQS PKG_LDCAPS | PKG_LDREQS | PKG_LDCNFLS
#define PKG_LDWHOLE   PKG_LDCAPREQS | PKG_LDFL

struct pkg *pkg_ldhdr_udata(Header h, const char *fname, unsigned ldflags, 
                            void *udata, size_t udsize);

#define pkg_ldhdr(h, fname, ldflags) \
   pkg_ldhdr_udata(h, fname, ldflags, NULL, 0)

struct pkg *pkg_ldrpm_udata(const char *path, unsigned ldflags,
                            void *udata, size_t udsize);

#define pkg_ldrpm(path, ldflags) pkg_ldrpm_udata(path, ldflags, NULL, 0)

void pkg_free(struct pkg *p);

/* add self name-evr to caps */
int pkg_add_selfcap(struct pkg *pkg);

int pkg_cmp_name(const struct pkg *p1, const struct pkg *p2); 
int pkg_cmp_evr(const struct pkg *p1, const struct pkg *p2);
int pkg_cmp_name_evr(const struct pkg *p1, const struct pkg *p2);
int pkg_cmp_name_ver(const struct pkg *p1, const struct pkg *p2);
int pkg_cmp_name_evr_rev(const struct pkg *p1, const struct pkg *p2);

int pkg_eq_capreq(const struct pkg *p, const struct capreq *cr);

/* look up into package caps only */
int pkg_caps_match_req(const struct pkg *pkg, const struct capreq *req,
                        int strict);

int pkg_evr_match_req(const struct pkg *pkg, const struct capreq *req);


int cap_match_req(const struct capreq *cap, const struct capreq *req,
                  int strict);
/*  */
int pkg_match_req(const struct pkg *pkg, const struct capreq *req,
                  int strict);
#if 0
int pkg_obsoletes_pkg(const struct pkg *pkg, const struct pkg *opkg);
#endif

int pkg_add_pkgcnfl(struct pkg *pkg, struct pkg *cpkg, int isbastard);
int pkg_has_pkgcnfl(struct pkg *pkg, struct pkg *cpkg);

/* RET %path/%name-%version-%release.%arch.rpm  */
char *pkg_filename(const struct pkg *pkg, char *buf, size_t size);
char *pkg_filename_s(const struct pkg *pkg);

int pkg_printf(const struct pkg *pkg, const char *str);
char *pkg_snprintf(char *str, size_t size, const struct pkg *pkg);
char *pkg_snprintf_s(const struct pkg *pkg);
char *pkg_snprintf_s0(const struct pkg *pkg);
char *pkg_snprintf_s1(const struct pkg *pkg);

void *pkg_uinf_tag(struct pkg *pkg, int tag);

#define pkg_group(pkg) pkg_uinf_tag(pkg, RPMTAG_GROUP)
#define pkg_summary(pkg) pkg_uinf_tag(pkg, RPMTAG_SUMMARY)
#define pkg_description(pkg) pkg_uinf_tag(pkg, RPMTAG_DESCRIPTION)

void set_pkg_allocfn(void *(*pkg_allocfn)(size_t), void (*pkg_freefn)(void*));

#endif /* POLDEK_PKG_H */
