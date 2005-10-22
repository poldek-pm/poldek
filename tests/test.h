#ifndef POLDEK_TEST_H
#define POLDEK_TEST_H
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

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "misc.h"
#include "capreq.h"
#include "pm/pm.h"
#include "pm/rpm/pm_rpm.h"
#include "pkgmisc.h"

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
