/* $Id$ */
#ifndef  POLDEK_LIB_TX_H
#define  POLDEK_LIB_TX_H

#include <trurl/narray.h>
#include <trurl/nhash.h>

enum poldek_ts_flag {
    POLDEK_TS_INSTALL      = (1 << 0), 
    POLDEK_TS_UNINSTALL    = (1 << 1), 
    POLDEK_TS_VERIFY       = (1 << 2),
    
    POLDEK_TS_DIST         = (1 << 5), 
    POLDEK_TS_UPGRADE      = (1 << 6), 
    POLDEK_TS_DOWNGRADE    = (1 << 7),
    POLDEK_TS_REINSTALL    = (1 << 8),

    POLDEK_TS_UPGRADEDIST  = POLDEK_TS_UPGRADE | POLDEK_TS_DIST,
    POLDEK_TS_INSTALLDIST  = POLDEK_TS_DIST,

    POLDEK_TS_TRACK        = (1 << 10) /* track changes made by ts
                                          (pkgs_{installed, removed})
                                       */
};

enum poldek_ts_type {
    POLDEK_TS_TYPE_INSTALL      = POLDEK_TS_INSTALL,
    POLDEK_TS_TYPE_UNINSTALL    = POLDEK_TS_UNINSTALL,
    POLDEK_TS_TYPE_VERIFY       = POLDEK_TS_VERIFY,
};

enum poldek_ts_opt {
    POLDEK_OP_NULL = 0,

    POLDEK_OP_UNIQN,             /* --uniqn */
    POLDEK_OP_VRFY_DEPS,         /* -V */
    POLDEK_OP_VRFY_ORDER,        /* --verify=order */
    POLDEK_OP_VRFY_CNFLS,        /* --verify=conflicts */
    POLDEK_OP_VRFY_FILECNFLS,    /* --verify=file-conflicts */
    POLDEK_OP_VRFY_FILEORPHANS,  /* --verify=file-orphans */
    POLDEK_OP_VRFY_FILEMISSDEPS, /* --verify=file-missing-deps */
    POLDEK_OP_DEPGRAPH,          /* --dependency-graph */

    POLDEK_OP_LDALLDESC,         /* internal, load all i18n descriptions */
    POLDEK_OP_LDFULLFILELIST,    /* internal, load whole file database */
    
    POLDEK_OP_VRFYMERCY,   /* --mercy */
    POLDEK_OP_PROMOTEPOCH, /* --promoteepoch */
    
    POLDEK_OP_FOLLOW,      /* !--nofollow */
    POLDEK_OP_FRESHEN,     /* --freshen */
    POLDEK_OP_GREEDY,   /* --greedy */
    POLDEK_OP_CONFLICTS,  /* honour conflicts */
    POLDEK_OP_OBSOLETES,  /* honour obsoletes */
    POLDEK_OP_SUGGESTS,   /* honour suggests */
    POLDEK_OP_AGGREEDY,
    POLDEK_OP_ALLOWDUPS, 
    POLDEK_OP_NODEPS,  /* rpm --nodeps */
    POLDEK_OP_AUTODIRDEP, /* auto directory deps from rpm 4.4.6 */
    
    POLDEK_OP_CAPLOOKUP,
    POLDEK_OP_MULTILIB,
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
    POLDEK_OP_IS_INTERACTIVE_ON,  /* any of above */

    POLDEK_OP_NOFETCH,        /* usable for debugging */
    POLDEK_OP_PARSABLETS,     /* print transaction (install/remove) summary in
                                 parseable form */
    POLDEK_OP___MAXOP,
};

struct poldek_ctx;
struct pkgdb;
struct pm_ctx;
struct source;
struct arg_packages;
struct pkgmark_set;
struct pkg;

#ifndef SWIG
struct poldek_ts {
    int                type;
    char               *typenam;
    struct poldek_ctx  *ctx;
    struct pkgdb       *db;
    struct pm_ctx      *pmctx;
    struct source      *pm_pdirsrc; /* for 'pset' PM, XXX unused, to rethink */
    tn_array           *pkgs;
    
    struct arg_packages  *aps;
    struct pkgmark_set   *pms;
    char               *rpm_bin;       /* /usr/bin/rpm   */ 
    char               *sudo_bin;      /* /usr/bin/sudo  */
    char               *rootdir;       /* top level dir          */
    char               *fetchdir;      /* dir to fetch files to  */
    char               *cachedir;      /* cache directory        */
    char               *dumpfile;      /* file to dump fqpns     */
    char               *prifile;       /* file with package priorities (split*) */
    char               *depgraph    ;  /* graph type[:path] graphviz and others graphs */
    tn_array           *rpmopts;       /* rpm cmdline opts (char *opts[]) */
    tn_array           *rpmacros;      /* rpm macros to pass to cmdline (char *opts[]) */
    tn_array           *hold_patterns;
    tn_array           *ign_patterns; 
    tn_array           *exclude_path;

    tn_hash            *ts_summary;     /* There are to I|R|D                */
    tn_array           *pkgs_installed; /* packages installed by transaction */
    tn_array           *pkgs_removed;   /* packages removed by transaction   */
    
    tn_alloc           *_na;
    uint32_t           _flags;      /* POLDEK_TS_* */
    uint32_t           _iflags;     /* internal flags */
    uint32_t           _opvect[4];  /* options POLDEK_OP* */
    uint32_t           _opvect_touched[4];
    
    int   (*getop)(const struct poldek_ts *, int op);
    int   (*getop_v)(const struct poldek_ts *, int op, ...);
    void  (*setop)(struct poldek_ts *, int op, int onoff);

    int   uninstall_greedy_deep; /* greediness of uninstall, is set
                                    by ts->setop(POLDEK_OP_GREEDY, v)
                                  */

};
#endif
struct poldek_ts *poldek_ts_new(struct poldek_ctx *ctx, unsigned flags);
void poldek_ts_free(struct poldek_ts *ts);

int poldek_ts_get_type(struct poldek_ts *ts);
int poldek_ts_set_type(struct poldek_ts *ts, enum poldek_ts_type type,
                       const char *typenam);

void poldek_ts_setf(struct poldek_ts *ts, uint32_t flag);
void poldek_ts_clrf(struct poldek_ts *ts, uint32_t flag);
uint32_t poldek_ts_issetf(struct poldek_ts *ts, uint32_t flag);
int poldek_ts_issetf_all(struct poldek_ts *ts, uint32_t flag);

void poldek_ts_setop(struct poldek_ts *ts, int optv, int on);
int poldek_ts_getop(const struct poldek_ts *ts, int optv);
int poldek_ts_op_touched(const struct poldek_ts *ts, int optv);
int poldek_ts_is_interactive_on(const struct poldek_ts *ts);

#include <stdarg.h>
#ifndef SWIG
int poldek_ts_vconfigure(struct poldek_ts *ts, int param, va_list ap);
#endif
int poldek_ts_configure(struct poldek_ts *ts, int param, ...);


/* add package arguments */
int poldek_ts_add_pkg(struct poldek_ts *ts, struct pkg *pkg);
int poldek_ts_add_pkgmask(struct poldek_ts *ts, const char *mask);
int poldek_ts_add_pkgfile(struct poldek_ts *ts, const char *pathname);
int poldek_ts_add_pkglist(struct poldek_ts *ts, const char *path);

void poldek_ts_clean_args(struct poldek_ts *ts);
tn_array* poldek_ts_get_args_asmasks(struct poldek_ts *ts, int hashed);
int poldek_ts_get_arg_count(struct poldek_ts *ts);

int poldek_ts_run(struct poldek_ts *ts, unsigned flags);

/* mark = {I|D|R} */
tn_array *poldek_ts_get_summary(const struct poldek_ts *ts, const char *mark);

#endif
