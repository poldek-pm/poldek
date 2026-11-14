#include "test.h"
#include <time.h>
#include <stdint.h>

#include "cli/dent.h"

#define DAY  86400
#define NOON(d) ((uint32_t)((d) * DAY + 12 * 3600))

static struct pkg *mkpkg(const char *name, uint32_t btime)
{
    struct pkg *pkg = pkg_new(name, 0, "1", "1", "noarch", "linux");
    pkg->btime = btime;
    return pkg;
}

static void init_dent(struct pkg_dent *ent, const char *name, struct pkg *pkg)
{
    memset(ent, 0, sizeof(*ent));
    ent->flags = 0;
    ent->name = name;
    ent->pkg_dent_pkg = pkg;
}

static void use_utc(void)
{
    /* pkg_dent_cmp_bday() shifts build times by the local GMT offset; pin it
       so the day boundaries below are the same everywhere */
    setenv("TZ", "UTC", 1);
    tzset();
}

/* -T sorts by build *day*: two packages built on the same day compare by
   name, not by their exact build time as -t does */
START_TEST (test_bday_ignores_time_within_day) {
    use_utc();

    struct pkg *late  = mkpkg("late",  NOON(20000) + 6 * 3600);
    struct pkg *early = mkpkg("early", NOON(20000) - 6 * 3600);
    struct pkg_dent a, b;

    init_dent(&a, "a", late);   /* "a" was built later */
    init_dent(&b, "b", early);  /* "b" was built earlier */

    fail_unless(pkg_dent_cmp_bday(&a, &b) < 0,
                "bday must order same-day packages by name");
    fail_unless(pkg_dent_cmp_bday(&b, &a) > 0,
                "bday must order same-day packages by name");
    fail_unless(pkg_dent_cmp_btime(&a, &b) > 0,
                "btime must still order by exact build time");

    pkg_free(late);
    pkg_free(early);
}
END_TEST

START_TEST (test_bday_sorts_by_day) {
    use_utc();

    struct pkg *p1 = mkpkg("z", NOON(20001));
    struct pkg *p2 = mkpkg("y", NOON(20000));
    struct pkg *p3 = mkpkg("a", NOON(20001) + 3600);
    struct pkg_dent dz, dy, da;

    init_dent(&dz, "z", p1);
    init_dent(&dy, "y", p2);
    init_dent(&da, "a", p3);

    tn_array *ents = n_array_new(4, NULL, (tn_fn_cmp)pkg_dent_cmp_bday);
    n_array_push(ents, &dz);
    n_array_push(ents, &dy);
    n_array_push(ents, &da);
    n_array_sort(ents);

    fail_unless(n_array_nth(ents, 0) == &dy, "older day must come first");
    fail_unless(n_array_nth(ents, 1) == &da, "same day sorted by name");
    fail_unless(n_array_nth(ents, 2) == &dz, "same day sorted by name");

    n_array_free(ents);
    pkg_free(p1);
    pkg_free(p2);
    pkg_free(p3);
}
END_TEST

NTEST_RUNNER("dent", test_bday_ignores_time_within_day, test_bday_sorts_by_day);
