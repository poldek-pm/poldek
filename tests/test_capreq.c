/*
  capreq_clone() of a capability made without an allocator copies it in
  one block; the copy has the same name, epoch, version and release
  whatever their length.
*/
#include "test.h"

static void check_clone(int namelen, int32_t epoch, const char *version,
                        const char *release)
{
    char name[512];
    struct capreq *cr, *copy;

    n_assert(namelen < (int)sizeof(name));
    memset(name, 'a', namelen);
    name[0] = '/';
    name[namelen] = '\0';

    cr = capreq_new(NULL, name, epoch, version, release, version ? REL_EQ : 0, 0);
    expect_notnull(cr);
    copy = capreq_clone(NULL, cr);
    expect_notnull(copy);

    expect_int(capreq_name_len(copy), namelen);
    expect_str(capreq_name(copy), name);
    expect_int(capreq_has_epoch(copy) != 0, epoch != 0);
    expect_int(capreq_has_ver(copy) != 0, version != NULL);
    expect_int(capreq_has_rel(copy) != 0, release != NULL);
    if (epoch)
        expect_int(capreq_epoch(copy), epoch);
    if (version)
        expect_str(capreq_ver(copy), version);
    if (release)
        expect_str(capreq_rel(copy), release);
    expect_int(capreq_cmp_name_evr(copy, cr), 0);

    capreq_free(copy);
    capreq_free(cr);
}

/* a file path as a capability: the name alone takes more than 255 bytes
   with the header */
START_TEST(test_clone_long_name) {
    check_clone(300, 0, NULL, NULL);
}
END_TEST

/* a long version: name and version together take more than 255 bytes */
START_TEST(test_clone_long_version) {
    char version[151];

    memset(version, '1', sizeof(version) - 1);
    version[sizeof(version) - 1] = '\0';
    check_clone(150, 0, version, "1");
}
END_TEST

/* epoch, version and release that capreq_new() takes, more than 255
   bytes with the header and a short name */
START_TEST(test_clone_long_evr) {
    char version[225];

    memset(version, '1', sizeof(version) - 1);
    version[sizeof(version) - 1] = '\0';
    check_clone(5, 1, version, "1");
}
END_TEST

/* and a short one, as before */
START_TEST(test_clone_short) {
    check_clone(5, 1, "1.0", "1");
}
END_TEST

NTEST_RUNNER("capreq", test_clone_long_name, test_clone_long_version,
             test_clone_long_evr, test_clone_short);
