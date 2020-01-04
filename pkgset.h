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

#include <stdint.h>
#include <trurl/narray.h>

#include "capreqidx.h"

struct file_index;
struct pkgdir;

struct pkgset {
    uint32_t           flags;

    tn_array           *pkgs;           /*  pkg* []    */
    tn_array           *ordered_pkgs;   /* in install order pkg* []    */

    tn_array           *pkgdirs;        /*  pkgdir* [] */

    tn_array           *depdirs;        /*  char* []   */
    int                nerrors;

    struct pm_ctx      *pmctx;

    tn_hash            *_vrfy_unreqs;
    tn_array           *_vrfy_file_conflicts;

    struct capreq_idx  cap_idx;    /* 'name'  => *pkg[]  */
    struct capreq_idx  req_idx;    /*  -"-               */
    struct capreq_idx  obs_idx;    /*  -"-               */
    struct capreq_idx  cnfl_idx;    /*  -"-               */
    struct file_index  *file_idx;   /* 'file'  => *pkg[]  */
};

#define PKGORDER_INSTALL     1
#define PKGORDER_UNINSTALL   2
int packages_order(tn_array *pkgs, tn_array **ordered_pkgs, int ordertype);

int packages_order_and_verify(tn_array *pkgs, tn_array **ordered_pkgs,
                              int ordertype, int verbose_level);

int pkgset_order(struct pkgset *ps, int verbose);


struct pm_ctx;
struct pkgset *pkgset_new(struct pm_ctx *ctx);
void pkgset_free(struct pkgset *ps);

int pkgset_load(struct pkgset *ps, int ldflags, tn_array *sources);
int pkgset_add_pkgdir(struct pkgset *ps, struct pkgdir *pkgdir);


/* VRFY_MERCY - if set then:
 * - requirements matched even if requirement has version
 *   while capability hasn't (RPM style)
 * - files with different modes only are not assumed as conflicts
 */
#define PSET_VRFY_MERCY          (1 << 0)
#define PSET_VRFY_PROMOTEPOCH    (1 << 1)

#define PSET_NODEPS              (1 << 5) /* skip deps processing (lazy), implies PSET_NOORDER */
#define PSET_NOORDER             (1 << 6)
#define PSET_VERIFY_ORDER        (1 << 7)
#define PSET_UNIQ_PKGNAME        (1 << 10)

#define PSET_RT_DBDIRS_LOADED    (1 << 11)
#define PSET_RT_INDEXED          (1 << 12)
#define PSET_RT_DEPS_PROCESSED   (1 << 13)
#define PSET_RT_ORDERED          (1 << 14)

int pkgset_setup(struct pkgset *ps, unsigned flags); /* uniqness, indexing */
int pkgset_setup_deps(struct pkgset *ps, unsigned flags); /* deps processing */

#include "poldek.h"
enum pkgset_search_tag {
    PS_SEARCH_RECNO = POLDEK_ST_RECNO,
    PS_SEARCH_NAME  = POLDEK_ST_NAME,
    PS_SEARCH_CAP   = POLDEK_ST_CAP,        /* what provides cap */
    PS_SEARCH_REQ   = 4,        /* what requires */
    PS_SEARCH_CNFL  = 5,
    PS_SEARCH_OBSL  = 6,
    PS_SEARCH_FILE  = 7,
    PS_SEARCH_PROVIDES = 8,     /* what provides cap or file */
};

tn_array *pkgset_search(struct pkgset *ps, enum pkgset_search_tag tag,
                        const char *value);

tn_array *pkgset_get_unsatisfied_reqs(struct pkgset *ps, struct pkg *pkg);

tn_array *pkgset_get_packages_bynvr(const struct pkgset *ps);

int pkgset_pm_satisfies(const struct pkgset *ps, const struct capreq *req);

void pkgset_report_fileconflicts(struct pkgset *ps, tn_array *pkgs);

int pkgset_add_package(struct pkgset *ps, struct pkg *pkg);
int pkgset_remove_package(struct pkgset *ps, struct pkg *pkg);

#endif /* POLDEK_PKGSET_H */
