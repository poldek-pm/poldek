#include <stdio.h>
#include <stdlib.h>

#include <stdio.h>
#include <stdlib.h>

#include "i18n.h"
#include "vfile.h"

void fetch(const char *url)
{
    vfile_fetch("/tmp", url);
}


int main(int argc, char *argv[])
{
    int verbose = 1;
//    const char *up  =  "/a/b/c/d/e/f/ftp.pld.org.pl/PLD-1.0/i686/PLD/RPMS/kernel-video-nvidia-1.0.2880-1@2.2.20_18.i686.rpm";

    const char *up  =  "ftp://mis:dupa@smok/a/b/c/d/e/f/ftp.pld.org.pl/PLD-1.0/i686/PLD/RPMS/kernel-video-nvidia-1.0.2880-1kldd4443.i686.rpm";

    
    const char *u  =  "ftp://ftp.pld.org.pl/PLD-1.0/i686/PLD/RPMS/kernel-video-nvidia-1.0.2880-1@2.2.20_18.i686.rpm";

    const char *uu  =  "/a/v/b/ftp:,,ftp.pld.org.pl,PLD-1.0,i686,PLD,RPMS,kernel-video-nvidia-1.0.2880-1@2.2.20_18.i686.rpm";
    
    vfile_configure(VFILE_CONF_CACHEDIR, ",tmp");
    vfile_verbose = &verbose;

    printf("%s\n", vf_url_slim_s(up,  0));
    printf("%s\n", vf_url_slim_s(u,  0));
    printf("%s\n", vf_url_slim_s(uu,  0));
    

    fetch("ftp://bb/PLD/i686/PLD/RPMS/glibc-2.2.3-3.i686.rpm");
    fetch("ftp://bb/PLD/i686/PLD/RPMS/portmap-5beta-6.i686.rpm");
    fetch("ftp://bb/PLD/i686/PLD/RPMS/nfs-utils-0.3.1-1.i686.rpm");
    return 0;
}
