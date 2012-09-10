/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef VFILE_V3OPEN_H
#define VFILE_V3OPEN_H

#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef EXPORT
# define EXPORT extern
#endif

/* custom popen(..., "r") */

#define VOPEN3_NOSTDIN      (1 << 0); /* use /dev/null as stdin */
#define VOPEN3_NOSTDOUT     (1 << 1); /* use /dev/null as stdout */
#define VOPEN3_NOSTDERR     (1 << 2); /* use /dev/null as stdout */

#define VOPEN3_PIPESTDIN    (1 << 3) /* pipe stdin */
#define VOPEN3_PIPESTDOUT   (1 << 4) /* pipe stdout */
#define VOPEN3_PIPESTDERR   (1 << 5) /* pipe stderr */

#define VOPEN3_STDOUTERR    (1 << 6) /* merge stdout && stderr */

struct vopen3_st {
    int     fd_in;
    int     fd_out;
    int     fd_err;

    int     flags;
    pid_t   pid;

    
    char    *cmd;
    char    **argv;
    
    int     (*pfunc)(void*);    /* process function */
    void    *pfunc_arg;

    int     (*grabfunc)(const char *, void*); /* grab output fn */
    void    *grabfunc_arg;

    int     ec;                 /* exit code */
    char    *errmsg;
    int     nread;
    struct  vopen3_st *next;
};
EXPORT void vopen3_init(struct vopen3_st *st, const char *cmd, char *const argv[]);
EXPORT void vopen3_init_fn(struct vopen3_st *st, int (*pfunc)(void*), void *pfunc_arg);
EXPORT void vopen3_set_grabfn(struct vopen3_st *st, int (*func)(const char *, void*),
                       void *arg);
EXPORT void vopen3_destroy(struct vopen3_st *st);

EXPORT int vopen3_st_infd(struct vopen3_st *st);
EXPORT int vopen3_st_outfd(struct vopen3_st *st);


EXPORT int vopen3_exec(struct vopen3_st *st, unsigned flags);

EXPORT int vopen3_close(struct vopen3_st *st);
EXPORT int vopen3_wait(struct vopen3_st *st);

/* st1->out -> st2->in */
EXPORT int vopen3_chain(struct vopen3_st *st1, struct vopen3_st *st2);
EXPORT void vopen3_process(struct vopen3_st *st, int verbose_level);

#endif /* POLDEK_V3OPEN_H */
