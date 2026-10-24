/*
  sigint_init() and sigint_destroy() calls nest: only the outermost pair sets
  up and restores the SIGINT handler. sigint_emit(), which library users call
  from another thread to cancel, stays safe after sigint_destroy().
*/
#include <signal.h>
#include "test.h"
#include "sigint/sigint.h"

/* the shell sets the handler up, then each install or removal again */
START_TEST (test_nested_init_keeps_handler) {
    signal(SIGINT, SIG_DFL);    /* the suite may run with SIGINT ignored */
    sigint_init();
    sigint_init();
    sigint_destroy();

    raise(SIGINT);              /* kills the test if the handler is gone */
    fail_unless(sigint_reached(), "SIGINT not noted");

    sigint_destroy();
    fail_unless(signal(SIGINT, SIG_DFL) == SIG_DFL, "handler not restored");
}
END_TEST

/* a cancel that comes between two operations */
START_TEST (test_emit_after_destroy) {
    sigint_init();
    sigint_destroy();

    sigint_emit();
    fail_unless(sigint_reached(), "sigint_emit() not noted");
}
END_TEST

NTEST_RUNNER("sigint", test_nested_init_keeps_handler, test_emit_after_destroy);
