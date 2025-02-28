/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_USRSET_H
#define POLDEK_USRSET_H

#include <stdint.h>
#include <trurl/narray.h>

#ifndef EXPORT
#  define EXPORT extern
#endif

struct pkg;
struct arg_packages;
struct pkgset;
struct pm_ctx;

EXPORT struct arg_packages *arg_packages_new(void);
EXPORT void arg_packages_free(struct arg_packages *aps);

EXPORT void arg_packages_clean(struct arg_packages *aps);
EXPORT int arg_packages_size(struct arg_packages *aps);

EXPORT tn_array *arg_packages_get_masks(struct arg_packages *aps, int hashed);

EXPORT int arg_packages_add_pkgmask(struct arg_packages *aps, const char *mask);
EXPORT int arg_packages_add_pkgmaska(struct arg_packages *aps, tn_array *masks);
EXPORT int arg_packages_add_pkgfile(struct arg_packages *aps, const char *pathname);
EXPORT int arg_packages_add_pkglist(struct arg_packages *aps, const char *path);
EXPORT int arg_packages_add_pkg(struct arg_packages *aps, struct pkg *pkg);
EXPORT int arg_packages_add_pkgs(struct arg_packages *aps, const tn_array *pkgs);

EXPORT int arg_packages_setup(struct arg_packages *aps, struct pm_ctx *ctx);

#define ARG_PACKAGES_RESOLV_EXACT       (1 << 0)/* no fnmatch() */
#define ARG_PACKAGES_RESOLV_MISSINGOK   (1 << 1)/* be quiet if nothing matches*/
#define ARG_PACKAGES_RESOLV_UNAMBIGUOUS (1 << 2)/* don't match duplicates */
#define ARG_PACKAGES_RESOLV_CAPS        (1 << 3)/* search in package caps too */
#define ARG_PACKAGES_RESOLV_CAPSINLINE  (1 << 4)/* add packages found by caps
                                                   to resolved packages */
#define ARG_PACKAGES_RESOLV_WARN_ONLY   (1 << 5)/* warn only*/

int arg_packages__validate_with_stubs(struct arg_packages *aps, tn_array *stubpkgs,
                                      tn_array **resolved, int quiet);

EXPORT int arg_packages_resolve(struct arg_packages *aps, tn_array *avpkgs,
                                struct pkgset *ps, unsigned flags);

EXPORT tn_hash *arg_packages_get_resolved_caps(struct arg_packages *aps);
EXPORT tn_array *arg_packages_get_resolved(struct arg_packages *aps);


#endif
