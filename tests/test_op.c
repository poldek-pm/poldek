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
    return 1;

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

START_TEST (test_ts_config_later) {
    struct poldek_ctx *ctx;
    struct poldek_ts *ts;
    tn_array *cnf = n_array_new(16, NULL, NULL);

    setlocale(LC_MESSAGES, "");
    setlocale(LC_CTYPE, "");
    poldeklib_init();

    ctx = poldek_new(0);
    ts = poldek_ts_new(ctx, 0);  /* before setup -> deferred sync */

    n_array_push(cnf, "multilib = 1");
    poldek_load_config(ctx, NULL, cnf, POLDEK_LOADCONF_NOCONF);

    /* ts created before setup does not see config until setup */
    fail_if(poldek_ts_getop(ts, POLDEK_OP_MULTILIB),
            "deferred ts was synced before setup");

    poldek_setup(ctx);

    fail_unless(poldek_ts_getop(ts, POLDEK_OP_MULTILIB),
                "deferred ts not synced by poldek_setup()");

    /* explicit postconf remains harmless */
    poldek__ts_postconf(ctx, ts);
    fail_unless(poldek_ts_getop(ts, POLDEK_OP_MULTILIB),
                "deferred ts changed after explicit postconf");

    poldek_ts_free(ts);
    poldek_free(ctx);
}
END_TEST

START_TEST (test_ts_config_later_free) {
    struct poldek_ctx *ctx;
    struct poldek_ts *ts;

    setlocale(LC_MESSAGES, "");
    setlocale(LC_CTYPE, "");
    poldeklib_init();

    /* ts freed before setup must be unregistered cleanly */
    ctx = poldek_new(0);
    ts = poldek_ts_new(ctx, 0);
    poldek_ts_free(ts);

    poldek_setup(ctx);      /* would touch freed ts if not unregistered */
    poldek_free(ctx);

    /* ctx freed with a deferred ts alive must not confuse ts_free */
    ctx = poldek_new(0);
    ts = poldek_ts_new(ctx, 0);
    poldek_free(ctx);
    poldek_ts_free(ts);
}
END_TEST

NTEST_RUNNER("ts option", test_op_ts_postconf, test_ts_config_later,
             test_ts_config_later_free);
