/* $Id$ */
#ifndef  POLDEK_PKGSET_H
#define  POLDEK_PKGSET_H

#include <obstack.h>
#include <trurl/narray.h>


#include "pkg.h"
#include "pkgdir.h"
#include "pkgdb.h"
#include "usrset.h"

#include "fileindex.h"
#include "capreqidx.h"


int pkgsetmodule_init(void);
void pkgsetmodule_destroy(void);


#define _PKGSET_INDEXES_INIT      (1 << 16) /* internal flag  */

struct pkgset {
    tn_array           *pkgs;           /*  pkg* []    */
    tn_array           *ordered_pkgs;   /*  pkg* []    */
    unsigned           flags;         

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
int pkgset_order(struct pkgset *ps);


#define INSTS_JUSTFETCH    (1 << 0)
#define INSTS_JUSTPRINT    (1 << 1)
#define INSTS_JUSTPRINT_N  (1 << 2) /* names, not filenames */
#define INSTS_MKDBDIR      (1 << 3)
#define INSTS_FOLLOW       (1 << 4)
#define INSTS_FRESHEN      (1 << 5)
#define INSTS_USESUDO      (1 << 6)

struct inst_s {
    struct pkgdb   *db;
    unsigned       flags;          /* INSTS_* */
    unsigned       instflags;      /* PKGINST_* from pkgdb.h */
    const char     *rootdir;       /* top level dir          */
    const char     *fetchdir;      /* dir to fetch files     */
    const char     *cachedir;      /* place for downloaded packages */
    const char     *dumpfile;      /* file to dump fqpns     */
    tn_array       *rpmopts;       /* rpm cmdline opts (char *opts[]) */
    tn_array       *rpmacros;      /* rpm macros to pass to cmdline (char *opts[]) */
    tn_array       *hold_pkgnames; 
    
    int  (*selpkg_fn)(const char *, const tn_array *);
    int  (*ask_fn)(const char *, ...);
    void (*inf_fn)(const char *, ...);
};

void inst_s_init(struct inst_s *inst);


/* if set then:
 * - requirements matched even if requirement has version
 *   while capability hasn't (RPM style)
 * - files with diffrent mode only not assumed as conflicts
 */
#define PSVERIFY_MERCY       (1 << 0)

#define PSMODE_VERIFY        (1 << 1)
#define PSMODE_MKIDX         (1 << 2)
#define PSMODE_INSTALL       (1 << 3)
#define PSMODE_INSTALL_DIST  (1 << 4)

#define PSMODE_UPGRADE       (1 << 5)
#define PSMODE_UPGRADE_DIST  (1 << 6)

struct pkgset *pkgset_new(unsigned psoptflags);
void pkgset_free(struct pkgset *ps);

int pkgset_setup(struct pkgset *ps);

/* returns sorted list of packages, free it by n_array_free() */
tn_array *pkgset_getpkgs(const struct pkgset *ps);
tn_array *pkgset_lookup_cap(struct pkgset *ps, const char *capname);


#define MARK_USET    0          /* mark only given set */
#define MARK_DEPS    1          /* follow dependencies */
int pkgset_mark_usrset(struct pkgset *ps, struct usrpkgset *ups,
                       struct inst_s *inst, int markflag);


#define PS_MARK_UNMARK_ALL  (1 << 0)
#define PS_MARK_UNMARK_DEPS (1 << 1)
void pkgset_unmark(struct pkgset *ps, unsigned markflags);

int pkgset_fetch_pkgs(const char *destdir, tn_array *pkgs, int nosubdirs);

int pkgset_install_dist(struct pkgset *ps, struct inst_s *inst);
int pkgset_upgrade_dist(struct pkgset *ps, struct inst_s *inst);

int pkgset_install(struct pkgset *ps, struct inst_s *inst,
                   tn_array *unistalled_pkgs);

void pkgset_mark_holds(struct pkgset *ps, tn_array *hold_pkgnames);


#include "pkgset-load.h"


#endif /* POLDEK_PKGSET_H */
