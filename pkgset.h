/* $Id$ */
#ifndef  POLDEK_PKGSET_H
#define  POLDEK_PKGSET_H

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
#define INSTS_NOIGNORE        (1 << 20)  /* --noignore  */
#define INSTS_GREEDY          (1 << 21)  /* --greedy */
#define INSTS_OBSOLETES       (1 << 21)  /* the same */
#define INSTS_KEEP_DOWNLOADS  (1 << 25) /* keep_downloads = yes */
#define INSTS_PARTICLE        (1 << 26) /* particle_install = yes */
#define INSTS_CHECKSIG        (1 << 27) /* not implemented yet */
#define INSTS_CONFIRM_INST    (1 << 28) /* confirm_installation = yes  */
#define INSTS_CONFIRM_UNINST  (1 << 29) /* confirm_removal = yes  */
#define INSTS_EQPKG_ASKUSER   (1 << 30) /* choose_equivalents_manually = yes */

#define INSTS_INTERACTIVE_ON  (INSTS_CONFIRM_INST | INSTS_EQPKG_ASKUSER | INSTS_CONFIRM_UNINST)

struct inst_s {
    struct pkgdb   *db;
    unsigned       flags;          /* INSTS_* */
    
    unsigned       ps_flags;
    unsigned       ps_setup_flags;
    
    char           *rootdir;       /* top level dir          */
    char           *fetchdir;      /* dir to fetch files to  */
    char           *cachedir;      /* cache directory        */
    char           *dumpfile;      /* file to dump fqpns     */
    char           *prifile;       /* file with package priorities (split*) */
    tn_array       *rpmopts;       /* rpm cmdline opts (char *opts[]) */
    tn_array       *rpmacros;      /* rpm macros to pass to cmdline (char *opts[]) */
    tn_array       *hold_patterns;
    tn_array       *ign_patterns; 
    
    int  (*askpkg_fn)(const char *, struct pkg **pkgs, struct pkg *deflt);
    int  (*ask_fn)(int default_a, const char *, ...);
};

void inst_s_init(struct inst_s *inst);
#define inst_flags_set(inst, flags) ((inst)->flags & flags)

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
                       unsigned inst_s_flags, int withdeps);

void packages_mark(tn_array *pkgs, unsigned flags_on, unsigned flags_off);
#define packages_unmark_all(pkgs) packages_mark(pkgs, 0, PKG_INDIRMARK | PKG_DIRMARK)

struct install_info {
    tn_array *installed_pkgs;
    tn_array *uninstalled_pkgs;
};

void install_info_init(struct install_info *iinf);
void install_info_destroy(struct install_info *iinf);

/* uninstall.c */
int uninstall_usrset(struct usrpkgset *ups, struct inst_s *inst,
                     struct install_info *iinf);


int pkgset_install_dist(struct pkgset *ps, struct inst_s *inst);

/* pkgset-install.c */
int pkgset_upgrade_dist(struct pkgset *ps, struct inst_s *inst);

int pkgset_install(struct pkgset *ps, struct inst_s *inst,
                   struct install_info *iinf);


int pkgset_rpmprovides(const struct pkgset *ps, const struct capreq *req);


struct pkgscore_s {
    char        pkgbuf[512];
    int         pkgname_off;
    struct pkg  *pkg;
};

void pkgscore_match_init(struct pkgscore_s *psc, struct pkg *pkg);
int pkgscore_match(struct pkgscore_s *psc, const char *mask);
void packages_score(tn_array *pkgs, tn_array *patterns, unsigned scoreflag);

/* flags is INSTS_JUSTPRINT[_N] */
int packages_dump(tn_array *pkgs, const char *path, unsigned flags);



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
