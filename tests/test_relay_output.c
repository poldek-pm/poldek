/*
  poldek__relay_output() and a ^C typed while the command it relays runs.
  RELAY_SIGINT_NOTICE is for a command that must run to its end, as rpm
  through sudo, which would pass a SIGTERM on to rpm's scriptlets: no ^C,
  the first or a later one, gets to it.
*/
#include <signal.h>
#include "test.h"
#include "sigint/sigint.h"
#include "vfile/p_open.h"

START_TEST (test_notice_never_stops_command) {
    char dir[PATH_MAX], runs[PATH_MAX], script[PATH_MAX * 2], buf[64];
    const char *tmpdir = getenv_path("TMPDIR");
    char *argv[] = { "sh", "-c", script, NULL };
    struct p_open_st pst;
    FILE *f;
    int n;

    n_snprintf(dir, sizeof(dir), "%s/poldek-relay-XXXXXX",
               tmpdir ? tmpdir : "/tmp");
    fail_if(mkdtemp(dir) == NULL, "mkdtemp %s: %s", dir, strerror(errno));
    n_snprintf(runs, sizeof(runs), "%s/runs", dir);

    /* two ^C while the command runs, as the terminal sends them to poldek */
    n_snprintf(script, sizeof(script),
               "trap 'echo TERM >> %s' TERM; kill -INT $PPID; sleep 1; "
               "kill -INT $PPID; sleep 1; echo done >> %s", runs, runs);

    signal(SIGINT, SIG_DFL);    /* the suite may run with SIGINT ignored */
    sigint_init();
    p_st_init(&pst);
    fail_if(p_open(&pst, 0, "/bin/sh", argv) == NULL, "p_open: %s", pst.errmsg);
    poldek__relay_output(&pst, 1, "cmd", RELAY_SIGINT_NOTICE);
    fail_if(p_close(&pst) != 0, "the command failed: %s", pst.errmsg);
    p_st_destroy(&pst);

    fail_unless(sigint_count() == 2, "%d ^C, not 2", sigint_count());
    sigint_destroy();

    f = fopen(runs, "r");
    fail_if(f == NULL, "%s: %s", runs, strerror(errno));
    n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    fail_unless(strcmp(buf, "done\n") == 0, "the command logged \"%s\"", buf);

    unlink(runs);
    rmdir(dir);
}
END_TEST

NTEST_RUNNER("relay_output", test_notice_never_stops_command);
