
#ifndef POLDEK_SHELL_PAGER_H
#define POLDEK_SHELL_PAGER_H


struct pager {
    FILE *stream;
    pid_t pid;
};

FILE *pager(struct pager *pg);
int pager_close(struct pager *pg);

#endif
