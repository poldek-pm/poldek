#include <trurl/nmalloc.h>

#include "cli.h"
#include "op.h"

struct poclidek_opgroup_rt *poclidek_opgroup_rt_new(struct poldek_ts *ts)
{
    struct poclidek_opgroup_rt *rt;

    rt = n_malloc(sizeof(*rt));
    rt->ctx = ts->ctx;
    rt->ts = ts;
    rt->_opdata = NULL;
    rt->_opdata_free = NULL;
    return rt;
};

void poclidek_opgroup_rt_free(struct poclidek_opgroup_rt *rt)
{
    n_assert(rt->_opdata_free);
    if (rt->_opdata && rt->_opdata_free) {
        rt->_opdata_free(rt->_opdata);
        rt->_opdata = NULL;
    }
    
    rt->ctx = NULL;
};
