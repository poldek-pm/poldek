/* $Id$ */
#ifndef  POLDEK_LIB_H
#define  POLDEK_LIB_H

//#include "conf.h"
#include "pkg.h"


// pkgdir.h structures
struct source;
struct pkgdir;

// pkgset.h structures
struct pkgset;
struct inst_s;

extern const char poldek_BUG_MAILADDR[];
extern const char poldek_VERSION_BANNER[];
extern const char poldek_BANNER[];

struct poldek_ctx {
    tn_array       *sources;    /* struct source *[]  */
    tn_array       *pkgdirs;    /* struct pkgdir *[]  */
    
    struct pkgset  *ps;
    struct inst_s  *inst;
    unsigned       inst_flags_orig;
    
    tn_array       *pkgs;       /* array of available packages  */
    tn_array       *inst_pkgs;  /* array of installed packages  */
    time_t         ts_instpkgs; /* inst_pkgs timestamp */
    
    struct pkgdir  *dbpkgdir;   /* db packages        */
    tn_hash        *cnf;
    unsigned       _iflags;
};

int pdek_resolve_packages(tn_array *patterns, tn_array *avpkgs,
                          tn_array *pkgs, int strict);

int poldek_init(struct poldek_ctx *ctx, unsigned flags);
void poldek_destroy(struct poldek_ctx *ctx);


#define POLDEK_CONF_FLAGS           0
#define POLDEK_CONF_PSFLAGS         1
#define POLDEK_CONF_PS_SETUP_FLAGS  2
#define POLDEK_CONF_CACHEDIR        3 
#define POLDEK_CONF_FETCHDIR        4
#define POLDEK_CONF_ROOTDIR         5 
#define POLDEK_CONF_DUMPFILE        6
#define POLDEK_CONF_PRIFILE         7
#define POLDEK_CONF_SOURCE          8
#define POLDEK_CONF_RPMMACROS       9
#define POLDEK_CONF_HOLD            10
#define POLDEK_CONF_IGNORE          11

#define POLDEK_CONF_LOGFILE         12
#define POLDEK_CONF_LOGTTY          13

int poldek_configure(struct poldek_ctx *ctx, int param, ...);

#define poldek_configure_f_reset(ctx) poldek_configure(ctx, POLDEK_CONF_FLAGS, 0)
#define poldek_configure_f(ctx, val) \
        poldek_configure(ctx, POLDEK_CONF_FLAGS, val)
#define poldek_configure_f_isset(ctx, fflags) ((ctx)->inst->flags & (fflags))
#define poldek_configure_f_clr(ctx, fflags) ((ctx)->inst->flags &= ~(fflags))

int poldek_load_config(struct poldek_ctx *ctx, const char *path);

int poldek_setup(struct poldek_ctx *ctx);



int poldek_load_sources(struct poldek_ctx *ctx, int load_dbdepdirs);

tn_array *poldek_av_packages(void);

int poldek_mark_usrset(struct poldek_ctx *ctx, struct usrpkgset *ups,
                       int withdeps);

int poldek_install_dist(struct poldek_ctx *ctx);
int poldek_upgrade_dist(struct poldek_ctx *ctx);

/*
struct install_info {
    tn_array *installed_pkgs;
    tn_array *uninstalled_pkgs;
};
*/

int poldek_install(struct poldek_ctx *ctx, struct install_info *iinf);

#endif 
