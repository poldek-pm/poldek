/* $Id$ */
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

#define REQPKG_MULTI      (1 << 7) /* has adds */

struct reqpkg {
    struct pkg    *pkg;
    struct capreq *req;
    uint8_t       flags;
    struct reqpkg *adds[0];     /* NULL terminated  */
};

struct pkgset;
/*
  Find requirement looking into capabilities and file list.
  ARGS: pkgsbuf to store suspects (used if finded in file list *only*)
  RET: bool, suspkgs and npkgs is set to suspect packages table
 */
int psreq_lookup(struct pkgset *ps, struct capreq *req,
                 struct pkg ***suspkgs, struct pkg **pkgsbuf, int *npkgs);

/* match suspkgs to req, store matches in matchedpkgs */
int psreq_match_pkgs(const struct pkg *pkg, struct capreq *req, int strict, 
                     struct pkg *suspkgs[], int npkgs,
                     struct pkg **matches, int *nmatched);

int pkgset_verify_deps(struct pkgset *ps, int strict);
int pkgset_verify_conflicts(struct pkgset *ps, int strict);

tn_array *pspkg_obsoletedby(struct pkgset *ps, struct pkg *pkg, int bymarked);

struct pkg_unreq {
    uint8_t       mismatch;
    char          req[0];
};


#endif /* POLDEK_PSREQ_H */
