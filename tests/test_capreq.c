/*
  capreq_new() takes version and release of up to CAPREQ_VR_MAX characters
  together. capreq_clone() of a capability made without an allocator copies
  it in one block; the copy has the same name, epoch, version and release
  whatever their length.
*/
#include "test.h"
#include "capreq_intern.h"

/* a capability "a" of a version of verlen characters */
static struct capreq *new_versioned(int32_t epoch, int verlen,
                                    const char *release)
{
    char version[256];

    n_assert(verlen < (int)sizeof(version));
    memset(version, '1', verlen);
    version[verlen] = '\0';

    return capreq_new(NULL, "a", epoch, version, release, REL_EQ, 0);
}

/* version and release are counted, the epoch is not; stored, a capability
   with 250..254 bytes of them hung split_capreq() */
START_TEST(test_new_vr_limit) {
    struct capreq *cr;

    expect_notnull(cr = new_versioned(0, CAPREQ_VR_MAX - 1, "1"));
    capreq_free(cr);
    expect_null(new_versioned(0, CAPREQ_VR_MAX, "1"));

    expect_notnull(cr = new_versioned(0, CAPREQ_VR_MAX, NULL));
    capreq_free(cr);
    expect_null(new_versioned(0, CAPREQ_VR_MAX + 1, NULL));

    expect_notnull(cr = new_versioned(1, CAPREQ_VR_MAX - 1, "1"));
    capreq_free(cr);
    expect_null(new_versioned(1, CAPREQ_VR_MAX, "1"));

    expect_null(new_versioned(0, 246, "2"));
}
END_TEST

/* nor a package of such a version: no capability of its own version could
   be made */
START_TEST(test_pkg_new_vr_limit) {
    char version[CAPREQ_VR_MAX + 1];
    struct pkg *pkg;

    memset(version, '1', sizeof(version) - 1);
    version[sizeof(version) - 1] = '\0';
    expect_null(pkg_new("a", 0, version, "1", "noarch", "linux"));

    version[CAPREQ_VR_MAX - 1] = '\0';
    pkg = pkg_new("a", 1, version, "1", "noarch", "linux");
    expect_notnull(pkg);
    pkg_add_selfcap(pkg);
    expect_int(n_array_size(pkg->caps), 1);
    pkg_free(pkg);
}
END_TEST

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
    char version[121];

    memset(version, '1', sizeof(version) - 1);
    version[sizeof(version) - 1] = '\0';
    check_clone(200, 0, version, "1");
}
END_TEST

/* the longest epoch, version and release capreq_new() takes */
START_TEST(test_clone_long_evr) {
    char version[CAPREQ_VR_MAX];

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

NTEST_RUNNER("capreq", test_new_vr_limit, test_pkg_new_vr_limit,
             test_clone_long_name, test_clone_long_version,
             test_clone_long_evr, test_clone_short);
