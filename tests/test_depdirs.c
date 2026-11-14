#include "test.h"

extern void init_depdirs(tn_array *dirnames);
extern void destroy_depdirs(void);
extern int in_depdirs(const char *dir);

/* destroy without a prior init must be a no-op, not a NULL dereference */
START_TEST (test_destroy_without_init) {
    destroy_depdirs();
    destroy_depdirs();
}
END_TEST

/* init may replace a previous set; the new one must win */
START_TEST (test_reinit) {
    tn_array *dirs = n_array_new(2, free, (tn_fn_cmp)strcmp);

    n_array_push(dirs, n_strdup("usr"));

    init_depdirs(dirs);
    n_array_push(dirs, n_strdup("opt"));
    init_depdirs(dirs);

    fail_unless(in_depdirs("usr/share") == 1);
    fail_unless(in_depdirs("opt/lib") == 1);
    fail_unless(in_depdirs("etc") == 0);

    n_array_free(dirs);
    destroy_depdirs();
}
END_TEST

NTEST_RUNNER("depdirs", test_destroy_without_init, test_reinit);
