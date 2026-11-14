/*
  pkgu__parse_datetime() reads the changelog dates poldek prints, which
  are UTC.  The date must not depend on the local time zone.
*/
#include "test.h"
#include "pkgu.h"

extern time_t pkgu__parse_datetime(const char *str);

START_TEST (test_parse_datetime_uses_utc) {
    char old_tz[256];
    const char *tz = getenv("TZ");
    int had_tz = tz != NULL;

    if (had_tz)
        snprintf(old_tz, sizeof(old_tz), "%s", tz);

    /* UTC+14: a local-time reading of noon would fall on the previous day */
    setenv("TZ", "UTC-14", 1);
    tzset();

    fail_unless(pkgu__parse_datetime("2020-04-26 12:00:00") ==
                (time_t)1587902400, "date read as local time");

    if (had_tz)
        setenv("TZ", old_tz, 1);
    else
        unsetenv("TZ");
    tzset();
}
END_TEST

START_TEST (test_parse_datetime_rejects_invalid) {
    fail_unless(pkgu__parse_datetime("") == 0);
    fail_unless(pkgu__parse_datetime("not a date") == 0);
}
END_TEST

/* a "*" changelog line with no space after it has no timestamp; it must not
   be dereferenced as if it had one */
START_TEST (test_changelog_entry_without_space) {
    tn_alloc *na = n_alloc_new(8, TN_ALLOC_OBSTACK);
    tn_buf *nbuf = n_buf_new(128);
    tn_buf_it it;
    struct pkguinf *inf;
    const char *log;

    /* the pkguinf_restore() format: tag, NUL, value, NUL; 'E' ends the
       common part, summary and description follow */
    n_buf_putc(nbuf, PKGUINF_CHANGELOG); n_buf_putc(nbuf, '\0');
    n_buf_puts(nbuf, "*\n- no space after the star\n"
                     "* 2030-01-01 00:00:00 someone\n- later entry\n");
    n_buf_putc(nbuf, '\0');

    n_buf_putc(nbuf, 'E'); n_buf_putc(nbuf, '\0');

    n_buf_putc(nbuf, PKGUINF_SUMMARY); n_buf_putc(nbuf, '\0');
    n_buf_puts(nbuf, "summary"); n_buf_putc(nbuf, '\0');

    n_buf_putc(nbuf, PKGUINF_DESCRIPTION); n_buf_putc(nbuf, '\0');
    n_buf_puts(nbuf, "description"); n_buf_putc(nbuf, '\0');

    n_buf_it_init(&it, nbuf);
    inf = pkguinf_restore(na, &it, "C");
    fail_unless(inf != NULL);

    const char *raw = pkguinf_get(inf, PKGUINF_CHANGELOG);
    fail_unless(raw != NULL && raw[0] == '*', "raw='%s'", raw ? raw : "(null)");

    log = pkguinf_get_changelog(inf, 1);
    fail_unless(log != NULL);
    fail_unless(strstr(log, "- later entry") != NULL, "got '%s'", log);

    pkguinf_free(inf);
    n_buf_free(nbuf);
    n_alloc_free(na);
}
END_TEST

NTEST_RUNNER("pkgu", test_parse_datetime_uses_utc,
             test_parse_datetime_rejects_invalid,
             test_changelog_entry_without_space);
