/* $Id$ */
#ifndef POLDEK_MISC_H
#define POLDEK_MISC_H

#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

char *next_token(char **str, char delim, int *toklen);
int is_rwxdir(const char *path);


/* custom popen */

struct runst {
    FILE *stream;
    pid_t pid;
    char *cmd;
};

FILE *p_open(struct runst *rst, const char *cmd, char *const argv[]);
int p_close(struct runst *rst);
int p_process_cmd(struct runst *st, const char *prefix);


inline static int validstr(const char *str) 
{
    while (*str) {
        if (isspace(*str) || *str == '*' || *str == '?' || *str == '&')
            return 0;
        str++;
    }
    return 1;
}

extern int mem_info_verbose;
void print_mem_info(const char *prefix);
void mem_info(int level, const char *msg);

#endif /* POLDEK_MISC_H */
