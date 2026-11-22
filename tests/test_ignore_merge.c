#include "test.h"
#include "poldek.h"
#include "pkgdir/pkgdir.h"
#include "pkgdir/source.h"
#include "pkg.h"
#include "pkgmisc.h"

static int list_contains(tn_array *list, const char *s)
{
    int i;
    for (i = 0; i < n_array_size(list); i++)
        if (n_str_eq((const char *)n_array_nth(list, i), s))
            return 1;
    return 0;
}

START_TEST (test_ignore_merge) {
    const char *path = "poldek_test_ignore_merge.conf";
    FILE *f = fopen(path, "w");
    fail_if(f == NULL, "cannot open %s for write", path);
    fprintf(f,
            "[global]\n"
            "ignore = foo* baz*\n"
            "\n"
            "[source]\n"
            "name = testsrc\n"
            "type = pndir\n"
            "url  = file:///tmp/poldek-test-nonexistent\n"
            "ignore = bar*\n");
    fclose(f);

    poldeklib_init();
    struct poldek_ctx *ctx = poldek_new(0);
    fail_if(ctx == NULL, "poldek_new failed");
    fail_if(poldek_load_config(ctx, path, NULL, 0) == 0,
            "load_config failed");
    fail_if(poldek_setup(ctx) == 0, "poldek_setup failed");

    tn_array *sources = poldek_get_sources(ctx);
    fail_if(sources == NULL, "no sources?");

    struct source *src = NULL;
    int i;
    for (i = 0; i < n_array_size(sources); i++) {
        struct source *s = n_array_nth(sources, i);
        if (s->name && n_str_eq(s->name, "testsrc")) {
            src = s;
            break;
        }
    }
    fail_if(src == NULL, "testsrc not found in sources");

    fail_unless(list_contains(src->ign_patterns, "foo*"),
                "global 'foo*' missing from merged ign_patterns");
    fail_unless(list_contains(src->ign_patterns, "baz*"),
                "global 'baz*' missing from merged ign_patterns");
    fail_unless(list_contains(src->ign_patterns, "bar*"),
                "per-source 'bar*' missing from merged ign_patterns");

    n_array_free(sources);
    poldek_free(ctx);
    unlink(path);
}
END_TEST

/* a mask is matched with the whole name-version-release, also of a package
   with no arch and of one whose source has a name longer than the matcher's
   buffer */
START_TEST (test_mask_long_name_no_arch) {
    struct pkgscore_s psc;
    struct pkgdir dir;
    struct pkg *pkg;
    char name[601], mask[640];

    pkg = pkg_new("foo", 0, "1", "1", NULL, NULL);
    fail_if(pkg == NULL, "pkg_new failed");
    pkgscore_match_init(&psc, pkg);
    expect_int(pkgscore_match(&psc, "foo"), 1);
    expect_int(pkgscore_match(&psc, "foo-1-1"), 1);
    expect_int(pkgscore_match(&psc, "foo-1"), 0);

    memset(name, 'n', sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    memset(&dir, 0, sizeof(dir));
    dir.name = name;
    pkg->pkgdir = &dir;
    pkgscore_match_init(&psc, pkg);
    expect_int(pkgscore_match(&psc, "foo"), 1);
    expect_int(pkgscore_match(&psc, "foo-1-1"), 1);
    expect_int(pkgscore_match(&psc, "foo-1"), 0);
    n_snprintf(mask, sizeof(mask), "%s:foo", name);
    expect_int(pkgscore_match(&psc, mask), 1);
    n_snprintf(mask, sizeof(mask), "%s:foo-1-1", name);
    expect_int(pkgscore_match(&psc, mask), 1);
    n_snprintf(mask, sizeof(mask), "%s:foo-1", name);
    expect_int(pkgscore_match(&psc, mask), 0);
    pkg->pkgdir = NULL;
    pkg_free(pkg);
}
END_TEST

NTEST_RUNNER("ignore_merge", test_ignore_merge, test_mask_long_name_no_arch);
