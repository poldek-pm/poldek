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
struct poldek_ts;

struct poldek_ctx {
    tn_hash        *htconf;     /* poldek configuration */
    tn_array       *sources;    /* struct source *[]  */
    tn_array       *pkgdirs;    /* struct pkgdir *[]  */

    tn_array       *dest_sources; /* for "pset" PM, struct source *[]  */

    struct poldek_ts *ts;       /* main, internal ts */

    unsigned         ps_flags;
    unsigned         ps_setup_flags;
    struct pkgset    *ps;
    struct pm_ctx    *pmctx;       /* package manager context */
    int              _rpm_tscolor; /* rpm transaction color */
    int              _depengine;

    /* callbacks, don't call them directly */
    void *data_confirm_fn;
    int  (*confirm_fn)(void *data, const struct poldek_ts *ts, int hint,
                       const char *message); /* confirm anything */

    void *data_ts_confirm_fn;
    int  (*ts_confirm_fn)(void *data, const struct poldek_ts *ts); /* confirm transaction */

    void *data_choose_equiv_fn;
    int  (*choose_equiv_fn)(void *data, const struct poldek_ts *ts,
                            const char *cap, tn_array *pkgs, int hint);

    
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

tn_array *poldek__ts_install_ordered_packages(const struct poldek_ts *ts);

void poldek__ts_update_summary(struct poldek_ts *ts,
                               const char *prefix, const tn_array *pkgs,
                               unsigned pmsflags, const struct pkgmark_set *pms);

void poldek__ts_display_summary(struct poldek_ts *ts);


/* ask.c */

int poldek__confirm(const struct poldek_ts *ts,
                    int default_answer, const char *message);

int poldek__ts_confirm(const struct poldek_ts *ts);

int poldek__choose_equiv(const struct poldek_ts *ts,
                         const char *capname, tn_array *pkgs, struct pkg *hint);

void poldek__setup_default_ask_callbacks(struct poldek_ctx *ctx);
int poldek__load_sources_internal(struct poldek_ctx *ctx);

#endif
