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
#include <trurl/n_check.h>

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "misc.h"
#include "capreq.h"
#include "pm/pm.h"
#include "config.h"
#ifdef HAVE_RPMORG
# include "pm/rpmorg/pm_rpm.h"
#else
# include "pm/rpm/pm_rpm.h"
#endif
#include "pkgmisc.h"

#define fail_ifnot fail_unless

struct test_case {
    const char *name;
    void (*test_fn)(int);
};

struct test_suite {
    const char *name;
    struct test_case cases[];
};

#endif
