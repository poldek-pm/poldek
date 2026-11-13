#include "test.h"
#include "poldek_term.h"

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

NTEST_RUNNER("term", test_puts_c_resets_bold);
