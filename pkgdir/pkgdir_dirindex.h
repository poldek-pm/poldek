/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef PKGDIR_DIRINDEX_H
#define PKGDIR_DIRINDEX_H
/* Directory index */

#include <trurl/narray.h>

struct pkgdir;
struct pkgdir_dirindex;

struct pkgdir_dirindex *pkgdir__dirindex_open(struct pkgdir *pkgdir);
void pkgdir__dirindex_close(struct pkgdir_dirindex *dirindex);

/* returns path providers */
tn_array *pkgdir_dirindex_get(const struct pkgdir *pkgdir,
                              tn_array *pkgs, const char *path);
/* path belongs to pkg? */
int pkgdir_dirindex_pkg_has_path(const struct pkgdir *pkgdir,
                                 const struct pkg *pkg, const char *path);

/* returns directories required by package */
tn_array *pkgdir_dirindex_get_required(const struct pkgdir *pkgdir,
                                       const struct pkg *pkg);

/* for public prototypes see pkgdir.h */
#endif
