/* $Id$ */
#ifndef  POLDEK_LIB_H
#define  POLDEK_LIB_H

#include <trurl/narray.h>
#include <trurl/nhash.h>

//#include "conf.h"
#include "pkg.h"
#include "poldek_ts.h"
#include "pkgdir/pkgdir.h"

// pkgdir.h structures
struct source;
struct pkgdir;

// pkgset.h structures
struct pkgset;

/* constans  */
extern const char poldek_BUG_MAILADDR[];
extern const char poldek_VERSION_BANNER[];
extern const char poldek_BANNER[];

struct poldek_ctx {
    tn_hash        *htconf;
    tn_array       *sources;    /* struct source *[]  */
    tn_array       *pkgdirs;    /* struct pkgdir *[]  */

    struct poldek_ts *ts;       /* main, internal ts */

    unsigned         ps_flags;
    unsigned         ps_setup_flags;
    struct pkgset    *ps;
    
    
    tn_array       *inst_pkgs;  /* array of installed packages  */
    time_t         ts_instpkgs; /* inst_pkgs timestamp */
    
    struct pkgdir  *dbpkgdir;   /* db packages        */
    tn_hash        *cnf;
    unsigned       _iflags;
};

int poldek_init(struct poldek_ctx *ctx, unsigned flags);
void poldek_destroy(struct poldek_ctx *ctx);

int poldek_load_sources(struct poldek_ctx *ctx);
tn_array *poldek_get_avpkgs(struct poldek_ctx *ctx);
tn_array *poldek_get_avpkgs_bynvr(struct poldek_ctx *ctx);

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
#define POLDEK_CONF_RPMOPTS         10
#define POLDEK_CONF_HOLD            11
#define POLDEK_CONF_IGNORE          12

#define POLDEK_CONF_LOGFILE         13
#define POLDEK_CONF_LOGTTY          14

int poldek_configure(struct poldek_ctx *ctx, int param, ...);

int poldek_load_config(struct poldek_ctx *ctx, const char *path);

/* call each setup_*() OR setup() */
int poldek_setup_cachedir(struct poldek_ctx *ctx);
int poldek_setup_sources(struct poldek_ctx *ctx);

int poldek_setup(struct poldek_ctx *ctx);


int poldek_split(const struct poldek_ctx *ctx, unsigned size,
                 unsigned first_free_space, const char *outprefix);

#endif 
