/* 
  Copyright (C) 2000 - 2004 Pawel A. Gajda (mis@k2.net.pl)
 
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <trurl/trurl.h>
#include <sigint/sigint.h>

#include "i18n.h"
#include "log.h"
#include "cli.h"
#include "cmd_pipe.h"


struct cmd_pipe *cmd_pipe_new(void) 
{
    struct cmd_pipe *p;
    
    p = n_calloc(sizeof(*p), 1);
    p->pkgs = pkgs_array_new(64);
    p->nbuf = n_buf_new(4096);
    n_buf_it_init(&p->nbuf_it, p->nbuf);
    return p;
}

void cmd_pipe_free(struct cmd_pipe *p)
{
    DBGF("%p\n", p);
    
    if (p->_refcnt > 0) {
        p->_refcnt--;
        return;
    }
    
    n_array_free(p->pkgs);
    n_buf_free(p->nbuf);
    memset(p, 0, sizeof(*p));
    free(p);
}

struct cmd_pipe *cmd_pipe_link(struct cmd_pipe *p) 
{
    p->_refcnt++;
    return p;
}



int cmd_pipe_writepkg(struct cmd_pipe *p, struct pkg *pkg) 
{
    n_array_push(p->pkgs, pkg_link(pkg));
    return 1;
}

struct pkg *cmd_pipe_getpkg(struct cmd_pipe *p)
{
    if (n_array_size == 0)
        return NULL;
    
    if (p->nread_pkgs >= n_array_size(p->pkgs))
        return NULL;

    return n_array_nth(p->pkgs, p->nread_pkgs++);
}

int cmd_pipe_writeline(struct cmd_pipe *p, const char *line, int len)
{
    int n;
    
    if (len <= 0)
        len = strlen(line);
    
    n = n_buf_write(p->nbuf, line, len);
    if (n == len)
        p->nwritten++;
    return len;
}

int cmd_pipe_printf(struct cmd_pipe *p, const char *fmt, ...)
{
    va_list  args;
    int n;
    
    va_start(args, fmt);
    n = n_buf_vprintf(p->nbuf, fmt, args);
    va_end(args);
    
    return n;
}

int cmd_pipe_vprintf(struct cmd_pipe *p, const char *fmt, va_list args)
{
    return n_buf_vprintf(p->nbuf, fmt, args);
}

char *cmd_pipe_getline(struct cmd_pipe *p, char *line, int size)
{
    int len = 0;
    char *ptr;
    
    if (n_buf_size(p->nbuf) > 0) {
        ptr = n_buf_it_gets(&p->nbuf_it, &len);
        if (size < len)
            len = size - 1;
        memcpy(line, ptr, len);
        line[len] = '\0';
        return line;
    }
    return NULL;
}

int cmd_pipe_size(struct cmd_pipe *p, int pctx)
{
    if (pctx == CMD_PIPE_CTX_PACKAGES)
        return n_array_size(p->pkgs);
    return p->nwritten;
}

int cmd_pipe_writeout_fd(struct cmd_pipe *p, int fd)
{
    int n = n_buf_size(p->nbuf);
    return write(fd, n_buf_ptr(p->nbuf), n) == n;
}



tn_array *cmd_pipe_xargs(struct cmd_pipe *p, int pctx)
{
    tn_array *args = n_array_new(64, free, NULL);

    if (pctx == CMD_PIPE_CTX_PACKAGES) {
        int i;
        for (i=0; i < n_array_size(p->pkgs); i++)
            n_array_push(args, n_strdup(pkg_snprintf_s(n_array_nth(p->pkgs, i))));
        
    } else {
        char *line;
        int  len;
        
        while ((line = n_buf_it_gets(&p->nbuf_it, &len)) && len > 0) {
            line[len] = '\0';
            n_array_push(args, n_strdup(line));
            line[len] = '\n';
        }
    }
    return args;
}



