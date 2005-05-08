#ifndef POLDEKCLI_OPGROUP_H
#define POLDEKCLI_OPGROUP_H

#include <argp.h>               /* for struct argp* */

struct poldek_ctx;
struct poldek_ts;

struct poclidek_op_ctx;

struct poclidek_op_ctx *poclidek_op_ctx_new(void);
void poclidek_op_ctx_free(struct poclidek_op_ctx *);
int poclidek_op_ctx_verify_major_mode(struct poclidek_op_ctx *opctx);

struct poclidek_opgroup_rt {
    struct poldek_ctx      *ctx;
    struct poldek_ts       *ts;

    struct poclidek_op_ctx *opctx;
    int (*set_major_mode)(struct poclidek_opgroup_rt *, const char *mode,
                          const char *cmd);
    
    void                   *_opdata;
    void                   (*_opdata_free)(void*);
    int                    (*run)(struct poclidek_opgroup_rt *);
};

#define OPGROUP_RC_NIL    0
#define OPGROUP_RC_OK     (1 << 0)
#define OPGROUP_RC_ERROR  (1 << 1)

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

struct poclidek_opgroup_rt *poclidek_opgroup_rt_new(struct poldek_ts *ts,
                                                    struct poclidek_op_ctx *opctx);
void poclidek_opgroup_rt_free(struct poclidek_opgroup_rt *rt);

#endif
