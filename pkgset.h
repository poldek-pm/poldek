/* $Id$ */
#ifndef  POLDEK_PKGSET_H
#define  POLDEK_PKGSET_H

#include <stdint.h>
#include <obstack.h>
#include <trurl/narray.h>

#include "fileindex.h"
#include "capreqidx.h"

struct pkgdir;
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
    struct capreq_idx  cnfl_idx;    /*  -"-               */
    struct file_index  file_idx;   /* 'file'  => *pkg[]  */
    int                _recno;
};

int packages_order(tn_array *pkgs, tn_array **ordered_pkgs);
int pkgset_order(struct pkgset *ps, int verbose);

/* if set then:
 * - requirements matched even if requirement has version
 *   while capability hasn't (RPM style)
 * - files with different modes only are not assumed as conflicts
 */

struct pm_ctx;
struct pkgset *pkgset_new(struct pm_ctx *ctx);
void pkgset_free(struct pkgset *ps);

int pkgset_load(struct pkgset *ps, int ldflags, tn_array *sources);
int pkgset_add_pkgdir(struct pkgset *ps, struct pkgdir *pkgdir);

#define PSET_VRFY_MERCY          (1 << 0)
#define PSET_VRFY_PROMOTEPOCH    (1 << 1)

#define PSET_DBDIRS_LOADED       (1 << 4)

#define PSET_VERIFY_ORDER        (1 << 7)
#define PSET_VERIFY_FILECNFLS    (1 << 9)
#define PSET_UNIQ_PKGNAME        (1 << 10)

int pkgset_setup(struct pkgset *ps, unsigned flags);

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

tn_array *pkgset_lookup_cap(struct pkgset *ps, const char *capname);
struct pkg *pkgset_lookup_1package(struct pkgset *ps, const char *name);

tn_array *pkgset_get_unsatisfied_reqs(struct pkgset *ps, struct pkg *pkg);

tn_array *pkgset_get_packages_bynvr(const struct pkgset *ps);

int pkgset_pmprovides(const struct pkgset *ps, const struct capreq *req);

#endif /* POLDEK_PKGSET_H */
