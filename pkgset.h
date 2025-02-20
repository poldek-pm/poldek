/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef  POLDEK_PKGSET_H
#define  POLDEK_PKGSET_H

#include <stdbool.h>
#include <stdint.h>
#include <trurl/narray.h>

#include "capreqidx.h"

struct file_index;
struct pkgdir;

struct pkgset {
    tn_array           *pkgs;           /*  pkg* []    */
    tn_array           *pkgdirs;        /*  pkgdir* [] */
    tn_array           *depdirs;        /*  char* []   */

    struct pm_ctx      *pmctx;

    tn_hash            *_depinfocache;

    struct capreq_idx  cap_idx;    /* 'name'  => *pkg[]  */
    struct capreq_idx  req_idx;    /*  -"-               */
    struct capreq_idx  obs_idx;    /*  -"-               */
    struct capreq_idx  cnfl_idx;    /*  -"-               */
    struct file_index  *file_idx;   /* 'file'  => *pkg[]  */
};

struct pm_ctx;
struct pkgset *pkgset_new(struct pm_ctx *ctx);
void pkgset_free(struct pkgset *ps);

int pkgset_load(struct pkgset *ps, int ldflags, tn_array *sources);
int pkgset_add_pkgdir(struct pkgset *ps, struct pkgdir *pkgdir);

int pkgset__index_caps(struct pkgset *ps);
//int pkgset__index_reqs(struct pkgset *ps);


// pkgset-req.c
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

/*
  Find requirement looking into capabilities and file list.
  RET: bool && matched packages in *packages
 */
int pkgset_find_match_packages(struct pkgset *ps,
                               const struct pkg *pkg, const struct capreq *req,
                               tn_array **packages, bool strict);

struct pkg_unreq {
    bool          mismatch;
    char          req[0];
};

tn_array *pkgset_get_conflicted_packages(int indent, struct pkgset *ps, const struct pkg *pkg);
tn_array *pkgset_get_required_packages_x(int indent, struct pkgset *ps, const struct pkg *pkg, tn_hash **unreqh);
tn_array *pkgset_get_required_packages(int indent, struct pkgset *ps, const struct pkg *pkg);

/* pkgset-order.c */
#define PKGORDER_INSTALL     1
#define PKGORDER_UNINSTALL   2
int pkgset_order_ex(struct pkgset *ps, const tn_array *pkgs, tn_array **ordered_pkgs, int ordertype, int verbose_level);
int pkgset_order(struct pkgset *ps, const tn_array *pkgs, tn_array **ordered_pkgs, int ordertype);

#include "poldek.h"
enum pkgset_search_tag {
    PS_SEARCH_RECNO = POLDEK_ST_RECNO,
    PS_SEARCH_NAME  = POLDEK_ST_NAME,
    PS_SEARCH_CAP   = POLDEK_ST_CAP,        /* what provides cap */
    PS_SEARCH_REQ   = POLDEK_ST_REQ,        /* what requires */
    PS_SEARCH_CNFL  = POLDEK_ST_CNFL,
    PS_SEARCH_OBSL  = POLDEK_ST_OBSL,
    PS_SEARCH_FILE  = POLDEK_ST_FILE,
    PS_SEARCH_PROVIDES = POLDEK_ST_PROVIDES     /* what provides cap or file */
};

tn_array *pkgset_search(struct pkgset *ps, enum pkgset_search_tag tag,
                        const char *value);

int pkgset_pm_satisfies(const struct pkgset *ps, const struct capreq *req);


int pkgset_add_package(struct pkgset *ps, struct pkg *pkg);
int pkgset_remove_package(struct pkgset *ps, struct pkg *pkg);

#endif /* POLDEK_PKGSET_H */
