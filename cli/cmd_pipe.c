/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <trurl/trurl.h>
#include <sigint/sigint.h>

#include "compiler.h"
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

int cmd_pipe_vprintf(struct cmd_pipe *p, const char *fmt, va_list args)
{
    return n_buf_vprintf(p->nbuf, fmt, args);
}

int cmd_pipe_writeout_fd(struct cmd_pipe *p, int fd)
{
    int n = n_buf_size(p->nbuf);
    return write(fd, n_buf_ptr(p->nbuf), n) == n;
}

static int xargs_packages(struct cmd_pipe *p, tn_array *args)
{
    int i;
    for (i=0; i < n_array_size(p->pkgs); i++) {
        struct pkg *pkg = n_array_nth(p->pkgs, i);
        n_array_push(args, n_strdup(pkg_id(pkg)));
    }
    return n_array_size(p->pkgs);
}

static int xargs_stdout(struct cmd_pipe *p, tn_array *args)
{
    char   *line, c;
    size_t len, i = 0;

    while ((line = n_buf_it_gets(&p->nbuf_it, &len)) && len > 0) {
        c = line[len];
        line[len] = '\0';
        n_array_push(args, n_strdup(line));
        line[len] = c;
        i++;
    }

    return i;
}

tn_array *cmd_pipe_xargs(struct cmd_pipe *p, int pctx)
{
    tn_array *args = n_array_new(64, free, NULL);

    if (pctx == CMD_PIPE_CTX_PACKAGES) {
        if (xargs_packages(p, args) == 0)
            xargs_stdout(p, args);

    } else if (pctx == CMD_PIPE_CTX_ASCII) {
        if (xargs_stdout(p, args) == 0)
            xargs_packages(p, args);
    }

    if (n_array_size(args) == 0)
        n_array_cfree(&args);

    return args;
}
