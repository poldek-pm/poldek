#include "test.h"
#include <sys/utsname.h>

#include "pkgcmp.h"
#include "pkg_ver_cmp.h"

extern int poldek_conf_MULTILIB;

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

/* colored (color != 0) and uncolored (color == 0, e.g. -devel packages
   without ELF binaries) package of given arch */
static struct pkg *new_colored_pkg(const char *arch, uint32_t color)
{
    struct pkg *p = pkg_new("a", 0, "1", "1", arch, "linux");
    p->color = color;
    return p;
}

/* Arch colors come from rpm's archcolor: table; unknown archs report <= 0
   and then rpm (and so poldek) has no opinion about them.  Cross-arch
   expectations are rpm-version dependent, so skip them in that case. */
static int arch_colors_known(const char *a1, const char *a2)
{
    return pm_architecture_color(a1) > 0 && pm_architecture_color(a2) > 0;
}

START_TEST (test_colored_like_colored) {
    struct utsname un;
    if (!arch_testable(&un))
        return;

    struct poldek_ctx *ctx = setup();

    int multilib_save = poldek_conf_MULTILIB;
    poldek_conf_MULTILIB = 1;

    struct pkg *p64 = new_colored_pkg("x86_64", 1);
    struct pkg *p32 = new_colored_pkg("i686", 3);

    /* both colored -> color bits decide, arch is irrelevant */
    fail_unless(pkg_is_colored_like(p64, p32) == 1,
                "expected %s and %s colored like", pkg_id(p64), pkg_id(p32));
    fail_unless(pkg_is_colored_like(p32, p64) == 1,
                "expected %s and %s colored like", pkg_id(p32), pkg_id(p64));

    p32->color = 2;              /* no common color bits with 1 */
    fail_unless(pkg_is_colored_like(p64, p32) == 0,
                "expected %s and %s not colored like", pkg_id(p64), pkg_id(p32));

    pkg_free(p32);
    pkg_free(p64);

    poldek_conf_MULTILIB = multilib_save;
    teardown(ctx);
}
END_TEST

START_TEST (test_colored_like_uncolored_same_arch) {
    struct utsname un;
    if (!arch_testable(&un))
        return;

    struct poldek_ctx *ctx = setup();

    int multilib_save = poldek_conf_MULTILIB;
    poldek_conf_MULTILIB = 1;

    /* uncolored packages of the very same arch are still interchangeable */
    struct pkg *u1 = new_colored_pkg("x86_64", 0);
    struct pkg *u2 = new_colored_pkg("x86_64", 0);
    struct pkg *c1 = new_colored_pkg("x86_64", 1);

    fail_unless(pkg_is_colored_like(u1, u2) == 1,
                "expected %s and %s colored like", pkg_id(u1), pkg_id(u2));
    fail_unless(pkg_is_colored_like(u1, c1) == 1,
                "expected %s and %s colored like", pkg_id(u1), pkg_id(c1));
    fail_unless(pkg_is_colored_like(c1, u1) == 1,
                "expected %s and %s colored like", pkg_id(c1), pkg_id(u1));

    pkg_free(c1);
    pkg_free(u2);
    pkg_free(u1);

    poldek_conf_MULTILIB = multilib_save;
    teardown(ctx);
}
END_TEST

/* uncolored packages of different arch families (e.g. xz-devel.x86_64 vs
   xz-devel.x32) install to different paths and must not be treated as
   interchangeable -- see poldek-nocolor-cmp.patch.  This mirrors rpm's
   addSelfErasures() check based on rpmGetArchColor(). */
START_TEST (test_colored_like_uncolored_other_arch) {
    struct utsname un;
    if (!arch_testable(&un))
        return;

    struct poldek_ctx *ctx = setup();

    int multilib_save = poldek_conf_MULTILIB;
    poldek_conf_MULTILIB = 1;

    struct pkg *ud64 = new_colored_pkg("x86_64", 0);
    struct pkg *ud32 = new_colored_pkg("i686", 0);
    struct pkg *c64 = new_colored_pkg("x86_64", 1);

    if (arch_colors_known("x86_64", "i686")) {
        fail_unless(pkg_is_colored_like(ud64, ud32) == 0,
                    "expected %s and %s not colored like", pkg_id(ud64), pkg_id(ud32));
        fail_unless(pkg_is_colored_like(ud32, ud64) == 0,
                    "expected %s and %s not colored like", pkg_id(ud32), pkg_id(ud64));

        /* one colored, one uncolored, different archs */
        fail_unless(pkg_is_colored_like(c64, ud32) == 0,
                    "expected %s and %s not colored like", pkg_id(c64), pkg_id(ud32));
        fail_unless(pkg_is_colored_like(ud32, c64) == 0,
                    "expected %s and %s not colored like", pkg_id(ud32), pkg_id(c64));
    }

    /* noarch has no arch color of its own (ac=0), so it stays replaceable
       against any arch -- exactly what rpm does */
    struct pkg *una = new_colored_pkg("noarch", 0);
    fail_unless(pkg_is_colored_like(una, una) == 1,
                "expected %s and %s colored like", pkg_id(una), pkg_id(una));
    fail_unless(pkg_is_colored_like(una, ud32) == 1,
                "expected noarch %s colored like %s", pkg_id(una), pkg_id(ud32));
    fail_unless(pkg_is_colored_like(ud32, una) == 1,
                "expected %s colored like noarch %s", pkg_id(ud32), pkg_id(una));

    /* ... though noarch is arch compatible */
    fail_unless(pkg_is_arch_compat(una, ud32) == 1,
                "expected %s arch compatible with %s", pkg_id(una), pkg_id(ud32));

    pkg_free(una);
    pkg_free(c64);
    pkg_free(ud32);
    pkg_free(ud64);

    poldek_conf_MULTILIB = multilib_save;
    teardown(ctx);
}
END_TEST

START_TEST (test_colored_like_no_multilib) {
    struct utsname un;
    if (!arch_testable(&un))
        return;

    struct poldek_ctx *ctx = setup();

    int multilib_save = poldek_conf_MULTILIB;
    poldek_conf_MULTILIB = 0;

    /* without multilib everything is of the same kind */
    struct pkg *ud64 = new_colored_pkg("x86_64", 0);
    struct pkg *ud32 = new_colored_pkg("i686", 0);

    fail_unless(pkg_is_colored_like(ud64, ud32) == 1,
                "expected %s and %s colored like", pkg_id(ud64), pkg_id(ud32));
    fail_unless(pkg_is_kind_of(ud32, ud64) == 1,
                "expected %s to be kind of %s", pkg_id(ud32), pkg_id(ud64));

    pkg_free(ud32);
    pkg_free(ud64);

    poldek_conf_MULTILIB = multilib_save;
    teardown(ctx);
}
END_TEST

START_TEST (test_kind_of_uncolored) {
    struct utsname un;
    if (!arch_testable(&un))
        return;

    struct poldek_ctx *ctx = setup();

    int multilib_save = poldek_conf_MULTILIB;
    poldek_conf_MULTILIB = 1;

    /* same name, same arch (even if colors differ) */
    struct pkg *ud64 = new_colored_pkg("x86_64", 0);
    struct pkg *c64 = new_colored_pkg("x86_64", 1);

    fail_unless(pkg_is_kind_of(c64, ud64) == 1,
                "expected %s to be kind of %s", pkg_id(c64), pkg_id(ud64));

    /* same name, different arch family, one of them uncolored */
    struct pkg *ud32 = new_colored_pkg("i686", 0);

    if (arch_colors_known("x86_64", "i686")) {
        fail_unless(pkg_is_kind_of(ud32, ud64) == 0,
                    "expected %s not to be kind of %s", pkg_id(ud32), pkg_id(ud64));
        fail_unless(pkg_is_kind_of(ud64, ud32) == 0,
                    "expected %s not to be kind of %s", pkg_id(ud64), pkg_id(ud32));
    }

    pkg_free(ud32);
    pkg_free(c64);
    pkg_free(ud64);

    poldek_conf_MULTILIB = multilib_save;
    teardown(ctx);
}
END_TEST

NTEST_RUNNER("cmp", test_arch_cmp, test_multi_arch_cmp, test_arch_sort,
             test_colored_like_colored, test_colored_like_uncolored_same_arch,
             test_colored_like_uncolored_other_arch, test_colored_like_no_multilib,
             test_kind_of_uncolored);
