/*
  pndir_mkidx_pathname() names a file next to a pndir index: a name that
  fits the buffer is built in full, a longer one gives NULL.
*/
#include <unistd.h>

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

/*
  A diff index gets a timestamp suffix.  If that name does not fit in
  PATH_MAX, pndir_m_create() must stop at mk_paths() and report
  ENAMETOOLONG instead of going on with an empty paths.path.
*/
START_TEST (test_create_paths_too_long) {
    char tmpl[] = "/tmp/poldek-pndir-XXXXXX";
    char path[PATH_MAX + 64];
    struct pkgdir pkgdir;
    char *dir;
    int n;

    dir = mkdtemp(tmpl);
    fail_unless(dir != NULL, "mkdtemp: %s", strerror(errno));

    n = snprintf(path, sizeof(path), "%s/", dir);
    fail_unless(n > 0 && n < (int)sizeof(path));
    memset(path + n, 'x', sizeof(path) - n - 1);
    path[sizeof(path) - 1] = '\0';

    memset(&pkgdir, 0, sizeof(pkgdir));
    pkgdir.flags = PKGDIR_DIFF;
    pkgdir.ts = pkgdir.orig_ts = 1;

    errno = 0;
    fail_unless(pndir_m_create(&pkgdir, path, 0) == 0);
    fail_unless(errno == ENAMETOOLONG, "errno %d", errno);

    snprintf(path, sizeof(path), "%s/packages.i", dir);
    rmdir(path);
    rmdir(dir);
}
END_TEST

NTEST_RUNNER("pndir", test_name_fills_buffer, test_name_too_long,
             test_create_paths_too_long);
