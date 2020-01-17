/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef PKGDIR_STUBINDEX_H
#define PKGDIR_STUBINDEX_H
/* "Stub" package index */

#include <trurl/narray.h>

struct source;
struct pkgdir;

// prototype of source_stubload in source.h
// tn_array *source_stubload(struct source *src);

void pkgdir__stubindex_update(struct pkgdir *pkgdir);

#endif
