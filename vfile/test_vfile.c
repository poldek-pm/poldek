/*
  $Id$
*/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <trurl/trurl.h>
#define ENABLE_VFILE_TRURLIO
#include "vfile.h"

int test_append(const char *path, int vft_io) 
{
    struct vfile *vf;
    
    vf = vfile_open(path, vft_io, VFM_APPEND);
    n_assert(vf);
    n_assert(n_stream_write(vf->vf_tnstream, "foo\n", 4) == 4);
    vfile_close(vf);
    return 1;
}


int main(int argc, char *argv[])
{
    test_append("/tmp/test_vfile.txt.gz", VFT_TRURLIO);
    test_append("/tmp/test_vfile.txt.gz", VFT_TRURLIO);
    test_append("/tmp/test_vfile.txt.gz", VFT_TRURLIO);
    
    test_append("/tmp/test_vfile.txt", VFT_TRURLIO);
    test_append("/tmp/test_vfile.txt", VFT_TRURLIO);
    test_append("/tmp/test_vfile.txt", VFT_TRURLIO);
    
}

