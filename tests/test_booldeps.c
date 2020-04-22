#include "test.h"
#include <trurl/nbuf.h>

#define POLDEK_BOOLDEP_INTERNAL
#include "booldep.h"

void do_serialize(struct node *node, tn_buf *buf)
{
    n_buf_puts(buf, node->name);
    n_buf_puts(buf, ",");

    for (int i = 0; i < NARY_MAX; i++) {
        struct node *n = node->args[i];
        if (n) {
            do_serialize(n, buf);
        }
    }
    n_buf_puts(buf, ";");
}

tn_buf *serialize(struct node *node) {
    tn_buf *buf = n_buf_new(128);
    do_serialize(node, buf);
    n_buf_puts_z(buf, "");
    return buf;
}

START_TEST (test_parse) {
    struct ecase {
        const char *expr;
        const char *serialized;
    } cases[] = {
        { "(a and b)", "and,a,;b,;;" },
        { "(a or b)", "or,a,;b,;;" },
        { "(a and (b or c))", "and,a,;or,b,;c,;;;" },
        { "(a or (b and c))", "or,a,;and,b,;c,;;;" },
        { "((a or b) and (c or d))", "and,or,a,;b,;;or,c,;d,;;;" },
        { "((a and b) or (c and d))", "or,and,a,;b,;;and,c,;d,;;;" },
        { "(a if b)", "if,a,;b,;;" },
        { "(a if (b or c))", "if,a,;or,b,;c,;;;" },
        { "(a if b else c)", "if,a,;b,;c,;;" },
        { "(a unless b)", "unless,a,;b,;;" },
        { "(a unless (b or c))", "unless,a,;or,b,;c,;;;" },
        { "(a unless b else c)", "unless,a,;b,;c,;;" },
        { "(a with b)", "with,a,;b,;;" },
        { "(a without b)", "without,a,;b,;;" },
        { "(a with b with c)", "with,a,;with,b,;c,;;;" },
        { NULL, NULL }
    };

    int i = 0;
    while (cases[i].expr != NULL) {
        const struct ecase *e = &cases[i++];
        struct booldep *dep = booldep_parse(e->expr);
        expect_notnull(dep);

        tn_buf *buf = serialize(dep->node);

        expect_notnull(buf);
        expect_str(n_buf_ptr(buf), e->serialized);

        n_buf_free(buf);
        booldep_free(dep);
    }

    const char *invalid_cases[] = {
        "a or b",               /* missing brackets */
        "(a or b",
        "(a or ",
        "(a on b)",             /* unknown op */
        "(a if)",
        NULL
    };

    i = 0;
    const char *e;
    while ((e = invalid_cases[i++]) != NULL) {
        struct booldep *dep = booldep_parse(e);
        expect_null(dep);
    }
}
END_TEST

int req_cost_satisfied_a(const struct capreq *req, tn_array **providers, void *ctx) {
    (void)ctx;
    (void)providers;
    return strcmp(capreq_name(req), "a") == 0 ? 0 : 1;
}

int req_cost_satisfied_b(const struct capreq *req, tn_array **providers, void *ctx) {
    (void)ctx;
    (void)providers;
    return strcmp(capreq_name(req), "b") == 0 ? 0 : 1;
}

int req_cost_satisfied_c(const struct capreq *req, tn_array **providers, void *ctx) {
    (void)ctx;
    (void)providers;
    return strcmp(capreq_name(req), "c") == 0 ? 0 : 1;
}

int req_cost_satisfied_none(const struct capreq *req, tn_array **providers, void *ctx) {
    (void)ctx;
    (void)req;
    (void)providers;
    return 99;
}

static const char *array_str(const tn_array *a)
{
    static char buf[4096];
    int n = 0;
    for (int i = 0; i < n_array_size(a); i++) {
        const struct capreq *req = n_array_nth(a, i);
        n += n_snprintf(&buf[n], sizeof(buf) - n, "%s,", capreq_stra(req));
    }
    if (n > 0)
        buf[n - 1] = '\0';      /* trim last comma */

    return buf;
}

START_TEST (test_eval) {
    struct ecase {
        const char *expr;
        int (*req_cost)(const struct capreq *req, tn_array **providers, void *ctx);
        const char *expected;
    } cases[] = {
        /* or */
        { "(a or b)", req_cost_satisfied_a, "a" },
        { "(a or b)", req_cost_satisfied_b, "b" },
        { "(a or b)", req_cost_satisfied_none, "a" },
        { "(a or b or c)", req_cost_satisfied_a, "a" },
        { "(a or b or c)", req_cost_satisfied_b, "b" },
        { "(a or b or c)", req_cost_satisfied_c, "c" },
        { "(a or b or c)", req_cost_satisfied_none, "a" },

        /* and */
        { "(a and b)", req_cost_satisfied_a, "a,b" },
        { "(a and b)", req_cost_satisfied_b, "a,b" },
        { "(a and b)", req_cost_satisfied_none, "a,b" },
        { "(a and b and c)", req_cost_satisfied_none, "a,b,c" },

        /* or/and */
        { "(a and (b or c))", req_cost_satisfied_none, "a,b" },
        { "(a and (b or c))", req_cost_satisfied_b, "a,b" },
        { "(a and (b or c))", req_cost_satisfied_c, "a,c" },

        /* if */
        { "(a if b)", req_cost_satisfied_none, NULL },
        { "(a if b)", req_cost_satisfied_a, NULL },
        { "(a if b)", req_cost_satisfied_b, "a" },
        { "((a and c) if b)", req_cost_satisfied_b, "a,c" },
        { "(a if (b or c))", req_cost_satisfied_b, "a" },
        { "(a if (b or c))", req_cost_satisfied_c, "a" },
        { "(a if (b or c))", req_cost_satisfied_none, NULL },

        /* if else */
        { "(a if b else c)", req_cost_satisfied_b, "a" },
        { "(a if b else c)", req_cost_satisfied_none, "c" },

        /* unless */
        { "(a unless b)", req_cost_satisfied_none, "a" },
        { "(a unless b)", req_cost_satisfied_a, "a" },
        { "(a unless b)", req_cost_satisfied_b, NULL },
        { "((a and c) unless b)", req_cost_satisfied_b, NULL },
        { "((a and c) unless b)", req_cost_satisfied_none, "a,c" },
        { "(a unless (b or c))", req_cost_satisfied_b, NULL },
        { "(a unless (b or c))", req_cost_satisfied_c, NULL },
        { "(a unless (b or c))", req_cost_satisfied_none, "a" },

        { NULL, NULL, NULL }
    };

    struct booldep_eval_ctx ctx = { NULL, NULL };

    int i = 0;
    while (cases[i].expr != NULL) {
        const struct ecase *e = &cases[i++];
        struct booldep *dep = booldep_parse(e->expr);

        ctx.req_cost = e->req_cost;
        tn_array *re = booldep_eval(dep, &ctx);


        NTEST_LOG("%d. %s => expected %s, got %s\n", i, e->expr, e->expected,
                  re ? array_str(re) : NULL);

        expect_notnull(re);

        if (e->expected == NULL) {
            expect_int(n_array_size(re), 0);
        } else {
            expect_str(array_str(re), e->expected);
        }

        n_array_free(re);
        booldep_free(dep);
    }
}
END_TEST

NTEST_RUNNER("boolean deps", test_parse, test_eval);
