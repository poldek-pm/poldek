#ifndef POLDEKCLI_OPTIONS_H
#define POLDEKCLI_OPTIONS_H

#include <argp.h>               /* for struct argp* */

#include "poldek_ts.h"

#define POCLIDEK_MNR_MODE (1 << 0)
#define POCLIDEK_MJR_MODE (1 << 0)

struct poclidek_opgroup_rt {
    struct poldek_ctx     *ctx;
    struct poldek_ts      *ts;
    unsigned              flags;
    void                  *_opdata;
    void                  (*_opdata_free)(void*);
    int                   (*run)(struct poclidek_opgroup_rt *);
};

#define OPGROUP_RC_NIL    0
#define OPGROUP_RC_OK     (1 << 0)
#define OPGROUP_RC_ERROR  (1 << 1)

#define OPGROUP_RC_IFINI  ((1 << 2) | OPGROUP_RC_OK)
#define OPGROUP_RC_FINI   ((1 << 3) | OPGROUP_RC_OK)

struct poclidek_opgroup {
    const char          *doc;
    struct argp         *argp;
    struct argp_child   *argp_child;
    int                 (*run)(struct poclidek_opgroup_rt *);
};

extern struct poclidek_opgroup poclidek_opgroup_source;
extern struct poclidek_opgroup poclidek_opgroup_install;
extern struct poclidek_opgroup poclidek_opgroup_packages;
extern struct poclidek_opgroup poclidek_opgroup_uninstall;
extern struct poclidek_opgroup poclidek_opgroup_makeidx;
extern struct poclidek_opgroup poclidek_opgroup_split;
extern struct poclidek_opgroup poclidek_opgroup_verify;

struct poclidek_opgroup_rt *poclidek_opgroup_rt_new(struct poldek_ts *ts);
void poclidek_opgroup_rt_free(struct poclidek_opgroup_rt *rt);


#endif
