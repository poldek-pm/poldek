#include <stdio.h>
#include <stdlib.h>

#include <stdio.h>
#include <stdlib.h>

#include "vfile.h"

void fetch(const char *url)
{
    if (vfile_fetch("/tmp", url, -1))
        puts("OK\n");
    else
        puts("FAIL\n");
}

int main(int argc, char *argv[])
{
    int verbose = 1;
    
    vfile_configure("/tmp", 0);
    vfile_verbose = &verbose;

    fetch("ftp://localhost/PLD/i686/PLD/RPMS/nfs-utils-0.3.1-1.i686.rpm");
    fetch("ftp://localhost/PLD/i686/PLD/RPMS/portmap-5beta-6.i686.rpm");
    fetch("ftp://localhost/PLD/i686/PLD/RPMS/nfs-utils-0.3.1-1.i686.rpm");
    return 0;
}
