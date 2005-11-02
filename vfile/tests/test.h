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

#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
#include <check.h>

#include "../vfile.h"
#define fail_ifnot fail_unless

struct test_case {
    const char *name;
    void (*test_fn)(void);
};

struct test_suite {
    const char *name;
    struct test_case cases[];
};

#endif
