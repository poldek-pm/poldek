/* $Id$ */
#ifndef  POLDEK_LIB_TX_H
#define  POLDEK_LIB_TX_H

#include <trurl/narray.h>
#include <trurl/nhash.h>


#define POLDEK_TS_INSTALL     (1 << 0) /* rpm -i */
#define POLDEK_TS_UNINSTALL   (1 << 1) /* rpm -e */

#define POLDEK_TS_UPGRADE     (1 << 2) /* rpm -U */
#define POLDEK_TS_DOWNGRADE   (1 << 3) /* rpm -U --oldpackage */
#define POLDEK_TS_REINSTALL   (1 << 4) /* rpm -U --replacefiles --replacepkgs */
#define POLDEK_TS_DIST        (1 << 5) /* */
#define POLDEK_TS_INSTALLDIST (1 << 6)

enum poldek_tsopt {
    POLDEK_OP_NULL = 0,
    POLDEK_OP_PS_UNIQN,     /* --uniqn */
    POLDEK_OP_VRFYMERCY,   /* --mercy */
    POLDEK_OP_PROMOTEPOCH, /* --promoteepoch */
    POLDEK_OP_FOLLOW,      /* !--nofollow */
    POLDEK_OP_FRESHEN,     /* --freshen */
    POLDEK_OP_GREEDY = 10,   /* --greedy */
    POLDEK_OP_OBSOLETES = POLDEK_OP_GREEDY,  /* the same */
    POLDEK_OP_AGGREEDY,
    POLDEK_OP_ALLOWDUPS, 
    POLDEK_OP_NODEPS,  /* rpm --nodeps */
    POLDEK_OP_FORCE,  /* rpm --force  */
    POLDEK_OP_IGNOREARCH,  /* rpm --ignorearch */
    POLDEK_OP_IGNOREOS,    /* rpm --ignoreos   */
    
    POLDEK_OP_TEST,        /* poldek test mode, not rpm one */
    POLDEK_OP_RPMTEST,    /* rpm --test */
    POLDEK_OP_JUSTDB,      /* rpm --justdb */
    POLDEK_OP_JUSTFETCH,  
    POLDEK_OP_JUSTPRINT,  
    POLDEK_OP_JUSTPRINT_N,  /* names, not filenames */
    POLDEK_OP_MKDBDIR,      /* --mkdir */
    POLDEK_OP_USESUDO,      /* use_sudo = yes  */
    POLDEK_OP_HOLD,         /* --nohold  */
    POLDEK_OP_IGNORE,       /* --noignore  */
    POLDEK_OP_PARTICLE,     /* particle_install = yes */
    
    POLDEK_OP_KEEP_DOWNLOADS,  /* keep_downloads = yes */
    POLDEK_OP_CHECKSIG,        /* not implemented yet */
    
    POLDEK_OP_CONFIRM_INST,    /* confirm_installation = yes  */
    POLDEK_OP_CONFIRM_UNINST,  /* confirm_removal = yes  */
    POLDEK_OP_EQPKG_ASKUSER,   /* choose_equivalents_manually = yes */
    POLDEK_OP_IS_INTERACTIVE_ON /* any of above */
};



struct poldek_ctx;
struct arg_packages;

struct poldek_ts {
    struct poldek_ctx  *ctx;
    struct pkgdb       *db;
    uint32_t           flags;      /* POLDEK_TS_* */
    tn_array  *pkgs;
    
    struct arg_packages  *aps;

    char               *rpm_bin;       /* /usr/bin/rpm   */ 
    char               *sudo_bin;      /* /usr/bin/sudo  */
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

    
    uint32_t           _iflags;    /* internal flags */
    uint32_t           _opvect[4];
    int   (*getop)(const struct poldek_ts *, int op);
    int   (*getop_v)(const struct poldek_ts *, int op, ...);
    void  (*setop)(struct poldek_ts *, int op, int onoff);
    
};

struct poldek_ts *poldek_ts_new(struct poldek_ctx *ctx);
void poldek_ts_free(struct poldek_ts *ts);

int poldek_ts_init(struct poldek_ts *ts, struct poldek_ctx *ctx);
void poldek_ts_destroy(struct poldek_ts *ts);

void poldek_ts_setf(struct poldek_ts *ts, uint32_t flag);
void poldek_ts_clrf(struct poldek_ts *ts, uint32_t flag);
uint32_t poldek_ts_issetf(struct poldek_ts *ts, uint32_t flag);
#define poldek_ts_setf(ts, flag) (ts->flags |= (flag))
#define poldek_ts_clrf(ts, flag) (ts->flags &= ~(flag))
#define poldek_ts_issetf(ts, flag) (ts->flags & (flag))

int poldek_ts_is_interactive_on(const struct poldek_ts *ts);


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

int poldek_ts_run(struct poldek_ts *ts, struct install_info *iinf);

//int poldek_ts_do_install_dist(struct poldek_ts *ts);
//int poldek_ts_do_install(struct poldek_ts *ts, struct install_info *iinf);
//int poldek_ts_do_uninstall(struct poldek_ts *ts, struct install_info *iinf);

#endif
