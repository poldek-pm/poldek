/* $Id$ */
#ifndef VFILE_V3OPEN_H
#define VFILE_V3OPEN_H

#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

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
void vopen3_init(struct vopen3_st *st, const char *cmd, char *const argv[]);
void vopen3_init_fn(struct vopen3_st *st, int (*pfunc)(void*), void *pfunc_arg);
void vopen3_set_grabfn(struct vopen3_st *st, int (*func)(const char *, void*),
                       void *arg);
void vopen3_destroy(struct vopen3_st *st);

int vopen3_st_infd(struct vopen3_st *st);
int vopen3_st_outfd(struct vopen3_st *st);


int vopen3_exec(struct vopen3_st *st, unsigned flags);

int vopen3_close(struct vopen3_st *st);
int vopen3_wait(struct vopen3_st *st);

/* st1->out -> st2->in */
int vopen3_chain(struct vopen3_st *st1, struct vopen3_st *st2);
void vopen3_process(struct vopen3_st *st, int verbose_level);

#endif /* POLDEK_V3OPEN_H */
