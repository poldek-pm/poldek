#include "test.h"
#include "poldek.h"
#include "poldek_intern.h"
#include "pkgdir/source.h"

/* a source's flags hold only PKGSOURCE_* bits: its options and autoupa (on by default) */
START_TEST (test_source_flags) {
    const char *path = "poldek_test_source_flags.conf";
    FILE *f = fopen(path, "w");
    fail_if(f == NULL, "cannot open %s for write", path);
    fprintf(f,
            "[source]\n"
            "name = testsrc\n"
            "type = pndir\n"
            "url  = file:///tmp/poldek-test-nonexistent\n"
            "pri  = -5\n"
            "compr = zst\n"
            "noautoup = y\n"
            "hold = foo\n"
            "hold = bar\n");
    fclose(f);

    poldeklib_init();
    struct poldek_ctx *ctx = poldek_new(0);
    fail_if(ctx == NULL, "poldek_new failed");
    fail_if(poldek_load_config(ctx, path, NULL, 0) == 0,
            "load_config failed");
    fail_if(poldek_setup(ctx) == 0, "poldek_setup failed");

    tn_array *sources = poldek_get_sources(ctx);
    fail_if(sources == NULL || n_array_size(sources) != 1, "1 source expected");

    struct source *src = n_array_nth(sources, 0);
    unsigned expected = PKGSOURCE_NAMED | PKGSOURCE_TYPE | PKGSOURCE_PRI |
        PKGSOURCE_COMPR | PKGSOURCE_AUTOUPA | PKGSOURCE_NOAUTOUP;
    fail_unless(src->flags == expected, "flags 0x%x, expected 0x%x",
                src->flags, expected);
    fail_unless(src->pri == -5, "pri %d, expected -5", src->pri);

    int i, foo = 0, bar = 0;
    fail_unless(src->hold_patterns != NULL &&
                n_array_size(src->hold_patterns) == 2,
                "2 holds expected, got %d",
                src->hold_patterns ? n_array_size(src->hold_patterns) : -1);
    for (i = 0; i < n_array_size(src->hold_patterns); i++) {
        const char *hold = n_array_nth(src->hold_patterns, i);
        foo += strcmp(hold, "foo") == 0;
        bar += strcmp(hold, "bar") == 0;
    }
    fail_unless(foo == 1 && bar == 1, "source holds foo=%d bar=%d", foo, bar);

    foo = bar = 0;
    for (i = 0; i < n_array_size(ctx->ts->hold_patterns); i++) {
        const char *hold = n_array_nth(ctx->ts->hold_patterns, i);
        foo += strcmp(hold, "testsrc:foo") == 0;
        bar += strcmp(hold, "testsrc:bar") == 0;
    }
    fail_unless(foo == 1 && bar == 1, "ts holds foo=%d bar=%d", foo, bar);

    n_array_free(sources);
    poldek_free(ctx);
    unlink(path);
}
END_TEST

/* poldek_select_sources() picks loaded sources by name pattern without
   changing their flags */
START_TEST (test_select_sources) {
    const char *path = "poldek_test_source_select.conf";
    FILE *f = fopen(path, "w");
    fail_if(f == NULL, "cannot open %s for write", path);
    fprintf(f,
            "[source]\n"
            "name = one\n"
            "type = dir\n"
            "path = /tmp/poldek-test-nonexistent-one\n"
            "noautoup = y\n"
            "\n"
            "[source]\n"
            "name = two\n"
            "type = dir\n"
            "path = /tmp/poldek-test-nonexistent-two\n");
    fclose(f);

    poldeklib_init();
    struct poldek_ctx *ctx = poldek_new(0);
    fail_if(ctx == NULL, "poldek_new failed");
    fail_if(poldek_load_config(ctx, path, NULL, 0) == 0,
            "load_config failed");
    fail_if(poldek_setup(ctx) == 0, "poldek_setup failed");

    tn_array *patterns = n_array_new(2, NULL, NULL);
    n_array_push(patterns, "on*");
    tn_array *selected = poldek_select_sources(ctx, patterns);
    fail_unless(selected != NULL && n_array_size(selected) == 1,
                "on* should select one source");
    struct source *src = n_array_nth(selected, 0);
    fail_unless(strcmp(src->name, "one") == 0, "got '%s'", src->name);
    fail_unless((src->flags & PKGSOURCE_NOAUTOUP) != 0,
                "selecting by name must not change the source flags");
    n_array_free(selected);

    n_array_push(patterns, "two");
    selected = poldek_select_sources(ctx, patterns);
    fail_unless(selected != NULL && n_array_size(selected) == 2,
                "two patterns should select both sources");
    n_array_free(selected);
    n_array_free(patterns);

    poldek_free(ctx);
    unlink(path);
}
END_TEST

NTEST_RUNNER("source", test_source_flags, test_select_sources);
