/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <locale.h>

#include <trurl/nassert.h>
#include <trurl/nmalloc.h>

#include "poldek.h"
#include "poclidek.h"

int test_01(void)
{
    struct poldek_ctx     *ctx;
    struct poclidek_ctx  *cctx;

    ctx = poldek_new(0);
    poldek_load_config(ctx, "test_cli.rc", 0);
    poldek_setup(ctx);

    cctx = poclidek_new(ctx);

    
    poclidek_load_packages(cctx);
    poclidek_free(cctx);
    poldek_free(ctx);
    return 1;
}


int test_02(void)
{
    struct poldek_ctx     *ctx;
    struct poclidek_ctx   *cctx;
    struct poclidek_rcmd  *cmd;
    
    ctx = poldek_new(0);
    poldek_load_config(ctx, "test_cli.rc", 0);
    poldek_setup(ctx);

    cctx = poclidek_new(ctx);
    poclidek_load_packages(cctx);

    cmd = poclidek_rcmd_new(cctx, NULL);
    if (poclidek_rcmd_execline(cmd, "ls xmms*")) {
        tn_buf_it it;
        char *p, line[1024];
        int len, n = 0;

        n_buf_it_init(&it, cmd->rbuf);
        n = 0;
        printf("pkgs %d\n", n_array_size(cmd->rpkgs));
        printf("buf %d\n", n_buf_size(cmd->rbuf));
        while ((p = n_buf_it_gets(&it, &len))) {
            if (len > 1024)
                len = 1023;
            memcpy(line, p, len);
            line[len] = '\0';
            printf("%d: %s\n", n++, line);
        }
        
    }
    
    poclidek_rcmd_free(cmd);
    
    
    poclidek_free(cctx);
    poldek_free(ctx);
    return 1;
}

int main(int argc, char *argv[]) 
{
    setlocale(LC_MESSAGES, "");
    setlocale(LC_CTYPE, "");
    test_02();
}
