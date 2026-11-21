#include "test.h"
#include "fileindex.h"

/* more owners than the 16 bits of a package's reference count */
#define NOWNERS 65536

/* A path of N owners makes N(N-1)/2 pairs.  The report kept a record of
   each pair with a reference to both packages before it dropped the pairs
   outside the packages given, so a package in more than 65533 pairs ended
   poldek in pkg_link().  Only the pair of the two packages given is to be
   reported. */
START_TEST (test_conflicts_of_widely_shared_path) {
    struct file_index *fi = file_index_new(16);
    tn_alloc *na = n_alloc_new(64, TN_ALLOC_OBSTACK);
    tn_array *owners = n_array_new(NOWNERS, (tn_fn_free)pkg_free, NULL);
    tn_array *given = n_array_new(2, NULL, (tn_fn_cmp)pkg_cmp_name_evr);
    void *dir = file_index_add_dirname(fi, "opt");
    char name[16];
    int i;

    for (i = 0; i < NOWNERS; i++) {
        struct pkg *pkg;

        snprintf(name, sizeof(name), "p%05d", i);
        pkg = pkg_new(name, 0, "1", "1", "noarch", "linux");
        n_array_push(owners, pkg);
        file_index_add_basename(fi, dir,
                                flfile_new(na, 0, S_IFREG | 0644, "x", 1, NULL, 0),
                                pkg);
    }
    n_array_push(given, n_array_nth(owners, 1));
    n_array_push(given, n_array_nth(owners, NOWNERS - 2));
    n_array_sort(given);

    expect_int(file_index_report_conflicts(fi, given), 1);

    n_array_free(given);
    file_index_free(fi);
    n_alloc_free(na);
    n_array_free(owners);
}
END_TEST

NTEST_RUNNER("fileindex", test_conflicts_of_widely_shared_path);
