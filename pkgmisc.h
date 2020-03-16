/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_PKGMISC_H
#define POLDEK_PKGMISC_H

#include <stdint.h>
#include <trurl/narray.h>
#include <stdbool.h>
#ifndef EXPORT
# define EXPORT extern
#endif

/*  === pkgscore ===  */
struct pkgscore_s {
    char        pkgbuf[512];
    int         pkgname_off;
    struct pkg  *pkg;
};

EXPORT void pkgscore_match_init(struct pkgscore_s *psc, struct pkg *pkg);
EXPORT int pkgscore_match(struct pkgscore_s *psc, const char *mask);
EXPORT void packages_score(tn_array *pkgs, tn_array *patterns, unsigned scoreflag);

/* mark matches by PKG_IGNORED and remove them if remove is set */
EXPORT int packages_score_ignore(tn_array *pkgs, tn_array *patterns, int remove);

/*  === utils ===  */
EXPORT int packages_dump(tn_array *pkgs, const char *path, int fqfn);
EXPORT int packages_uniq(tn_array *pkgs, bool by_name);


struct pm_ctx;
/* pmctx is needed to call pm_verify_signature() */
EXPORT void packages_fetch_summary(struct pm_ctx *pmctx, const tn_array *pkgs,
                            const char *destdir, int nosubdirs);

EXPORT int packages_fetch(struct pm_ctx *pmctx,
                   tn_array *pkgs, const char *destdir, int nosubdirs);

EXPORT int packages_fetch_remove(tn_array *pkgs, const char *destdir);



#define PKGVERIFY_MD   (1 << 0)
#define PKGVERIFY_GPG  (1 << 1)
#define PKGVERIFY_PGP  (1 << 2)
EXPORT unsigned pkg_get_verify_signflags(struct pkg *pkg);



EXPORT int poldek_util_parse_evr(char *evrstr, int32_t *epoch,
                          const char **ver, const char **rel);
EXPORT int poldek_util_parse_nevr(char *nevrstr, const char **name,
                           int32_t *epoch, const char **ver, const char **rel);


/*  === pkgmark_set ===  */
struct pkgmark_set;
#define PKGMARK_SET_IDNEVR (1 << 0) /* id = pkg_id() */
#define PKGMARK_SET_IDPTR  (1 << 1) /* id = printf("%p", pkg); */

EXPORT struct pkgmark_set *pkgmark_set_new(int size, unsigned flags);
EXPORT void pkgmark_set_free(struct pkgmark_set *pms);
EXPORT int pkgmark_set(struct pkgmark_set *pms, struct pkg *pkg, int set,
                uint32_t flag);

EXPORT int pkgmark_isset(const struct pkgmark_set *pms, const struct pkg *pkg,
                  uint32_t flag);

EXPORT tn_array *pkgmark_get_packages(struct pkgmark_set *pmark, uint32_t flag);

// pkg_set_MarkFlag
#define pkg_set_mf(pms, pkg, flag) pkgmark_set(pms, pkg, 1, flag)
#define pkg_clr_mf(pms, pkg, flag) pkgmark_set(pms, pkg, 0, flag)
#define pkg_isset_mf(pms, pkg, flag) pkgmark_isset(pms, pkg, flag)

#define PKGMARK_MARK        (1 << 1)  /* marked directly, i.e. by the user*/
#define PKGMARK_DEP         (1 << 2)  /* marked by deps */
#define pkg_hand_mark(pms, pkg) pkgmark_set(pms, pkg, 1, PKGMARK_MARK)
#define pkg_dep_mark(pms, pkg) pkgmark_set(pms, pkg, 1, PKGMARK_DEP)
#define pkg_unmark(pms, pkg) pkgmark_set(pms, pkg, 0, PKGMARK_DEP|PKGMARK_MARK)

#define pkg_is_dep_marked(pms, pkg) pkgmark_isset(pms, pkg, PKGMARK_DEP)
#define pkg_is_hand_marked(pms, pkg)  pkgmark_isset(pms, pkg, PKGMARK_MARK)
#define pkg_is_marked(pms, pkg) pkgmark_isset(pms, pkg, PKGMARK_MARK|PKGMARK_DEP)
#define pkg_isnot_marked(pms, pkg) (!pkgmark_isset(pms, pkg, PKGMARK_MARK|PKGMARK_DEP))


#define PKGMARK_RM          (1 << 3) /* marked for removal */
#define pkg_rm_mark(pms, pkg) pkgmark_set(pms, pkg, 1, PKGMARK_RM)
#define pkg_is_rm_marked(pms, pkg) pkgmark_isset(pms, pkg, PKGMARK_RM)
#define pkg_rm_unmark(pms, pkg) pkgmark_set(pms, pkg, 0, PKGMARK_RM)


#define PKGMARK_INTERNAL    (1 << 4)
#define pkg_mark_i(pms, pkg) pkgmark_set(pms, pkg, 1, PKGMARK_INTERNAL)
#define pkg_unmark_i(pms, pkg) pkgmark_set(pms, pkg, 0, PKGMARK_INTERNAL)
#define pkg_is_marked_i(pms, pkg) pkgmark_isset(pms, pkg, PKGMARK_INTERNAL)


#define PKGMARK_UNMETDEPS   (1 << 5)
#define pkg_set_unmetdeps(pms, pkg) pkgmark_set(pms, pkg, 1, PKGMARK_UNMETDEPS)
#define pkg_clr_unmetdeps(pms, pkg) pkgmark_set(pms, pkg, 0, PKGMARK_UNMETDEPS)
#define pkg_has_unmetdeps(pms, pkg) pkgmark_isset(pms, pkg, PKGMARK_UNMETDEPS)


#define PKGMARK_WHITE     (1 << 10)
#define PKGMARK_GRAY      (1 << 11)
#define PKGMARK_BLACK     (1 << 12)
#define PKGMARK_ALLCOLORS (PKGMARK_WHITE | PKGMARK_GRAY | PKGMARK_BLACK)

#if 0
#define pkg_set_color(pms, pkg, c) \
   do { pkg_clr_mf(pms, pkg, ~(PKGMARK_ALLCOLORS)); \
        pkg_set_mf(pms, pkg, c); } while (0)

#define pkg_is_color(pms, pkg, c) pkg_isset_mf(pms, pkg, c)
#endif

EXPORT void pkgmark_massset(struct pkgmark_set *pmark, int set, uint32_t flag);

/* mark packages (PKGMARK_{MARK,DEP}) to pms */
EXPORT int packages_mark(struct pkgmark_set *pms, const tn_array *pkgs, int withdeps);
/* check how many packages are required by pkg */
EXPORT int pkgmark_pkg_drags(struct pkg *pkg, struct pkgmark_set *pms, int deep);
/* .. and then verify marked set  */
EXPORT int pkgmark_verify_package_conflicts(struct pkgmark_set *pms);

struct pkgset;
EXPORT int packages_verify_dependecies(tn_array *pkgs, struct pkgset *ps);
EXPORT int packages_generate_depgraph(tn_array *pkgs, struct pkgset *ps,
                               const char *graphspec);
#endif
