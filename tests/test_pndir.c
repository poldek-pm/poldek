/*
  pndir_mkidx_pathname() names a file next to a pndir index: a name that
  fits the buffer is built in full, a longer one gives NULL.
*/
#include "test.h"
#include "pkgdir/pndir/pndir.h"

/* 26 characters: with ".dscr" and the trailing '\0', 32 */
#define IDXPATH "/repo/xxx/packages.ndir.gz"

START_TEST (test_name_fills_buffer) {
    char dest[32];

    fail_unless(pndir_mkidx_pathname(dest, sizeof(dest), IDXPATH, ".dscr") == dest);
    fail_unless(strcmp(dest, "/repo/xxx/packages.ndir.dscr.gz") == 0, "got '%s'", dest);
}
END_TEST

START_TEST (test_name_too_long) {
    char dest[31];

    errno = 0;
    fail_unless(pndir_mkidx_pathname(dest, sizeof(dest), IDXPATH, ".dscr") == NULL);
    fail_unless(errno == ENAMETOOLONG, "errno %d", errno);
}
END_TEST

NTEST_RUNNER("pndir", test_name_fills_buffer, test_name_too_long);
