/* $Id$ */
#ifndef  POLDEK_PKGDB___H
#define  POLDEK_PKGDB___H

#include <stdint.h>
#include <obstack.h>
#include <trurl/narray.h>

#include "pkg.h"
#include "pkgdir/pkgdir.h"
#include "pkgdb.h"

struct pkgdb {
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


struct pkgdb *pkgdb_new(unsigned psoptflags);
void pkgdb_free(struct pkgdb *ps);

int pkgdb_load(struct pkgdb *ps, int ldflags, tn_array *sources);

#define PSET_VERIFY_DEPS         (1 << 0)
#define PSET_VERIFY_ORDER        (1 << 1)
#define PSET_VERIFY_CNFLS        (1 << 2)
#define PSET_VERIFY_FILECNFLS    (1 << 3)
#define PSET_DO_UNIQ_PKGNAME     (1 << 4)  

int pkgdb_setup(struct pkgdb *ps, unsigned flags);

struct pkgdb_ts {
    unsigned       ldflags;
    struct pkgdb   *db;
    void           *_cache;
};


tn_array *pkgdb_lookup_capreq(struct pkgdb *ps, tn_array *dst,
                              int type, const char *name,
                              tn_array *skipl);

tn_array *pkgdb_lookup_pkgn(struct pkgdb *ps, const char *name);
struct pkg *pkgdb_lookup_pkg(struct pkgdb *ps, struct pkg *pkg);


//tn_array *rpm_get_conflicted_dbpkgs(rpmdb db, const struct capreq *cap,
//                                    tn_array *unistdbpkgs, unsigned ldflags)

tn_array *pkgdb_lookup_conflicted_withcap(struct pkgdb *pp, tn_array *dst,
                                          const struct capreq *cap,
                                          tn_array *skipl);

tn_array *pkgdb_lookup_conflicted_withpath(struct pkgdb *pp, tn_array *dst,
                                           const char *path, tn_array *skipl);

//int rpm_get_pkgs_requires_capn(rpmdb db, tn_array *dbpkgs, const char *capname,
//                               tn_array *unistdbpkgs, unsigned ldflags);

tn_array *pkgdb_lookup_requirescap(struct pkgdb *pp, tn_array *dst,
                                     const struct capreq *cap,
                                     tn_array *skipl);


//int rpm_get_obsoletedby_pkg(rpmdb db, tn_array *dbpkgs, const struct pkg *pkg,
//                            unsigned ldflags);


tn_array *pkgdb_lookup_obsoletedby_pkg(struct pkgdb *pp, tn_array *dst,
                                         const struct pkg *pkg,
                                         tn_array *skipl);


int pkgdb_match_req(struct pkgdb *pp, const struct capreq *req, int strict,
                      tn_array *skipl);



tn_array *pkgdb_get_packages_bynvr(const struct pkgdb *ps);

/* pkgdb-mark.c */
int pkgdb_mark_packages(struct pkgdb *ps, const tn_array *pkgs, 
                         int withdeps, int nodeps);

void packages_mark(tn_array *pkgs, unsigned flags_on, unsigned flags_off);
#define packages_unmark_all(pkgs) packages_mark(pkgs, 0, PKG_INDIRMARK | PKG_DIRMARK)

int pkgdb_rpmprovides(const struct pkgdb *ps, const struct capreq *req);

#endif /* POLDEK_PKGDB__H */

    
    
