#include "test.h"

int test_append(const char *path, int vft_io) 
{
    struct vfile *vf;
    
    vf = vfile_open(path, vft_io, VFM_APPEND);
    fail_if(vf == NULL);
    fail_if(n_stream_write(vf->vf_tnstream, "foo\n", 4) != 4);
    vfile_close(vf);
    return 1;
}

START_TEST (test_vfile_append) {
    int ec;
    
    test_append("tmp.txt.gz", VFT_TRURLIO);
    test_append("tmp.txt.gz", VFT_TRURLIO);
    test_append("tmp.txt.gz", VFT_TRURLIO);
    
    test_append("tmp.txt", VFT_TRURLIO);
    test_append("tmp.txt", VFT_TRURLIO);
    test_append("tmp.txt", VFT_TRURLIO);
    
    ec = system("zdiff tmp.txt tmp.txt.gz");
    fail_if(ec != 0);
    
    
}
END_TEST

struct test_suite test_suite_vfile = {
    "vfile", 
    {
        { "append", test_vfile_append },
        { NULL, NULL }
    }
};
