#include <stdio.h>
#include <stdlib.h>

#include <stdio.h>
#include <stdlib.h>

#include "i18n.h"
#include "vfile.h"

void fetch(const char *url)
{
    vfile_fetch("/tmp", url, VFURL_UNKNOWN);
}

int main(int argc, char *argv[])
{
    int verbose = 1;
    
    vfile_configure("/tmp", 0);
    vfile_verbose = &verbose;

    fetch("ftp://bb/PLD/i686/PLD/RPMS/glibc-2.2.3-3.i686.rpm");
    fetch("ftp://bb/PLD/i686/PLD/RPMS/portmap-5beta-6.i686.rpm");
    fetch("ftp://bb/PLD/i686/PLD/RPMS/nfs-utils-0.3.1-1.i686.rpm");
    return 0;
}
