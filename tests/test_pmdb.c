#include "test.h"

START_TEST (test_pmdb_rpm) {
    char buf[PATH_MAX], tmp[PATH_MAX];
    struct pm_ctx *pmctx;
    struct pkgdb *db;
    struct capreq *req;

    
    pmmodule_init();
    
    pmctx = pm_new("rpm");
    fail_if(pmctx == NULL);

    db = pkgdb_open(pmctx, "/tmp/foo/dupa/blada", NULL, O_RDONLY, NULL);
    fail_if(db != NULL);

    db = pkgdb_open(pmctx, "/", NULL, O_RDONLY, NULL);
    fail_if(db == NULL);

    capreq_new_name_a("/bin/sh", req);
    fail_if(!pkgdb_match_req(db, req, 1, NULL));

    capreq_new_name_a("/bin/dupa/foo/blada", req);
    fail_if(pkgdb_match_req(db, req, 1, NULL));
    
    pkgdb_close(db);
    
}
END_TEST


struct test_suite test_suite_pmdb = {
    "PM database", 
    {
        { "pm_rpm", test_pmdb_rpm },
        { NULL, NULL }
    }
};
