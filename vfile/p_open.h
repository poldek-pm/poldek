/* $Id$ */
#ifndef POLDEK_POPEN_H
#define POLDEK_POPEN_H

#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

/* custom popen */

struct p_open_st {
    FILE *stream;
    pid_t pid;
    char *cmd;
    char *errmsg;
};

void p_st_init(struct p_open_st *pst);
void p_st_destroy(struct p_open_st *pst);

FILE *p_open(struct p_open_st *pst, const char *cmd, char *const argv[]);
int p_close(struct p_open_st *pst);

#endif /* POLDEK_POPEN_H */
