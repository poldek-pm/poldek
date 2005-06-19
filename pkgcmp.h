/* $Id$ */
#ifndef  POLDEK_PKGCMP_H
#define  POLDEK_PKGCMP_H

struct pkg;
struct capreq;

#ifdef SWIG
# define extern__inline
#else
# define extern__inline extern inline
#endif

int pkg_is_kind_of(const struct pkg *candidate, const struct pkg *pkg);

/* strncmp(p1->name, p2->name, strlen(p2->name)) */
extern__inline int pkg_ncmp_name(const struct pkg *p1, const struct pkg *p2);

/* strcmp(p1->name, p2->name) */
extern__inline int pkg_cmp_name(const struct pkg *p1, const struct pkg *p2);

/* versions only */
int pkg_cmp_ver(const struct pkg *p1, const struct pkg *p2);
/* EVR only */
int pkg_cmp_evr(const struct pkg *p1, const struct pkg *p2);
/* ARCH only */
int pkg_cmp_arch(const struct pkg *p1, const struct pkg *p2);


/* Name-EVR */
int pkg_cmp_name_evr(const struct pkg *p1, const struct pkg *p2);
/* Like above, but reversed EVR */
int pkg_cmp_name_evr_rev(const struct pkg *p1, const struct pkg *p2);

//Dint pkg_cmp_name_srcpri(const struct pkg *p1, const struct pkg *p2);

/* pkg_cmp_name_evr_rev() + package which fits better to current
   architecture is _lower_ (notice _rev_)  */
int pkg_cmp_name_evr_arch_rev_srcpri(const struct pkg *p1, const struct pkg *p2);

/* compares pri, then name_evr_rev() */
int pkg_cmp_pri_name_evr_rev(struct pkg *p1, struct pkg *p2);

/* compares recno only */
int pkg_cmp_recno(const struct pkg *p1, const struct pkg *p2);

/* like pkg_cmp_name_evr() but VR is compared by strcmp() */
int pkg_strcmp_name_evr_rev(const struct pkg *p1, const struct pkg *p2);

/* with warn message, for n_array_uniq() only */
int pkg_cmp_uniq_name(const struct pkg *p1, const struct pkg *p2);
int pkg_cmp_uniq_name_evr(const struct pkg *p1, const struct pkg *p2);
int pkg_cmp_uniq_name_evr_arch(const struct pkg *p1, const struct pkg *p2);

/* compares most of packages data */
int pkg_deepcmp_name_evr_rev(const struct pkg *p1, const struct pkg *p2);
int pkg_deepstrcmp_name_evr(const struct pkg *p1, const struct pkg *p2);


int pkg_eq_name_prefix(const struct pkg *pkg1, const struct pkg *pkg2);
int pkg_eq_capreq(const struct pkg *pkg, const struct capreq *cr);


/* compares nvr using strcmp() */
int pkg_nvr_strcmp(struct pkg *p1, struct pkg *p2);
int pkg_nvr_strcmp_rev(struct pkg *p1, struct pkg *p2);

int pkg_nvr_strncmp(struct pkg *pkg, const char *name);

int pkg_nvr_strcmp_btime(struct pkg *p1, struct pkg *p2);
int pkg_nvr_strcmp_btime_rev(struct pkg *p1, struct pkg *p2);

int pkg_nvr_strcmp_bday(struct pkg *p1, struct pkg *p2);
int pkg_nvr_strcmp_bday_rev(struct pkg *p1, struct pkg *p2);

#endif 
