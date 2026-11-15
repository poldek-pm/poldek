#include "test.h"
#include "pkgfl.h"

/* pkgfl_ent_new() trims the leading and trailing slashes of a directory
   name.  The name comes from an rpm header and must be left alone: writing
   a '\0' into it turns the header's string pool into ".../etc\0\0..." and
   the next headerGet() over the same header reads an empty dirname. */
START_TEST (test_dirname_not_modified) {
    char dirname[] = "/etc/openjdk8/";
    tn_alloc *na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    struct pkgfl_ent *ent;

    ent = pkgfl_ent_new(na, dirname, strlen(dirname), 1);
    fail_unless(ent != NULL);
    expect_str(ent->dirname, "etc/openjdk8");
    expect_str(dirname, "/etc/openjdk8/");
    n_alloc_free(na);
}
END_TEST

NTEST_RUNNER("pkgfl", test_dirname_not_modified);
