/* $Id$ */
#ifndef POLDEK_SHELL_PAGER_H
#define POLDEK_SHELL_PAGER_H

#include <unistd.h>
#include <termios.h>

struct pager {
    FILE             *stream;
    pid_t            pid;
    struct termios  _tios;
    int              ec; 
};

FILE *pager(struct pager *pg);
int pager_exited(struct pager *pg);
int pager_close(struct pager *pg);

#endif
