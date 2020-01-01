#include "test.h"

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

    system("rm -rf /tmp/poldek-tests/");
    const char *path = "/tmp/poldek-tests/bar/baz/var/lib/rpm";

    db = pkgdb_open(pmctx, path, NULL, O_RDONLY, NULL);
    fail_if(db != NULL);
    fail_if(access(path, R_OK) == 0);

    db = pkgdb_open(pmctx, path, NULL, O_RDWR, NULL);
    fail_if(db == NULL);
    fail_if(access(path, R_OK) != 0);

    capreq_new_name_a("/bin/sh", req);
    fail_if(pkgdb_match_req(db, req, 1, NULL));

    pkgdb_close(db);
}
END_TEST


struct test_suite test_suite_pmdb = {
    "PM database",
    {
        { "pm_rpm system database", test_system_rpmdb },
        { "pm_rpm custom database", test_custom_rpmdb },
        { NULL, NULL }
    }
};
