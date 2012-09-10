/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef  POLDEK_PKGCMP_H
#define  POLDEK_PKGCMP_H

#ifndef EXPORT
# define EXPORT extern
#endif

struct pkg;
struct capreq;

#ifdef SWIG
# define extern__inline
#else
# define extern__inline inline
#endif

/* candidate in pkg's rainbow */
EXPORT int pkg_is_colored_like(const struct pkg *candidate, const struct pkg *pkg);

/* same name && arch */
EXPORT int pkg_is_kind_of(const struct pkg *candidate, const struct pkg *pkg);

/* ret : 0 if pkg is cappable to upgrade arch<=>arch, arch<=>noarch */
EXPORT int pkg_is_arch_compat(const struct pkg *candidate, const struct pkg *pkg);

/* strncmp(p1->name, p2->name, strlen(p2->name)) */
EXPORT extern__inline int pkg_ncmp_name(const struct pkg *p1, const struct pkg *p2);

/* strcmp(p1->name, p2->name) */
EXPORT int pkg_cmp_name(const struct pkg *p1, const struct pkg *p2);

/* strcmp(pkg_id(p1), pkg_id(p2) */
EXPORT extern__inline int pkg_cmp_id(const struct pkg *p1, const struct pkg *p2);


/* versions only (+epoch) */
EXPORT int pkg_cmp_ver(const struct pkg *p1, const struct pkg *p2);
/* EVR only */
EXPORT int pkg_cmp_evr(const struct pkg *p1, const struct pkg *p2);
/* ARCH only */
EXPORT int pkg_cmp_arch(const struct pkg *p1, const struct pkg *p2);


/* Name-EVR */
EXPORT int pkg_cmp_name_evr(const struct pkg *p1, const struct pkg *p2);
/* Like above, but reversed EVR */
EXPORT int pkg_cmp_name_evr_rev(const struct pkg *p1, const struct pkg *p2);

//Dint pkg_cmp_name_srcpri(const struct pkg *p1, const struct pkg *p2);

/* pkg_cmp_name_evr_rev() + package which fits better to current
   architecture is _lower_ (notice _rev_)  */
EXPORT int pkg_cmp_name_evr_arch_rev_srcpri(const struct pkg *p1, const struct pkg *p2);

/* compares pri, then name_evr_rev() */
EXPORT int pkg_cmp_pri_name_evr_rev(struct pkg *p1, struct pkg *p2);

/* compares recno only */
EXPORT int pkg_cmp_recno(const struct pkg *p1, const struct pkg *p2);

/* like pkg_cmp_name_evr() but VR is compared by strcmp() */
EXPORT int pkg_strcmp_name_evr_rev(const struct pkg *p1, const struct pkg *p2);

/* with warn message, for n_array_uniq() only */
EXPORT int pkg_cmp_uniq_name(const struct pkg *p1, const struct pkg *p2);
EXPORT int pkg_cmp_uniq_name_evr(const struct pkg *p1, const struct pkg *p2);
EXPORT int pkg_cmp_uniq_name_evr_arch(const struct pkg *p1, const struct pkg *p2);

/* compares most of packages data */
EXPORT int pkg_deepcmp_name_evr_rev(const struct pkg *p1, const struct pkg *p2);
EXPORT int pkg_deepstrcmp_name_evr(const struct pkg *p1, const struct pkg *p2);


EXPORT int pkg_eq_name_prefix(const struct pkg *pkg1, const struct pkg *pkg2);
EXPORT int pkg_eq_capreq(const struct pkg *pkg, const struct capreq *cr);


/* compares nvr using strcmp() */
EXPORT int pkg_nvr_strcmp(struct pkg *p1, struct pkg *p2);
EXPORT int pkg_nvr_strcmp_rev(struct pkg *p1, struct pkg *p2);

EXPORT int pkg_nvr_strncmp(struct pkg *pkg, const char *name);

EXPORT int pkg_nvr_strcmp_btime(struct pkg *p1, struct pkg *p2);
EXPORT int pkg_nvr_strcmp_btime_rev(struct pkg *p1, struct pkg *p2);

EXPORT int pkg_nvr_strcmp_bday(struct pkg *p1, struct pkg *p2);
EXPORT int pkg_nvr_strcmp_bday_rev(struct pkg *p1, struct pkg *p2);

#endif 
