#include "test.h"

START_TEST (test_valid_path) {
    char *inv_paths[] = {
        "../ala/ma/kota",
        "dupa/blada",
        "/ala/../foo",
        NULL
    };
    char *valid_paths[] = {
        "/",
        "/ala/ma/kota",
        "/dupa/..blada",
        "/ala../foo",
	"/home/foo/.poldek-cache/_www.rpm.xx.redhat-7.3../.vflock__home.foo..poldek-cache..www.rpm.xx.redhat-7.3..",
        NULL
    };
    int i;

    i = 0;
    while (inv_paths[i] != NULL) {
        fail_if(vf_valid_path(inv_paths[i]), 
                "validated invalid '%s'", inv_paths[i]);
        i++;
    }

    i = 0;
    while (valid_paths[i] != NULL) {
        fail_if(!vf_valid_path(valid_paths[i]), 
                "invalid valid '%s'", valid_paths[i]);
        i++;
    }
}
END_TEST


struct test_case test_case_misc = {
    "vf_valid_path", test_valid_path
};
