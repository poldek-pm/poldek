/* $Id$ */
#ifndef  POLDEK_LIB_TX_H
#define  POLDEK_LIB_TX_H

#include <trurl/narray.h>
#include <trurl/nhash.h>


#define POLDEK_TS_INSTALL         (1 << 0) /* rpm -i */
#define POLDEK_TS_UPGRADE         (1 << 1) /* rpm -U */
#define POLDEK_TS_DOWNGRADE       (1 << 3) /* rpm -U --oldpackage */
#define POLDEK_TS_REINSTALL       (1 << 4) /* rpm -U --replacefiles --replacepkgs */
#define POLDEK_TS_UNINSTALL       (1 << 5) /* rpm -e */
#define POLDEK_TS_NODEPS          (1 << 6) /* rpm --nodeps */
#define POLDEK_TS_FORCE           (1 << 7) /* rpm --force  */
#define POLDEK_TS_IGNOREARCH      (1 << 8) /* rpm --ignorearch */
#define POLDEK_TS_IGNOREOS        (1 << 9) /* rpm --ignoreos   */
#define POLDEK_TS_TEST            (1 << 10) /* poldek test mode, not rpm one */
#define POLDEK_TS_RPMTEST         (1 << 11) /* rpm --test */
#define POLDEK_TS_JUSTDB          (1 << 12) /* rpm --justdb */
#define POLDEK_TS_JUSTFETCH       (1 << 13)
#define POLDEK_TS_JUSTPRINT       (1 << 14)
#define POLDEK_TS_JUSTPRINT_N     (1 << 15) /* names, not filenames */

#define POLDEK_TS_JUSTPRINTS      (POLDEK_TS_JUSTPRINT | POLDEK_TS_JUSTPRINT_N)


#define POLDEK_TS_MKDBDIR         (1 << 16)  /* --mkdir */
#define POLDEK_TS_FOLLOW          (1 << 17)  /* !--nofollow */
#define POLDEK_TS_FRESHEN         (1 << 18)  /* --freshen */
#define POLDEK_TS_USESUDO         (1 << 19)  /* use_sudo = yes  */
#define POLDEK_TS_NOHOLD          (1 << 20)  /* --nohold  */
#define POLDEK_TS_NOIGNORE        (1 << 21)  /* --noignore  */
#define POLDEK_TS_GREEDY          (1 << 22)  /* --greedy */
#define POLDEK_TS_OBSOLETES       (1 << 23)  /* the same */
#define POLDEK_TS_KEEP_DOWNLOADS  (1 << 24) /* keep_downloads = yes */
#define POLDEK_TS_PARTICLE        (1 << 25) /* particle_install = yes */
#define POLDEK_TS_CHECKSIG        (1 << 26) /* not implemented yet */
#define POLDEK_TS_CONFIRM_INST    (1 << 27) /* confirm_installation = yes  */
#define POLDEK_TS_CONFIRM_UNINST  (1 << 28) /* confirm_removal = yes  */
#define POLDEK_TS_EQPKG_ASKUSER   (1 << 29) /* choose_equivalents_manually = yes */

#define POLDEK_TS_INTERACTIVE_ON  (POLDEK_TS_CONFIRM_INST  | \
                                   POLDEK_TS_EQPKG_ASKUSER | \
                                   POLDEK_TS_CONFIRM_UNINST)
struct poldek_ctx;
struct arg_packages;

struct poldek_ts {
    int                type;
    struct poldek_ctx  *ctx;
    struct pkgdb       *db;
    uint32_t           flags;      /* POLDEK_TS_* */

    tn_array  *pkgs;
    
    struct arg_packages  *aps;
    
    char               *rootdir;       /* top level dir          */
    char               *fetchdir;      /* dir to fetch files to  */
    char               *cachedir;      /* cache directory        */
    char               *dumpfile;      /* file to dump fqpns     */
    char               *prifile;       /* file with package priorities (split*) */
    tn_array           *rpmopts;       /* rpm cmdline opts (char *opts[]) */
    tn_array           *rpmacros;      /* rpm macros to pass to cmdline (char *opts[]) */
    tn_array           *hold_patterns;
    tn_array           *ign_patterns; 
    
    int  (*askpkg_fn)(const char *, struct pkg **pkgs, struct pkg *deflt);
    int  (*ask_fn)(int default_a, const char *, ...);
};

struct poldek_ts *poldek_ts_new(struct poldek_ctx *ctx);
void poldek_ts_free(struct poldek_ts *ts);

int poldek_ts_init(struct poldek_ts *ts, struct poldek_ctx *ctx);
void poldek_ts_destroy(struct poldek_ts *ts);

#define poldek_ts_setf(ts, flag) (ts->flags |= (flag))
#define poldek_ts_clrf(ts, flag) (ts->flags &= ~(flag))
#define poldek_ts_issetf(ts, flag) (ts->flags & (flag))

#define poldek_ts_istest(ts) (ts->flags & POLDEK_TS_TEST)
#define poldek_ts_isrpmtest(ts) (ts->flags & POLDEK_TS_RPMTEST)

#include <stdarg.h>
int poldek_ts_vconfigure(struct poldek_ts *ts, int param, va_list ap);
int poldek_ts_configure(struct poldek_ts *ts, int param, ...);

int poldek_ts_add_pkg(struct poldek_ts *ts, struct pkg *pkg);
int poldek_ts_add_pkgmask(struct poldek_ts *ts, const char *def);
int poldek_ts_add_pkgfile(struct poldek_ts *ts, const char *pathname);
int poldek_ts_add_pkglist(struct poldek_ts *ts, const char *path);

void poldek_ts_clean_arg_pkgmasks(struct poldek_ts *ts);
const tn_array* poldek_ts_get_arg_pkgmasks(struct poldek_ts *ts);
int poldek_ts_get_arg_count(struct poldek_ts *ts);

struct install_info {
    tn_array *installed_pkgs;
    tn_array *uninstalled_pkgs;
};

void install_info_init(struct install_info *iinf);
void install_info_destroy(struct install_info *iinf);


int poldek_ts_do_install_dist(struct poldek_ts *ts);
int poldek_ts_do_install(struct poldek_ts *ts, struct install_info *iinf);
int poldek_ts_do_uninstall(struct poldek_ts *ts, struct install_info *iinf);

int poldekts_do(struct poldek_ts *ts);


#endif
