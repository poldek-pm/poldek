/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

# define _GNU_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <zlib.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmio.h>
#include <rpm/rpmurl.h>
#include <rpm/rpmmacro.h>
#include <trurl/nassert.h>
#include <trurl/nstr.h>

int zfseek(void *stream, _IO_off64_t *offset, int whence)
{
    int rc;
    off_t off = *offset;
    
    rc = gzseek(stream, off, whence);
    if (rc >= 0)
        rc = 0;

    printf("zfseek (%p, %ld, %lld, %d) = %d\n", stream, off, *offset, whence, rc);
    return rc;
}

cookie_io_functions_t gzio_cookie = {
    (cookie_read_function_t*)gzread,
    (cookie_write_function_t*)gzwrite,
    zfseek,
    (cookie_close_function_t*)gzclose
};



int main(int argc, char *argv[])
{
    char *path = argv[1];
    void *gzstream;
    FILE *stream;
    int c;
    
    
    
    if ((gzstream = gzopen(path, "r")) != NULL) {
        stream = fopencookie(gzstream, "r", gzio_cookie);
        printf("%ld\n", ftell(stream));
        fseek(stream, 1, SEEK_CUR);
        ftell(stream);
        //fseek(stream, 600L, SEEK_SET);
        printf("%ld\n", ftell(stream));
        fread(&c, 1, 1, stream);
        
        printf("%c %ld\n", (unsigned char)c, ftell(stream));
    }
    return 1;
}
