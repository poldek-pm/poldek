#include <stdio.h>
#include <stdlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include "i18n.h"
#include "vfile.h"

void dump_file(const char *url)
{
    struct vfile *vf;
    char buf[1024];
    
    vf = vfile_open(url, VFT_STDIO, VFM_RO | VFM_NORM);
    if (vf == NULL)
        return;

    while (fgets(buf, sizeof(buf), vf->vf_stream)) 
        printf("%s", buf);

    vfile_close(vf);
}

void fetch_ext(const char *url)
{
    if (!vfile_register_ext_handler(VFURL_FTP | VFURL_HTTP,
                                    "/usr/bin/wget -N -P %d %Pn")) {
        printf("bad handler def\n");
        return;
    }

    if (vfile_fetch("/tmp", url, -1))
        puts("OK\n");
    else
        puts("FAIL\n");
}

void fetch(const char *url)
{
    if (vfile_fetch("/tmp", url, vfile_url_type(url)))
        puts("OK\n");
    else
        puts("FAIL\n");
}

                         

int main(int argc, char *argv[])
{
    void *fn;
    int verbose = 10;
    
    vfile_verbose = &verbose;
    vfile_configure("/tmp", -1);
    
    
    
    
    while (1) {
        printf("verbose = %d\n", *vfile_verbose);
        fetch("ftp://localhost/bigg");
        unlink("/tmp/bigg");
    }
    
//dump_file("ftp://ftp.pld.org.pl/PLD-1.0/i686/PLD/RPMS/tocfile.lst");
    //fetch_ext("http://sunsite.icm.edu.pl/index.html");
    //dump_file("/tmp/index.html");
    return 0;
}
