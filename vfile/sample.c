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
    tn_array *protocols;

    protocols = n_array_new(4, NULL, NULL);
    n_array_push(protocols, "http");
    n_array_push(protocols, "ftp");
    if (!vfile_register_ext_handler("wget", protocols, 
                                    "/usr/bin/wget -N -P %d %Pn")) {
        printf("bad handler def\n");
        return;
    }

    if (vfile_fetch("/tmp", url))
        puts("OK\n");
    else
        puts("FAIL\n");
}

void fetch(const char *url)
{
    if (vfile_fetch("/tmp", url))
        puts("OK\n");
    else
        puts("FAIL\n");
}

                         

int main(int argc, char *argv[])
{
    int verbose = 10;
    
    vfile_verbose = &verbose;
    vfile_configure(VFILE_CONF_CACHEDIR, "/tmp");
    
    
    
    
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
