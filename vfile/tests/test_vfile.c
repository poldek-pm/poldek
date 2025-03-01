#include "test.h"

void append(const char *path, int vft_io)
{
    struct vfile *vf;

    vf = vfile_open(path, vft_io, VFM_APPEND);
    fail_if(vf == NULL);
    fail_if(n_stream_write(vf->vf_tnstream, "foo\n", 4) != 4);
    vfile_close(vf);
}

START_TEST (test_vfile_append) {
    int ec;

    char *gzpath = strdup(NTEST_TMPPATH("tmp.txt.gz"));
    append(gzpath, VFT_TRURLIO);
    append(gzpath, VFT_TRURLIO);
    append(gzpath, VFT_TRURLIO);

    char *path = strdup(NTEST_TMPPATH("tmp.txt"));
    append(path, VFT_TRURLIO);
    append(path, VFT_TRURLIO);
    append(path, VFT_TRURLIO);


    char cmd[1024];
    n_snprintf(cmd, sizeof(cmd), "zdiff %s %s\n", gzpath, path);
    ec = system(cmd);
    fail_if(ec != 0);
}
END_TEST

START_TEST (test_valid_path) {
    char *inv_paths[] = {
        "../ala/ma/kota",
        "foo/bar",
        "/ala/../foo",
        NULL
    };
    char *valid_paths[] = {
        "/",
        "/ala/ma/kota",
        "/foo/..bar",
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


NTEST_RUNNER("vfile", test_vfile_append, test_valid_path);
