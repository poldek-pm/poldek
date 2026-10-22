/*
  pkgu__parse_datetime() reads the changelog dates poldek prints, which
  are UTC.  The date must not depend on the local time zone.
*/
#include "test.h"

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

NTEST_RUNNER("pkgu", test_parse_datetime_uses_utc,
             test_parse_datetime_rejects_invalid);
