/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_PSREQ_H
#define POLDEK_PSREQ_H

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>

#include "pkg.h"

#define REQPKG_PREREQ     (1 << 0)
#define REQPKG_PREREQ_UN  (1 << 1)
#define REQPKG_CONFLICT   (1 << 2)
#define REQPKG_OBSOLETE   (1 << 3)
#define REQPKG_MULTI      (1 << 7) /* with alternatives */

struct reqpkg {
    struct pkg    *pkg;
    struct capreq *req;
    uint8_t       flags;
    struct reqpkg *adds[0];     /* NULL terminated  */
};

struct pkgset;
/*
  Find requirement looking into capabilities and file list.
  RET: bool && matched packages in *packages
 */
int pkgset_find_match_packages(struct pkgset *ps,
                               const struct pkg *pkg, const struct capreq *req,
                               tn_array **packages, int strict);

int pkgset_verify_deps(struct pkgset *ps, int strict);
int pkgset_verify_conflicts(struct pkgset *ps, int strict);

tn_array *pspkg_obsoletedby(struct pkgset *ps, struct pkg *pkg, int bymarked);

struct pkg_unreq {
    uint8_t       mismatch;
    char          req[0];
};


#endif /* POLDEK_PSREQ_H */
