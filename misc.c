/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_STRSIGNAL
# define _GNU_SOURCE 1  /* for strsignal */
#endif

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
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <trurl/nassert.h>
#include <vfile/p_open.h>

#include "log.h"
#include "misc.h"


const char *tmpdir(void) 
{
    struct stat st;
    static char *tmpdir = NULL;
    char *dir;

    if (tmpdir != NULL)
        return tmpdir;
    
    if ((dir = getenv("TMPDIR")) == NULL)
        tmpdir = "/tmp";
    
    else if (*dir != '/' || strlen(dir) < 4)
        dir = "/tmp";
    
    else {
        char *p;
            
        p = dir + 1;
        while (*p) {
            if (!isalnum(*p) && *p != '/' && *p != '-') {
                tmpdir = "/tmp";
                log(LOGERR, "$TMPDIR (%s) contains non alnum characters, using /tmp\n", dir);
                break;
            }
            p++;
        }

        if (tmpdir == NULL) {
            if (stat(dir, &st) != 0) {
                log(LOGERR, "$TMPDIR (%s): %m, using /tmp\n", dir);
                tmpdir = "/tmp";
                
            } else if (!S_ISDIR(st.st_mode)) {
                log(LOGERR, "$TMPDIR (%s): not a directory, using /tmp\n", dir);
                tmpdir = "/tmp";
            }
        }
    }

    if (tmpdir == NULL)
        tmpdir = dir;

    return tmpdir;
}



char *architecture(void) 
{
    static struct utsname utsn = {
        {'\0'}, {'\0'}, {'\0'}, {'\0'}, {'\0'}, {'\0'}
    };

    if (*utsn.machine == '\0')  
        uname(&utsn);
    return utsn.machine;
}


char *trimslash(char *path) 
{
    if (path) {
        char *p = strchr(path, '\0');
    
        if (p) {
            p--;
            if (*p == '/')
                *p = '\0';
        }
    }
    return path;
}


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
    return stat(path, &st) == 0 &&
        S_ISDIR(st.st_mode) && (st.st_mode & S_IRWXU);
}

void die(void) 
{
    printf("Something wrong, something not quite right, die\n");
    abort();
}

void process_cmd_output(struct p_open_st *st, const char *prefix) 
{
    int c, endl = 1, cnt = 0;
    
    if (prefix == NULL)
        prefix = st->cmd;

    setvbuf(st->stream, NULL, _IONBF, 0);
    while ((c = fgetc(st->stream)) != EOF) {
        
        if (endl) {
            msg(1, "_%s: ", prefix);
            endl = 0;
        }

        msg(1, "_%c", c);
        if (c == '\n' && cnt > 0)
            endl = 1;
        
        cnt++;
    }
}

    
int exec_rpm(const char *cmd, char *const argv[])
{
    int rc, st;
    pid_t pid;

    if (access(cmd, X_OK) != 0) {
        log(LOGERR, "%s: no such file", cmd);
        return -1;
    }
    
    if ((pid = fork()) == 0) {
        execv(cmd, argv);
	exit(EXIT_FAILURE);
        
    } else if (pid < 0) {
        log(LOGERR, "%s: no such file", cmd);
        return -1;
    }

    rc = 0;
    while (wait(&st) > 0) {
        if (WIFEXITED(st)) {
            if (WEXITSTATUS(st) != 0) {
                log(LOGERR, "%s exited with %d\n", cmd, WEXITSTATUS(st));
            }
            
            rc = WEXITSTATUS(st);
            
        } else if (WIFSIGNALED(st)) {
#ifdef HAVE_STRSIGNAL
            log(LOGERR, "%s terminated by signal %s\n", cmd,
                strsignal(WTERMSIG(st)));
#else
            log(LOGERR, "%s terminated by signal %d\n", cmd,
                WTERMSIG(st));
#endif        
            rc = -1;
        } else {
            log(LOGERR, "%s died under inscrutable circumstances\n", cmd);
            rc = -1;
        }
    }
    
    return rc;
}





