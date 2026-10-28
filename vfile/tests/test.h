#ifndef VFILE_TEST_H
#define VFILE_TEST_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include <trurl/n_check.h>
#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
#include <trurl/n_snprintf.h>

#include "../vfile.h"
#define fail_ifnot fail_unless

/* set the mtime of NAME in the test server's data directory to the last
   nanosecond of second SEC, which a server rounding the time sends as SEC + 1 */
static inline int set_server_mtime(const char *name, time_t sec)
{
    char path[PATH_MAX];
    struct timespec ts[2] = { { sec, 999999999 }, { sec, 999999999 } };
    const char *datadir = getenv("TEST_DATA_DIR");

    if (datadir == NULL ||
        (size_t)snprintf(path, sizeof(path), "%s/%s", datadir, name) >= sizeof(path))
        return -1;

    return utimensat(AT_FDCWD, path, ts, 0);
}

/* preserve TZ across a test case; use_zone_with_dst() changes it */
static char saved_tz[256];
static int saved_tz_was_set;

static inline void save_tz(void)
{
    const char *tz = getenv("TZ");

    saved_tz_was_set = tz != NULL;
    if (tz != NULL)
        snprintf(saved_tz, sizeof(saved_tz), "%s", tz);
}

static inline void restore_tz(void)
{
    if (saved_tz_was_set)
        setenv("TZ", saved_tz, 1);
    else
        unsetenv("TZ");

    tzset();
}

/* a zone with summer time, where a server time taken for local time shows */
static inline void use_zone_with_dst(void)
{
    setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);
    tzset();
}

struct test_case {
    const char *name;
    void (*test_fn)(void);
};

struct test_suite {
    const char *name;
    struct test_case cases[];
};

#endif
