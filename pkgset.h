/* $Id$ */
#ifndef  POLDEK_PKGSET_H
#define  POLDEK_PKGSET_H

#include <stdint.h>
#include <obstack.h>
#include <trurl/narray.h>

#include "pkg.h"
#include "pkgdir/pkgdir.h"
#include "pkgdb.h"
#include "usrset.h"

#include "fileindex.h"
#include "capreqidx.h"


int pkgsetmodule_init(void);
void pkgsetmodule_destroy(void);

#define _PKGSET_INDEXES_INIT      (1 << 20) /* internal flag  */

struct pkgset {
    unsigned           flags;
    
    tn_array           *pkgs;           /*  pkg* []    */
    tn_array           *ordered_pkgs;   /*  pkg* []    */
    tn_array           *pkgdirs;        /*  pkgdir* [] */
 
    tn_array           *depdirs;        /*  char* []   */
    int                nerrors;
    
    tn_array           *rpmcaps;        /*  capreq* [] */
    
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
#define PSVERIFY_MERCY        (1 << 0)
#define PSDBDIRS_LOADED       (1 << 5)

struct pkgset *pkgset_new(unsigned psoptflags);
void pkgset_free(struct pkgset *ps);

int pkgset_load(struct pkgset *ps, int ldflags, tn_array *sources);

#define PSET_VERIFY_DEPS         (1 << 0)
#define PSET_VERIFY_ORDER        (1 << 1)
#define PSET_VERIFY_CNFLS        (1 << 2)
#define PSET_VERIFY_FILECNFLS    (1 << 3)
#define PSET_DO_UNIQ_PKGNAME     (1 << 4)  

int pkgset_setup(struct pkgset *ps, unsigned flags);

/* returns sorted list of packages, free it by n_array_free() */
tn_array *pkgset_getpkgs(const struct pkgset *ps);

tn_array *pkgset_lookup_cap(struct pkgset *ps, const char *capname);
struct pkg *pkgset_lookup_pkgn(struct pkgset *ps, const char *name);

/* pkgset-mark.c */
int pkgset_mark_usrset(struct pkgset *ps, struct usrpkgset *ups,
                       int withdeps, int nodeps);

void packages_mark(tn_array *pkgs, unsigned flags_on, unsigned flags_off);
#define packages_unmark_all(pkgs) packages_mark(pkgs, 0, PKG_INDIRMARK | PKG_DIRMARK)

int pkgset_rpmprovides(const struct pkgset *ps, const struct capreq *req);


struct pkgscore_s {
    char        pkgbuf[512];
    int         pkgname_off;
    struct pkg  *pkg;
};

void pkgscore_match_init(struct pkgscore_s *psc, struct pkg *pkg);
int pkgscore_match(struct pkgscore_s *psc, const char *mask);
void packages_score(tn_array *pkgs, tn_array *patterns, unsigned scoreflag);

int packages_dump(tn_array *pkgs, const char *path, int fqfn);
int packages_fetch(tn_array *pkgs, const char *destdir, int nosubdirs);

struct poldek_ts;
int packages_rpminstall(tn_array *pkgs, struct poldek_ts *ts);


#define PKGVERIFY_MD   (1 << 0)
#define PKGVERIFY_GPG  (1 << 1)
#define PKGVERIFY_PGP  (1 << 2)

int package_verify_sign(const char *path, unsigned flags);

/* looks if pkg->pkgdir has set VERSIGN flag */
int package_verify_pgpg_sign(const struct pkg *pkg, const char *localpath);

#endif /* POLDEK_PKGSET_H */
