/*
  A die message (LOGDIE) reaches the goodbye callback set with
  POLDEK_CONF_GOODBYE_CB; a callback that returns 0 keeps poldek running.
*/
#include "test.h"
#include "poldek.h"

static char goodbye_msg[1024];
static int ngoodbyes = 0;

static int keep_going(const char *msg)
{
    n_snprintf(goodbye_msg, sizeof(goodbye_msg), "%s", msg);
    ngoodbyes++;
    return 0;
}

START_TEST (test_goodbye_cb) {
    struct poldek_ctx *ctx;

    poldeklib_init();
    ctx = poldek_new(0);
    fail_unless(poldek_configure(ctx, POLDEK_CONF_GOODBYE_CB, keep_going),
                "poldek_configure() failed");
    fail_unless(ngoodbyes == 0, "poldek_configure() said goodbye: '%s'", goodbye_msg);

    logn(LOGERR | LOGDIE, "packages are equal to me, give up");
    fail_unless(strstr(goodbye_msg, "packages are equal to me, give up") != NULL,
                "got '%s'", goodbye_msg);
    poldek_free(ctx);
}
END_TEST

NTEST_RUNNER("log", test_goodbye_cb);
