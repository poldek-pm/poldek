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

    tn_array       *dest_sources; /* for "pset" PM, struct source *[]  */

    struct poldek_ts *ts;       /* main, internal ts */

    unsigned         ps_flags;
    unsigned         ps_setup_flags;
    struct pkgset    *ps;
    struct pm_ctx    *pmctx;
    int              _rpm_tscolor; /* rpm transaction color */
    int              _depengine;
    
    tn_hash        *_cnf;       /* runtime config */
    unsigned       _iflags;     /* internal flags */
    int            _refcnt;
};

int poldek__is_setup_done(struct poldek_ctx *ctx);
void poldek__ts_postconf(struct poldek_ctx *ctx, struct poldek_ts *ts);

struct pkgdb;
struct pkgdb *poldek_ts_dbopen(struct poldek_ts *ts, mode_t mode);

void poldek_ts_xsetop(struct poldek_ts *ts, int optv, int on, int touch);

void poldek__ts_dump_settings(struct poldek_ctx *ctx, struct poldek_ts *ts);

#endif
