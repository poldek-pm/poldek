/* $Id$ */
#ifndef  POLDEK_PKGSET_H
#define  POLDEK_PKGSET_H

#include <stdint.h>
#include <obstack.h>
#include <trurl/narray.h>

#include "pkg.h"
#include "pkgdir/pkgdir.h"
//#include "pkgdb/pkgdb.h"

#include "fileindex.h"
#include "capreqidx.h"

struct pkgset {
    unsigned           flags;
    
    tn_array           *pkgs;           /*  pkg* []    */
    tn_array           *ordered_pkgs;   /*  pkg* []    */
    
    tn_array           *pkgdirs;        /*  pkgdir* [] */
 
    tn_array           *depdirs;        /*  char* []   */
    int                nerrors;
    
    tn_array           *rpmcaps;        /*  capreq* [] */

    tn_hash            *_vrfy_unreqs;
    tn_array           *_vrfy_file_conflicts;
    
    struct capreq_idx  cap_idx;    /* 'name'  => *pkg[]  */
    struct capreq_idx  req_idx;    /*  -"-               */
    struct capreq_idx  obs_idx;    /*  -"-               */     
    struct file_index  file_idx;   /* 'file'  => *pkg[]  */
};

int packages_order(tn_array *pkgs, tn_array **ordered_pkgs);
int pkgset_order(struct pkgset *ps, int verbose);

/* if set then:
 * - requirements matched even if requirement has version
 *   while capability hasn't (RPM style)
 * - files with different modes only are not assumed as conflicts
 */



struct pkgset *pkgset_new();
void pkgset_free(struct pkgset *ps);

int pkgset_load(struct pkgset *ps, int ldflags, tn_array *sources);

#define PSET_VRFY_MERCY          (1 << 0)
#define PSET_VRFY_PROMOTEPOCH    (1 << 1)

#define PSET_DBDIRS_LOADED       (1 << 4)

#define PSET_VERIFY_ORDER        (1 << 7)
#define PSET_VERIFY_FILECNFLS    (1 << 9)
#define PSET_UNIQ_PKGNAME        (1 << 10)


int pkgset_setup(struct pkgset *ps, unsigned flags);

tn_array *pkgset_lookup_cap(struct pkgset *ps, const char *capname);
struct pkg *pkgset_lookup_pkgn(struct pkgset *ps, const char *name);

tn_array *pkgset_get_packages_bynvr(const struct pkgset *ps);

/* pkgset-mark.c */
int pkgset_mark_packages(struct pkgset *ps, const tn_array *pkgs,
                         tn_array *marked, int withdeps);

void packages_mark(tn_array *pkgs, unsigned flags_on, unsigned flags_off);
#define packages_unmark_all(pkgs) packages_mark(pkgs, 0, PKG_INDIRMARK | PKG_DIRMARK)

int pkgset_rpmprovides(const struct pkgset *ps, const struct capreq *req);

#endif /* POLDEK_PKGSET_H */
