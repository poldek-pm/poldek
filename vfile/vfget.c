#include <stdio.h>
#include <stdlib.h>

#include <stdio.h>
#include <stdlib.h>

#include "vfile.h"

int main(int argc, char *argv[])
{
    int verbose = 2;
    char *url, *destdir;

    vfile_configure(VFILE_CONF_VERBOSE, &verbose);
    vfile_setup();
    
    if (argc < 2) {
        printf("Usage: vfget URL [DESTDIR]\n");
        exit(EXIT_SUCCESS);
    }
    url = argv[1];
    destdir = argc > 2 ? argv[2] : "/tmp";
    vf_fetch(url, destdir, 0, 0, 0);
    return 0;
}
