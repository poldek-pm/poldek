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

/* the mtime of NAME in the test server's data directory */
static inline time_t server_mtime(const char *name)
{
    char path[PATH_MAX];
    struct stat st;

    if ((size_t)snprintf(path, sizeof(path), "%s/%s", getenv("TEST_DATA_DIR"),
                         name) >= sizeof(path) || stat(path, &st) != 0)
        return -1;

    return st.st_mtime;
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
