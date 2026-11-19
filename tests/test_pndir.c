/*
  pndir_mkidx_pathname() names a file next to a pndir index: a name that
  fits the buffer is built in full, a longer one gives NULL.
*/
#include <stdarg.h>
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

/*
  The files of a diff carry the timestamp of the index it starts from,
  in the form pndir_tsstr() writes; the list itself and other files
  have none.
*/
#define TSS "2026.11.18-16.04.00"

START_TEST (test_find_tss) {
    const char *names[] = {
        "packages.ndir." TSS ".gz",
        "packages.ndir.dscr." TSS ".gz",
        "packages.ndir.dscr.pl." TSS,
        NULL,
    };
    const char *t;
    int i;

    for (i = 0; names[i]; i++) {
        t = pndir_difftoc_find_tss(names[i]);
        fail_unless(t != NULL, "%s: no timestamp found", names[i]);
        fail_unless(strncmp(t, TSS, strlen(TSS)) == 0, "%s: got '%s'", names[i], t);
    }
}
END_TEST

START_TEST (test_find_tss_none) {
    const char *names[] = {
        "packages.ndir.diff.toc",
        "packages.ndir.gz",
        "packages.ndir.2026.11.18-16.04.0",     /* too short */
        "packages.ndir.2026.11.18-16.04.0x.gz", /* not a digit */
        "packages.ndir.2026-11-18-16.04.00.gz", /* not the form */
        "packages.ndir.2026.11.18-16.04.001",   /* followed by neither '.' nor '\0' */
        "packages.ndir" TSS,                    /* not after a '.' */
        NULL,
    };
    int i;

    for (i = 0; names[i]; i++)
        fail_if(pndir_difftoc_find_tss(names[i]) != NULL, "%s: timestamp found", names[i]);
}
END_TEST

/*
  The last listed diff must lead to the index the new one starts from.
*/
static tn_array *toc_lines(const char *line, ...)
{
    tn_array *lines = n_array_new(4, free, NULL);
    va_list ap;

    va_start(ap, line);
    for (; line; line = va_arg(ap, const char *))
        n_array_push(lines, n_strdup(line));
    va_end(ap);

    return lines;
}

START_TEST (test_leads_to) {
    tn_array *lines;

    lines = toc_lines(NULL);
    fail_unless(pndir_difftoc_leads_to(lines, 100), "an empty list");
    n_array_free(lines);

    lines = toc_lines("packages.ndir.a.gz 100 md 50\n",
                      "packages.ndir.b.gz 200 md 100\n", NULL);
    fail_unless(pndir_difftoc_leads_to(lines, 200), "the last diff leads to 200");
    fail_if(pndir_difftoc_leads_to(lines, 100), "only an older diff leads to 100");
    n_array_free(lines);

    /* emptied lines (the vaccumed ones) are skipped */
    lines = toc_lines("packages.ndir.a.gz 100 md 50\n", "", "\n", NULL);
    fail_unless(pndir_difftoc_leads_to(lines, 100), "empty lines are skipped");
    n_array_free(lines);

    lines = toc_lines("packages.ndir.a.gz 100 md 50\n", "broken\n", NULL);
    fail_if(pndir_difftoc_leads_to(lines, 100), "a line without a timestamp");
    n_array_free(lines);
}
END_TEST

NTEST_RUNNER("pndir", test_name_fills_buffer, test_name_too_long,
             test_create_paths_too_long, test_find_tss, test_find_tss_none,
             test_leads_to);
