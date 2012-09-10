/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_POPEN_H
#define POLDEK_POPEN_H

#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef EXPORT
# define EXPORT extern
#endif

/* custom popen(..., "r") */

struct p_open_st {
    FILE    *stream;
    int     fd;
    pid_t   pid;
    char    *cmd;
    int     ec;                 /* exit code */
    char    *errmsg;
};

EXPORT void p_st_init(struct p_open_st *pst);
EXPORT void p_st_destroy(struct p_open_st *pst);

#define P_OPEN_KEEPSTDIN (1 << 0) /* don't close stdin */
#define P_OPEN_OUTPTYS   (1 << 1) /* pass output through pseudo-tty */

EXPORT FILE *p_open(struct p_open_st *pst, unsigned flags, const char *cmd,
             char *const argv[]);

EXPORT int p_close(struct p_open_st *pst);
EXPORT int p_wait(struct p_open_st *pst);

#endif /* POLDEK_POPEN_H */
