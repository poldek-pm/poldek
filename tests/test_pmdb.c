#include "test.h"
#include "pkgfl.h"

START_TEST (test_system_rpmdb) {
    //char buf[PATH_MAX], tmp[PATH_MAX];
    struct pm_ctx *pmctx;
    struct pkgdb *db;
    struct capreq *req;


    pmmodule_init();

    pmctx = pm_new("rpm");
    fail_if(pmctx == NULL);

    db = pkgdb_open(pmctx, "/", NULL, O_RDONLY, NULL);
    fail_if(db == NULL);

    capreq_new_name_a("/bin/sh", req);
    fail_if(!pkgdb_match_req(db, req, 1, NULL));

    capreq_new_name_a("/bin/foo/bar/baz.sh", req);
    fail_if(pkgdb_match_req(db, req, 1, NULL));

    pkgdb_close(db);

}
END_TEST


START_TEST (test_custom_rpmdb) {
    //char buf[PATH_MAX], tmp[PATH_MAX];
    struct pm_ctx *pmctx;
    struct pkgdb *db;
    struct capreq *req;

    pmmodule_init();

    pmctx = pm_new("rpm");
    fail_if(pmctx == NULL);

    system("rm -rf /tmp/poldek-unit-tests/");
    const char *path = "/tmp/poldek-unit-tests/bar/baz";
    const char *dbpath = "/tmp/poldek-unit-tests/bar/baz" "/var/lib/rpm";

    db = pkgdb_open(pmctx, path, NULL, O_RDONLY, NULL);
    fail_if(db != NULL);
    fail_if(access(dbpath, R_OK) == 0);

    db = pkgdb_open(pmctx, path, NULL, O_RDWR, NULL);
    fail_if(db == NULL);
    fail_if(access(dbpath, R_OK) != 0);

    capreq_new_name_a("/bin/sh", req);
    fail_if(pkgdb_match_req(db, req, 1, NULL));

    pkgdb_close(db);
}
END_TEST

#ifdef HAVE_RPMORG              /* tests the header loader of the rpm.org module */
/* rpm stores LONGFILESIZES instead of FILESIZES in a v6 package and for a file
   of 4 GiB or more; poldek keeps sizes in 32 bits, so 5 GiB becomes UINT32_MAX */
START_TEST (test_ldhdr_long_file_sizes) {
    const char *basenames[] = { "small", "big" };
    const char *dirnames[] = { "/opt/" };
    const char *links[] = { "", "" };
    uint32_t diridxs[] = { 0, 0 };
    uint16_t modes[] = { S_IFREG | 0644, S_IFREG | 0644 };
    uint64_t sizes[] = { 1, 5368709120ULL }, size = 5368709121ULL;
    tn_alloc *na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    Header h = headerNew();
    struct pkgfl_ent *ent;
    struct pkg *pkg;

    headerPutString(h, RPMTAG_NAME, "big");
    headerPutString(h, RPMTAG_VERSION, "1");
    headerPutString(h, RPMTAG_RELEASE, "1");
    headerPutString(h, RPMTAG_ARCH, "noarch");
    headerPutString(h, RPMTAG_OS, "linux");
    headerPutString(h, RPMTAG_SOURCERPM, "big-1-1.src.rpm");
    headerPutStringArray(h, RPMTAG_BASENAMES, basenames, 2);
    headerPutStringArray(h, RPMTAG_DIRNAMES, dirnames, 1);
    headerPutUint32(h, RPMTAG_DIRINDEXES, diridxs, 2);
    headerPutUint16(h, RPMTAG_FILEMODES, modes, 2);
    headerPutUint64(h, RPMTAG_LONGFILESIZES, sizes, 2);
    headerPutStringArray(h, RPMTAG_FILELINKTOS, links, 2);
    headerPutUint64(h, RPMTAG_LONGSIZE, &size, 1);

    pkg = pm_rpm_ldhdr(na, h, "big-1-1.noarch.rpm", 0, PKG_LDWHOLE);
    fail_if(pkg == NULL);
    fail_if(pkg->fl == NULL || n_tuple_size(pkg->fl) != 1);

    fail_unless(pkg->size == UINT32_MAX, "size %u", pkg->size);

    ent = n_tuple_nth(pkg->fl, 0);
    fail_unless(ent->items == 2, "%d files", ent->items);
    for (int i = 0; i < ent->items; i++) {
        struct flfile *f = ent->files[i];
        uint32_t expected = strcmp(f->basename, "big") == 0 ? UINT32_MAX : 1;
        fail_unless(f->size == expected, "%s: size %u", f->basename, f->size);
    }

    pkg_free(pkg);
    headerFree(h);
    n_alloc_free(na);
}
END_TEST

/* a DIRINDEXES entry that names no directory makes the file list unusable, as for rpm */
START_TEST (test_ldhdr_dirindex_out_of_range) {
    const char *basenames[] = { "a", "b" };
    const char *dirnames[] = { "/opt/" };
    uint32_t diridxs[] = { 0, 1 };
    tn_alloc *na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    Header h = headerNew();
    tn_tuple *fl = NULL;

    headerPutStringArray(h, RPMTAG_BASENAMES, basenames, 2);
    headerPutStringArray(h, RPMTAG_DIRNAMES, dirnames, 1);
    headerPutUint32(h, RPMTAG_DIRINDEXES, diridxs, 2);

    fail_unless(pm_rpm_ldhdr_fl(na, &fl, h, PKGFL_ALL, "bad") == -1);
    fail_unless(fl == NULL);

    headerFree(h);
    n_alloc_free(na);
}
END_TEST

/* a file tag without an entry for each file makes the file list unusable, as for rpm */
START_TEST (test_ldhdr_file_tag_count) {
    const char *basenames[] = { "a", "b" };
    const char *dirnames[] = { "/opt/" };
    uint32_t diridxs[] = { 0, 0 };
    uint16_t modes[] = { S_IFREG | 0644 };
    tn_alloc *na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    Header h = headerNew();
    tn_tuple *fl = NULL;

    headerPutStringArray(h, RPMTAG_BASENAMES, basenames, 2);
    headerPutStringArray(h, RPMTAG_DIRNAMES, dirnames, 1);
    headerPutUint32(h, RPMTAG_DIRINDEXES, diridxs, 2);
    headerPutUint16(h, RPMTAG_FILEMODES, modes, 1);

    fail_unless(pm_rpm_ldhdr_fl(na, &fl, h, PKGFL_ALL, "bad") == -1);
    fail_unless(fl == NULL);

    headerFree(h);
    n_alloc_free(na);
}
END_TEST

NTEST_RUNNER("PM database", test_system_rpmdb, test_custom_rpmdb,
             test_ldhdr_long_file_sizes, test_ldhdr_dirindex_out_of_range,
             test_ldhdr_file_tag_count);
#else
NTEST_RUNNER("PM database", test_system_rpmdb, test_custom_rpmdb);
#endif
