#include "test.h"
#include "poldek_term.h"
#include "poldek.h"

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <term.h>

/* puts_c() must reset bold like the printf-style helpers do; at_no_attr was
   never appended because isbold stayed 0 */
START_TEST (test_puts_c_resets_bold) {
    char expected[256], got[256];
    const char *path = NTEST_TMPPATH("term-puts-c.out");
    const char *sgr0;
    int fd, saved, n;

    setenv("TERM", "xterm", 1);
    fail_unless(poldek_term_init(1), "cannot initialize TERM=xterm");

    sgr0 = tigetstr("sgr0");
    fail_unless(sgr0 != NULL && sgr0 != (char*)-1 && *sgr0 != '\0',
                "TERM=xterm has no sgr0");

    poldek_term_snprintf_c(PRAT_BOLD | PRCOLOR_GREEN, expected, sizeof(expected),
                           "%s", "x");

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    fail_unless(fd >= 0, "cannot open %s", path);

    fflush(stdout);
    saved = dup(STDOUT_FILENO);
    fail_unless(saved >= 0);
    fail_unless(dup2(fd, STDOUT_FILENO) >= 0);

    poldek_term_puts_c(PRAT_BOLD | PRCOLOR_GREEN, "x");
    fflush(stdout);

    fail_unless(dup2(saved, STDOUT_FILENO) >= 0);
    close(saved);

    lseek(fd, 0, SEEK_SET);
    n = read(fd, got, sizeof(got) - 1);
    close(fd);
    unlink(path);
    fail_unless(n >= 0);
    got[n] = '\0';

    fail_unless(strcmp(got, expected) == 0,
                "puts_c wrote '%s', expected '%s'", got, expected);
}
END_TEST

static void sigwinch_handler(int signo)
{
    signo = signo;
}

/* poldeklib_destroy() must undo what poldeklib_init() did to SIGWINCH; only
   a tty makes poldek_term_init() install its handler */
START_TEST (test_destroy_restores_sigwinch) {
    struct sigaction sa, old, cur;
    int mfd, sfd, saved, rc;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigwinch_handler;
    sigemptyset(&sa.sa_mask);
    fail_unless(sigaction(SIGWINCH, &sa, &old) == 0);

    mfd = posix_openpt(O_RDWR | O_NOCTTY);
    fail_unless(mfd >= 0, "posix_openpt: %s", strerror(errno));
    fail_unless(grantpt(mfd) == 0, "grantpt: %s", strerror(errno));
    fail_unless(unlockpt(mfd) == 0, "unlockpt: %s", strerror(errno));
    sfd = open(ptsname(mfd), O_RDWR | O_NOCTTY);
    fail_unless(sfd >= 0, "open pty: %s", strerror(errno));

    setenv("TERM", "xterm", 1);

    fflush(stdout);
    saved = dup(STDOUT_FILENO);
    fail_unless(saved >= 0);
    fail_unless(dup2(sfd, STDOUT_FILENO) >= 0);

    rc = poldeklib_init();
    poldeklib_destroy();

    fflush(stdout);
    fail_unless(dup2(saved, STDOUT_FILENO) >= 0);
    close(saved);
    close(sfd);
    close(mfd);

    fail_unless(rc, "poldeklib_init failed");
    fail_unless(sigaction(SIGWINCH, NULL, &cur) == 0);
    fail_unless(cur.sa_handler == sigwinch_handler,
                "SIGWINCH handler was not restored");

    sigaction(SIGWINCH, &old, NULL);
}
END_TEST

NTEST_RUNNER("term", test_puts_c_resets_bold, test_destroy_restores_sigwinch);
