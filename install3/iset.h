/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_INSTALL3_ISET_H
#define POLDEK_INSTALL3_ISET_H

#include <stdint.h>
#include <trurl/narray.h>

#include "pkgmisc.h"
#include "pkgset.h"

struct pkg;
struct capreq;
struct iset;

struct iset *iset_new(void);
void iset_free(struct iset *iset);

//inline FIXME - is inline function can be defined outside header?
void iset_markf(struct iset *iset, struct pkg *pkg, unsigned mflag);
//inline
int iset_ismarkedf(struct iset *iset, const struct pkg *pkg, unsigned mflag);

const struct pkgmark_set *iset_pms(struct iset *iset);
const tn_array *iset_packages(struct iset *iset);

/* return array sorted by package recno */
const tn_array *iset_packages_by_recno(struct iset *iset);

void iset_add(struct iset *iset, struct pkg *pkg, unsigned mflag);
int  iset_remove(struct iset *iset, struct pkg *pkg);

int iset_provides(struct iset *iset, const struct capreq *cap);
// returns how many pkg reqs are in iset
int iset_reqs_score(struct iset *iset, const struct pkg *pkg);

int iset_has_pkg(struct iset *iset, const struct pkg *pkg);
// return 1st found pkg_is_kind_of from iset or null
struct pkg *iset_has_kind_of_pkg(struct iset *iset, const struct pkg *pkg);

#endif
