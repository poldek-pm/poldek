#ifndef POLDEK_INTERNAL_H
#define POLDEK_INTERNAL_H

#include <trurl/narray.h>
#include <trurl/nhash.h>


// pkgdir.h structures
struct source;
struct pkgdir;

// pkgset.h structures
struct pkgset;
struct pm_ctx;
struct poldek_ctx;

struct poldek_ctx {
    tn_hash        *htconf;
    tn_array       *sources;    /* struct source *[]  */
    tn_array       *pkgdirs;    /* struct pkgdir *[]  */

    struct poldek_ts *ts;       /* main, internal ts */

    unsigned         ps_flags;
    unsigned         ps_setup_flags;
    struct pkgset    *ps;
    struct pm_ctx    *pmctx;

//    tn_array       *inst_pkgs;  /* array of installed packages  */
//    time_t         ts_instpkgs; /* inst_pkgs timestamp */
    
//    struct pkgdir  *dbpkgdir;   /* db packages        */
    tn_hash        *_cnf;       /* runtime config */
    unsigned       _iflags;     /* internal flags */
    int            _refcnt;
};

int poldek__is_setup_done(struct poldek_ctx *ctx);
void poldek__apply_tsconfig(struct poldek_ctx *ctx, struct poldek_ts *ts);

struct pkgdb;
struct pkgdb *poldek_ts_dbopen(struct poldek_ts *ts, mode_t mode);

void poldek_ts_xsetop(struct poldek_ts *ts, int optv, int on, int touch);

#endif
