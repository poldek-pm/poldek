#ifndef POLDEKCLI_OPGROUP_H
#define POLDEKCLI_OPGROUP_H

#include <argp.h>               /* for struct argp* */

#ifndef EXPORT
# define EXPORT extern
#endif

struct poldek_ctx;
struct poldek_ts;

struct poclidek_op_ctx;

EXPORT struct poclidek_op_ctx *poclidek_op_ctx_new(void);
EXPORT void poclidek_op_ctx_free(struct poclidek_op_ctx *);
EXPORT int poclidek_op_ctx_verify_major_mode(struct poclidek_op_ctx *opctx);

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

EXPORT struct poclidek_opgroup poclidek_opgroup_source;
EXPORT struct poclidek_opgroup poclidek_opgroup_install;
EXPORT struct poclidek_opgroup poclidek_opgroup_packages;
EXPORT struct poclidek_opgroup poclidek_opgroup_uninstall;
EXPORT struct poclidek_opgroup poclidek_opgroup_makeidx;
EXPORT struct poclidek_opgroup poclidek_opgroup_split;
EXPORT struct poclidek_opgroup poclidek_opgroup_verify;

EXPORT struct poclidek_opgroup_rt *poclidek_opgroup_rt_new(struct poldek_ts *ts,
                                                    struct poclidek_op_ctx *opctx);
EXPORT void poclidek_opgroup_rt_free(struct poclidek_opgroup_rt *rt);

#endif
