/* $Id$ */
#ifndef POLDEK_POPEN_H
#define POLDEK_POPEN_H

#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/* custom popen(..., "r") */

struct p_open_st {
    FILE    *stream;
    int     fd;
    pid_t   pid;
    char    *cmd;
    int     ec;                 /* exit code */
    char    *errmsg;
};

void p_st_init(struct p_open_st *pst);
void p_st_destroy(struct p_open_st *pst);

#define P_OPEN_KEEPSTDIN (1 << 0) /* don't close stdin */
#define P_OPEN_OUTPTYS   (1 << 0) /* pass output through pseudo-tty */

FILE *p_open(struct p_open_st *pst, unsigned flags, const char *cmd,
             char *const argv[]);

int p_close(struct p_open_st *pst);
int p_wait(struct p_open_st *pst);

#endif /* POLDEK_POPEN_H */
