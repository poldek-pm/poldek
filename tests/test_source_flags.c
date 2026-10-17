#include "test.h"
#include "poldek.h"
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
            "pri  = 5\n"
            "compr = zst\n");
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
        PKGSOURCE_COMPR | PKGSOURCE_AUTOUPA;
    fail_unless(src->flags == expected, "flags 0x%x, expected 0x%x",
                src->flags, expected);

    n_array_free(sources);
    poldek_free(ctx);
    unlink(path);
}
END_TEST

NTEST_RUNNER("source_flags", test_source_flags);
