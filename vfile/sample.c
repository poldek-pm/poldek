#include <stdio.h>
#include <stdlib.h>

#include <stdio.h>
#include <stdlib.h>

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
    int verbose = 1;
    vfile_configure("/tmp", -1);
    vfile_verbose = &verbose;

    fetch("ftp://localhost/bigg");
    
    //dump_file("ftp://ftp.pld.org.pl/PLD-1.0/i686/PLD/RPMS/tocfile.lst");
    //fetch_ext("http://sunsite.icm.edu.pl/index.html");
    //dump_file("/tmp/index.html");
    return 0;
}
