/* $Id$ */
#ifndef  POLDEK_PKGSET_H
#define  POLDEK_PKGSET_H

#include "pkg.h"
#include "pkgdb.h"
#include "usrset.h"

int pkgsetmodule_init(void);
void pkgsetmodule_destroy(void);

struct pkgset;

#define INSTS_JUSTFETCH    (1 << 0)
#define INSTS_JUSTPRINT    (1 << 1)
#define INSTS_MKDBDIR      (1 << 2)

struct inst_s {
    struct pkgdb   *db;
    unsigned       flags;
    unsigned       instflags;   /* PKGINST_* from pkgdb.h */
    const char     *rootdir;    /* top level dir          */
    const char     *fetchdir;   /* dir to fetch files     */
    const char     *dumpfile;   /* file to dump fqpns     */
    tn_array       *rpmopts;
    tn_array       *rpmacros;
    
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
#define PSVERIFY_MERCY  (1 << 0)

#define PSMODE_VERIFY   (1 << 1)
#define PSMODE_MKIDX    (1 << 2)
#define PSMODE_INSTALL  (1 << 3)
#define PSMODE_UPGRADE  (1 << 4)

struct pkgset *pkgset_new(unsigned psoptflags);
void pkgset_free(struct pkgset *ps);

/* ldmethod  */
#define PKGSET_LD_DIR      1    /* scan directory          */
#define PKGSET_LD_HDRFILE  2    /* read rpmhdr (toc)file   */
#define PKGSET_LD_TXTFILE  3    /* read Packages file      */

int pkgset_load(struct pkgset *ps, int ldmethod, void *path,
                const char *prefix);

int pkgset_setup(struct pkgset *ps);

int pkgset_create_txtidx(struct pkgset *ps, const char *pathname);
int pkgset_create_rpmidx(const char *dirpath, const char *pathname);

/* returns sorted list of packages, free it by n_array_free() */
tn_array *pkgset_getpkgs(const struct pkgset *ps);
tn_array *pkgset_lookup_cap(struct pkgset *ps, const char *capname);


#define MARK_USET    0
#define MARK_DEPS    1  
int pkgset_mark_usrset(struct pkgset *ps, struct usrpkgset *ups,
                       struct inst_s *inst, int markflag);


#define PS_MARK_UNMARK_ALL  (1 << 0)
#define PS_MARK_UNMARK_DEPS (1 << 1)
void pkgset_unmark(struct pkgset *ps, unsigned markflags);


int pkgset_install_dist(struct pkgset *ps, struct inst_s *inst);
int pkgset_upgrade_dist(struct pkgset *ps, struct inst_s *inst);
int pkgset_install(struct pkgset *ps, struct inst_s *inst);


int pkgset_update_txtidx(const char *pathname);


#endif /* POLDEK_PKGSET_H */
