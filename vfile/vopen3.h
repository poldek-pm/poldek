/* $Id$ */
#ifndef VFILE_V3OPEN_H
#define VFILE_V3OPEN_H

#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/* custom popen(..., "r") */

#define VOPEN3_SHARE_STDIN  (1 << 0) /* don't close stdin */
#define VOPEN3_SHARE_STDOUT (1 << 1) /* don't close stdin */
#define VOPEN3_SHARE_STDERR (1 << 2) /* pass output through pseudo-tty */

#define VOPEN3_STDIN        (1 << 4)
#define VOPEN3_STDOUT       (1 << 5)
#define VOPEN3_STDERR       (1 << 6) 

struct vopen3_st {
    FILE    *stream_in;
    int     fd_in;

    FILE    *stream_out;
    int     fd_out;

    FILE    *stream_err;
    int     fd_err;

    int     flags;
    pid_t   pid;

    
    char    *cmd;
    char    **argv;
    
    int     (*pfunc)(void*);
    void    *pfunc_arg;

    int     ec;                 /* exit code */
    char    *errmsg;
    int     nread;
    struct  vopen3_st *next;
};

void vopen3_init(struct vopen3_st *st, const char *cmd, char *const argv[]);
void vopen3_init_fn(struct vopen3_st *st, int (*pfunc)(void*), void *pfunc_arg);
void vopen3_destroy(struct vopen3_st *st);


int vopen3_exec(struct vopen3_st *st, unsigned flags);

int vopen3_close(struct vopen3_st *st);
int vopen3_wait(struct vopen3_st *st);

/* st1->out -> st2->in */
int vopen3_chain(struct vopen3_st *st1, struct vopen3_st *st2);
void vopen3_process(struct vopen3_st *st, int verbose_level);

#endif /* POLDEK_V3OPEN_H */
