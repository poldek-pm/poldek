/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#define _GNU_SOURCE 1           /* for strsignal */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <trurl/nassert.h>
#include "log.h"
#include "misc.h"


char *next_token(char **str, char delim, int *toklen) 
{
    char *p, *token;

    if (*str == NULL)
        return NULL;
    
    
    if ((p = strchr(*str, delim)) == NULL) {
        token = *str;
        if (toklen)
            *toklen = strlen(*str);
        *str = NULL;
        
    } else {
        *p = '\0';
        
        if (toklen)
            *toklen = p - *str;
        p++;
        while(isspace(*p))
            p++;
        token = *str;
        *str = p;
    }
    
    return token;
}


int is_rwxdir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode) &&
        access(path, R_OK | W_OK | X_OK) == 0;
}


FILE *p_open(struct runst *rst, const char *cmd, char *const argv[])
{
    int pp[2];
    pid_t pid;

    
    if (access(cmd, X_OK) != 0) {
        log(LOGERR, "%s: no such file");
        return NULL;
    }

    
    if (pipe(pp) != 0) {
        log(LOGERR, "pipe: %m\n");
        return NULL;
    }
    
    rst->pid = 0;
    rst->stream = NULL;
    
    if ((pid = fork()) == 0) {
	close(pp[0]);
	dup2(pp[1], 1);
	dup2(pp[1], 2);
	close(pp[1]);

        execv(cmd, argv);
	exit(EXIT_FAILURE);
        
    } else if (pid < 0) {
        log(LOGERR, "fork: %m\n");
        return NULL;
        
    } else {
        close(pp[1]);
        rst->stream = fdopen(pp[0], "r");
        rst->pid = pid;
        rst->cmd = strdup(cmd);
    }
    
    return rst->stream;
}


int p_close(struct runst *rst) 
{
    int st, rc = -1;

    fclose(rst->stream);
    rst->stream = NULL;

    if (rst->pid == 0)
        return 0;
    
    waitpid(rst->pid, &st, 0);
    
    if (WIFEXITED(st)) {
        if (WEXITSTATUS(st) != 0)
            log(LOGERR, "%s exited with %d\n", rst->cmd, WEXITSTATUS(st));
        rc = WEXITSTATUS(st);
        
    } else if (WIFSIGNALED(st)) {
        log(LOGERR, "%s terminated by signal %d\n", rst->cmd,
            strsignal(WTERMSIG(st)));
        
    } else {
        log(LOGERR, "have no idea what happen with %s\n", rst->cmd);
    }

    free(rst->cmd);
    rst->cmd = NULL;
    return rc;
}


int p_process_cmd(struct runst *st, const char *prefix) 
{
    int c, endl = 1, cnt = 0;
    
    if (prefix == NULL)
        prefix = st->cmd;

    setvbuf(st->stream, NULL, _IONBF, 0);
    while ((c = fgetc(st->stream)) != EOF) {
        if (endl) {
            msg(1, "_%s: ", prefix, cnt);
            endl = 0;
        }
        
        msg(1, "_%c", c);
        if (c == '\n' && cnt > 0)
            endl = 1;

        cnt++;
    }
    
    return 1;
}


void errdie(void) 
{
    abort();
}


    
