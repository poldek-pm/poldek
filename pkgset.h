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

#define _PKGSET_INDEXES_INIT      (1 << 20) /* internal flag  */

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

#define INSTS_INSTALL         (1 << 0) /* rpm -i */
#define INSTS_UPGRADE         (1 << 1) /* rpm -U */
#define INSTS_DOWNGRADE       (1 << 3) /* rpm -U --oldpackage */
#define INSTS_REINSTALL       (1 << 4) /* rpm -U --replacefiles --replacepkgs */
#define INSTS_NODEPS          (1 << 5) /* rpm --nodeps */ 
#define INSTS_FORCE           (1 << 6) /* rpm --force  */
#define INSTS_TEST            (1 << 7) /* poldek test mode, not rpm one */
#define INSTS_RPMTEST         (1 << 8) /* rpm --test */
#define INSTS_JUSTDB          (1 << 9) /* rpm --justdb */
#define INSTS_JUSTFETCH       (1 << 10)
#define INSTS_JUSTPRINT       (1 << 11)
#define INSTS_JUSTPRINT_N     (1 << 12) /* names, not filenames */

#define INSTS_JUSTPRINTS      (INSTS_JUSTPRINT | INSTS_JUSTPRINT_N)


#define INSTS_MKDBDIR         (1 << 15)  /* --mkdir */
#define INSTS_FOLLOW          (1 << 16)  /* !--nofollow */
#define INSTS_FRESHEN         (1 << 17)  /* --freshen */
#define INSTS_USESUDO         (1 << 18)  /* use_sudo = yes  */
#define INSTS_NOHOLD          (1 << 19)  /* --nohold  */
#define INSTS_GREEDY          (1 << 20) /* --greedy */
#define INSTS_KEEP_DOWNLOADS  (1 << 21) /* keep_downloads = yes */
#define INSTS_PARTICLE        (1 << 22) /* particle_install = yes */
#define INSTS_CHECKSIG        (1 << 23) /* not implemented yet */
#define INSTS_CONFIRM_INST    (1 << 24) /* confirm_installation = yes  */
#define INSTS_CONFIRM_UNINST  (1 << 25) /* confirm_removal = yes  */
#define INSTS_EQPKG_ASKUSER   (1 << 26) /* choose_equivalents_manually = yes */

#define INSTS_INTERACTIVE_ON  (INSTS_CONFIRM_INST | INSTS_EQPKG_ASKUSER)

struct inst_s {
    struct pkgdb   *db;
    unsigned       flags;          /* INSTS_* */
    const char     *rootdir;       /* top level dir          */
    const char     *fetchdir;      /* dir to fetch files to  */
    const char     *cachedir;      /* cache directory        */
    const char     *dumpfile;      /* file to dump fqpns     */
    tn_array       *rpmopts;       /* rpm cmdline opts (char *opts[]) */
    tn_array       *rpmacros;      /* rpm macros to pass to cmdline (char *opts[]) */
    tn_array       *hold_pkgnames; 
    
    int  (*askpkg_fn)(const char *, struct pkg **pkgs, struct pkg *deflt);
    int  (*ask_fn)(int default_a, const char *, ...);
};

void inst_s_init(struct inst_s *inst);
#define inst_iflags_set(inst, flags) ((inst)->instflags & flags)
#define inst_flags_set(inst, flags) ((inst)->flags & flags)

/* if set then:
 * - requirements matched even if requirement has version
 *   while capability hasn't (RPM style)
 * - files with diffrent mode only not assumed as conflicts
 */
#define PSVERIFY_MERCY        (1 << 0)

#define PSVERIFY_DEPS         (1 << 1)
#define PSVERIFY_CNFLS        (1 << 2)
#define PSVERIFY_FILECNFLS    (1 << 3)

#define PSDBDIRS_LOADED       (1 << 4)

#define PSMODE_VERIFY        (1 << 11)
#define PSMODE_MKIDX         (1 << 12)
#define PSMODE_INSTALL       (1 << 13)
#define PSMODE_INSTALL_DIST  (1 << 14)

#define PSMODE_UPGRADE       (1 << 15)
#define PSMODE_UPGRADE_DIST  (1 << 16)

struct pkgset *pkgset_new(unsigned psoptflags);
void pkgset_free(struct pkgset *ps);

int pkgset_setup(struct pkgset *ps, const char *pri_fpath);

/* returns sorted list of packages, free it by n_array_free() */
tn_array *pkgset_getpkgs(const struct pkgset *ps);


tn_array *pkgset_lookup_cap(struct pkgset *ps, const char *capname);

struct install_info {
    tn_array *installed_pkgs;
    tn_array *uninstalled_pkgs;
};

#define MARK_USET    0          /* mark only given set */
#define MARK_DEPS    1          /* follow dependencies */
int pkgset_mark_usrset(struct pkgset *ps, struct usrpkgset *ups,
                       struct inst_s *inst, int markflag);

int pkg_match_pkgdef(const struct pkg *pkg, const struct pkgdef *pdef);

/* uninstall.c */
int uninstall_usrset(struct usrpkgset *ups, struct inst_s *inst,
                     struct install_info *iinf);

#define PS_MARK_OFF_ALL      (1 << 0)
#define PS_MARK_OFF_DEPS     (1 << 1)
#define PS_MARK_ON_INTERNAL  (1 << 2) /* use with one of above PS_MARK_* */

void pkgset_mark(struct pkgset *ps, unsigned markflags);

int pkgset_fetch_pkgs(const char *destdir, tn_array *pkgs, int nosubdirs);


int pkgset_install_dist(struct pkgset *ps, struct inst_s *inst);

/* pkgset-install.c */
int pkgset_upgrade_dist(struct pkgset *ps, struct inst_s *inst);

int pkgset_install(struct pkgset *ps, struct inst_s *inst,
                   struct install_info *iinf);

void pkgset_mark_holds(struct pkgset *ps, tn_array *hold_pkgnames);
tn_array *read_holds(const char *fpath, tn_array *hold_pkgnames);

int pkgset_dump_marked_pkgs(struct pkgset *ps, const char *dumpfile, int bn);


int packages_fetch(tn_array *pkgs, const char *destdir, int nosubdirs);
int packages_rpminstall(tn_array *pkgs, struct pkgset *ps, struct inst_s *inst);

int packages_uninstall(tn_array *pkgs, struct inst_s *inst, struct install_info *iinf);

/* returns /bin/rpm exit code */
int rpmr_exec(const char *cmd, char *const argv[], int ontty, int verbose_level);


#define PKGVERIFY_MD   (1 << 0)
#define PKGVERIFY_GPG  (1 << 1)
#define PKGVERIFY_PGP  (1 << 2)

int package_verify_sign(const char *path, unsigned flags);

/* looks if pkg->pkgdir has set VERSIGN flag */
int package_verify_pgpg_sign(const struct pkg *pkg, const char *localpath);

#include "pkgset-load.h"

#endif /* POLDEK_PKGSET_H */
