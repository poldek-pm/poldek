#include "test.h"
#include "poldek_intern.h"

static int is_op(struct poldek_ts *ts, int op, int v) 
{
    if (v)
        return ts->getop(ts, op);
    
    return !ts->getop(ts, op);
}


static int do_test_op(struct poldek_ctx *ctx, struct poldek_ts *ts,
                      int op, const char *opname, int defaultv)
{
    fail_unless(is_op(ctx->ts, op, defaultv),
                "fresh ctx has %s?", opname);

    fail_unless(is_op(ts, op, defaultv),
                "fresh ts has %s = %d?", opname, !defaultv);
    
    poldek_configure(ctx, POLDEK_CONF_OPT, op, !defaultv);

    fail_unless(is_op(ctx->ts, op, !defaultv), "poldek_configure() fails");

    fail_unless(is_op(ts, op, defaultv), "fresh ts has %s = %d??", opname, !defaultv);

    poldek__ts_postconf(ctx, ts);
    /* should be propagated */

    fail_unless(is_op(ts, op, !defaultv),
                "%s not propagated to ts", opname);
}

START_TEST (test_op_ts_postconf) {
    struct poldek_ctx *ctx;
    struct poldek_ts *ts;
    tn_array *cnf = n_array_new(16, NULL, NULL);
    
    setlocale(LC_MESSAGES, "");
    setlocale(LC_CTYPE, "");
    poldeklib_init();

    ctx = poldek_new(0);
    ts = poldek_ts_new(ctx, 0);
    poldek_setup(ctx);          /* ts is not fully configured */
    do_test_op(ctx, ts, POLDEK_OP_PROMOTEPOCH, "promoteepoch", 0);

    poldek_ts_free(ts);
    poldek_free(ctx);

    ctx = poldek_new(0);
    n_array_push(cnf, "promoteepoch = 1");
    poldek_load_config(ctx, NULL, cnf, POLDEK_LOADCONF_NOCONF);
    
    ts = poldek_ts_new(ctx, 0);
    
    poldek_setup(ctx);          /* ts is not fully configured */
    do_test_op(ctx, ts, POLDEK_OP_PROMOTEPOCH, "promoteepoch", 1);
    return;
}
END_TEST
    
struct test_suite test_suite_op = {
    "playing with option", 
    {
        { "ts_postconf", test_op_ts_postconf },
        { NULL, NULL }
    }
};

    

    


