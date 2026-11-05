/*
  sigint_init() and sigint_destroy() calls nest: only the outermost pair sets
  up and restores the SIGINT handler. sigint_emit(), which library users call
  from another thread to cancel, stays safe after sigint_destroy(). A signal
  a handler notes with sigint_note_end() is not lost, even while the
  operation ends, and takes effect at its end, also when it ends before it
  begins.
*/
#include <dlfcn.h>
#include <signal.h>
#include "test.h"
#include "sigint/sigint.h"
#include "poldek.h"
#include "poldek_ts.h"

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

static int hup_at_query = 0;

/* sigint_release() asks for a handler while it reads and resets the
   signals noted: a SIGHUP comes then */
int sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
    int (*next)(int, const struct sigaction *, struct sigaction *) =
        (int (*)(int, const struct sigaction *, struct sigaction *))
        dlsym(RTLD_NEXT, "sigaction");

    if (hup_at_query && sig == SIGTERM && act == NULL) {
        hup_at_query = 0;
        raise(SIGHUP);
    }
    return next(sig, act, oact);
}

static void note_end(int sig)
{
    sigint_note_end(sig);
}

static void caught(int sig)
{
    (void)sig;
}

/* a handler of the library user's own notes a signal as the operation ends */
START_TEST (test_note_while_ending_kept) {
    signal(SIGHUP, note_end);
    signal(SIGTERM, caught);    /* what the operation ends with */
    sigint_hold();
    raise(SIGTERM);             /* while rpm runs */
    sigint_unhold();

    hup_at_query = 1;
    sigint_release();
    fail_if(hup_at_query, "sigint_release() asked for no handler");
    fail_unless(sigint_ending() & (1 << SIGHUP), "SIGHUP noted then was lost");

    signal(SIGHUP, SIG_IGN);    /* the next operation ends with it */
    sigint_release();
    signal(SIGHUP, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
}
END_TEST

static volatile sig_atomic_t ncaught = 0;

static void count(int sig)
{
    (void)sig;
    ncaught++;
}

/* a library user notes a signal for an operation that ends before it
   begins, on a package list it cannot read */
START_TEST (test_release_after_early_end) {
    struct poldek_ctx *ctx;
    struct poldek_ts *ts;

    poldeklib_init();
    ctx = poldek_new(0);
    fail_if(poldek_setup(ctx) == 0, "poldek_setup() failed");
    ts = poldek_ts_new(ctx, 0);
    poldek_ts_set_type(ts, POLDEK_TS_TYPE_INSTALL, "install");
    poldek_ts_add_pkglist(ts, "/nonexistent");

    signal(SIGTERM, count);
    sigint_note_end(SIGTERM);
    fail_if(poldek_ts_run(ts, 0), "the operation did not end early");
    fail_unless(ncaught == 1, "the signal noted took no effect");
    fail_if(sigint_ending(), "the signal stays noted for the next operation");

    signal(SIGTERM, SIG_DFL);
    poldek_ts_free(ts);
    poldek_free(ctx);
}
END_TEST

NTEST_RUNNER("sigint", test_nested_init_keeps_handler, test_emit_after_destroy,
             test_note_while_ending_kept, test_release_after_early_end);
