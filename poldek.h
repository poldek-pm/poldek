/* $Id$ */
#ifndef  POLDEK_LIB_H
#define  POLDEK_LIB_H

#include "pkg.h"
#include "poldek_ts.h"
#include "pkgdir/pkgdir.h"

/* constans  */
extern const char poldek_BUG_MAILADDR[];
extern const char poldek_VERSION_BANNER[];
extern const char poldek_BANNER[];

struct poldek_ctx;

struct poldek_ctx *poldek_new(unsigned flags);
void poldek_free(struct poldek_ctx *ctx);

#define POLDEK_CONF_OPT             0
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
#define POLDEK_CONF_PM              13

#define POLDEK_CONF_LOGFILE         20
#define POLDEK_CONF_LOGTTY          21

int poldek_configure(struct poldek_ctx *ctx, int param, ...);
int poldek_load_config(struct poldek_ctx *ctx, const char *path);

int poldek_setup_cachedir(struct poldek_ctx *ctx);
int poldek_setup(struct poldek_ctx *ctx);

int poldek_load_sources(struct poldek_ctx *ctx);

int poldek_is_interactive_on(const struct poldek_ctx *ctx);
tn_array *poldek_get_sources(struct poldek_ctx *ctx);
tn_array *poldek_get_pkgdirs(struct poldek_ctx *ctx);


enum poldek_search_tag {
    POLDEK_ST_RECNO = 1,
    POLDEK_ST_NAME  = 2,
    POLDEK_ST_CAP   = 3,        /* what provides cap */
    POLDEK_ST_REQ   = 4,        /* what requires */
    POLDEK_ST_CNFL  = 5,        
    POLDEK_ST_OBSL  = 6,
    POLDEK_ST_FILE  = 7,
    POLDEK_ST_PROVIDES = 8,     /* what provides cap or file */
};

tn_array *poldek_search_avail_packages(struct poldek_ctx *ctx,
                                       enum poldek_search_tag tag,
                                       const char *value);

tn_array *poldek_get_avail_packages(struct poldek_ctx *ctx);

int poldek_split(const struct poldek_ctx *ctx, unsigned size,
                 unsigned first_free_space, const char *outprefix);

#endif 
