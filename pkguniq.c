/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdbool.h>
#include <trurl/narray.h>

#include "pkg.h"
#include "pkgcmp.h"

static
int do_pkg_cmp_uniq_name_evr(const struct pkg *p1, struct pkg *p2)
{
    register int rc;

    if ((rc = pkg_cmp_uniq_name_evr(p1, p2)) == 0)
        pkg_score(p2, PKG_IGNORED_UNIQ);

    return rc;
}

static
int do_pkg_cmp_uniq_name(const struct pkg *p1, struct pkg *p2)
{
    register int rc;

    if ((rc = pkg_cmp_uniq_name(p1, p2)) == 0)
        pkg_score(p2, PKG_IGNORED_UNIQ);

    return rc;
}

int packages_uniq(tn_array *pkgs, bool names)
{
    int n = n_array_size(pkgs);

    if (names) {
        n_array_uniq_ex(pkgs, (tn_fn_cmp)do_pkg_cmp_uniq_name);
    } else {
        n_array_uniq_ex(pkgs, (tn_fn_cmp)do_pkg_cmp_uniq_name_evr);
    }

    return n - n_array_size(pkgs);
}
