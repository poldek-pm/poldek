/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@pld-linux.org)
 
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

#include "compiler.h"
#include "poldek.h"
#define POCLIDEK_ITSELF
#include "poclidek.h"

struct poldek_ctx     *ctx;
struct poclidek_ctx  *cctx;

int init(void) 
{
    ctx = poldek_new(0);
    poldek_load_config(ctx, "test_cli.rc", NULL, 0);
    poldek_setup(ctx);
    cctx = poclidek_new(ctx);
    return  poclidek_load_packages(cctx, 1);
}

int test_01(void)
{
    init();
    poclidek_free(cctx);
    poldek_free(ctx);
    return 1;
}


int test_02(void)
{
    struct poclidek_rcmd  *cmd;
    init();
    
    cmd = poclidek_rcmd_new(cctx, NULL);
    if (poclidek_rcmd_execline(cmd, "ls xmms*")) {
        tn_array *pkgs;
        tn_buf  *buf;
        tn_buf_it it;
        char *p, line[1024];
        int len, n = 0;

        pkgs = poclidek_rcmd_get_packages(cmd);
        buf = poclidek_rcmd_get_buf(cmd);
        n_buf_it_init(&it, buf);
        n = 0;
        printf("buf %d\n", n_buf_size(buf));
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
    poldeklib_init();
    test_01();
}
