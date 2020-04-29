#include "test.h"
#include <sys/utsname.h>

struct poldek_ctx *setup(void)
{
    poldeklib_init();
    struct poldek_ctx *ctx = poldek_new(0);
    poldek_setup(ctx);          /* initialize rpm */
    return ctx;
}

void teardown(struct poldek_ctx *ctx)
{
    poldek_free(ctx);
    poldeklib_destroy();
}

int arch_testable(struct utsname *un) {
    if (uname(un) == 0 && n_str_in(un->machine, "i686", "x86_64", NULL))
        return 1;

    return 0;
}

START_TEST (test_arch_cmp) {
    struct utsname un;
    if (!arch_testable(&un))
        return;

    struct poldek_ctx *ctx = setup();

    struct pkg *p32 = pkg_new("a", 0, "1", "1", "i686", "linux");
    struct pkg *p64 = pkg_new("a", 0, "1", "1", "x86_64", "linux");

    if (n_str_eq(un.machine, pkg_arch(p32))) {
        //printf("cmp %d\n", pkg_cmp_arch(p32, p64));
        fail_unless(pkg_cmp_arch(p32, p64) > 0,
                    "expected %s > %s", pkg_id(p32), pkg_id(p64));
    } else if (n_str_eq(un.machine, pkg_arch(p64))) {
        //printf("cmp %d\n", pkg_cmp_arch(p64, p32));
        fail_unless(pkg_cmp_arch(p64, p32) > 0,
                    "expected %s > %s", pkg_id(p64), pkg_id(p32));
    }

    teardown(ctx);
}
END_TEST

START_TEST (test_multi_arch_cmp) {
    struct utsname un;
    if (!arch_testable(&un))
        return;

    struct poldek_ctx *ctx = setup();
    struct pkg *pp[4];

    pp[0] = pkg_new("a", 0, "1", "1", "i686", "linux");
    pp[1] = pkg_new("a", 0, "1", "1", "x86_64", "linux");
    pp[2] = pkg_new("a", 0, "1", "1", "x32", "linux");
    pp[3] = pkg_new("a", 0, "1", "1", "xfoo", "linux");

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (i != j) {
                fail_if(pkg_cmp_arch(pp[i], pp[j]) == 0,
                        "expected %s != %s", pkg_id(pp[i]), pkg_id(pp[j]));
            }
        }
    }

    teardown(ctx);
}
END_TEST


START_TEST (test_arch_sort) {
    struct utsname un;
    if (!arch_testable(&un))
        return;

    struct poldek_ctx *ctx = setup();

    struct pkg *p0 = pkg_new("a", 0, "1", "1", "noarch", "linux");
    struct pkg *p32 = pkg_new("a", 0, "1", "1", "i686", "linux");
    struct pkg *p64 = pkg_new("a", 0, "1", "1", "x86_64", "linux");

    tn_array *pkgs = pkgs_array_new(4);
    n_array_push(pkgs, p0);
    n_array_push(pkgs, p32);
    n_array_push(pkgs, p64);

    struct pkg *expected = NULL;
    if (n_str_eq(un.machine, pkg_arch(p32))) {
        expected = p32;
    } else if (n_str_eq(un.machine, pkg_arch(p64))) {
        expected = p64;
    }

    n_array_sort(pkgs);
    struct pkg *first = n_array_nth(pkgs, 0);
#if 0
    for (int i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        printf(" %d. %s\n", i, pkg_id(pkg));
    }
#endif
    fail_if(first != expected,
            "expected %s first, got %s", pkg_id(expected), pkg_id(first));

    teardown(ctx);
}
END_TEST

NTEST_RUNNER("cmp", test_arch_cmp, test_multi_arch_cmp, test_arch_sort);
