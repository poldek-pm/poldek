/* $Id$ */
#ifndef POLDEK_POPEN_H
#define POLDEK_POPEN_H

#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/* custom popen */

struct p_open_st {
    FILE    *stream;
    int     fd;
    pid_t   pid;
    char    *cmd;
    char    *errmsg;
};

void p_st_init(struct p_open_st *pst);
void p_st_destroy(struct p_open_st *pst);

#define P_OPEN_KEEPSTDIN (1 << 0)

FILE *p_open(struct p_open_st *pst, unsigned flags, const char *cmd,
             char *const argv[]);

int p_close(struct p_open_st *pst);

FILE *pty_open(struct p_open_st *pst, const char *cmd, char *const argv[]);

#endif /* POLDEK_POPEN_H */
