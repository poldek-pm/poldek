/* $Id$ */
#ifndef  POLDEK_RPM_PKGLOAD_H
#define  POLDEK_RPM_PKGLOAD_H

#include <rpm/rpmlib.h>
#include "pkg.h"

#ifdef POLDEK_RPM_INTERNAL
#define CRTYPE_CAP  1
#define CRTYPE_REQ  2
#define CRTYPE_CNFL 3
#define CRTYPE_OBSL 4

tn_array *rpm_capreqs_ldhdr(tn_array *arr, const Header h, int crtype);

#endif

struct pkg *pkg_ldrpmhdr(Header h, const char *fname, unsigned fsize,
                         unsigned ldflags);

struct pkg *pkg_ldrpm(const char *path, unsigned ldflags);

#endif
