/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@k2.net.pl>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/


#if HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_STRSIGNAL
# define _GNU_SOURCE 1
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
#include <sys/stat.h>
#include <fcntl.h>

#if HAVE_FORKPTY
# include <pty.h>
# include <termios.h>
#endif

#include <trurl/nassert.h>

#include "i18n.h"
#include "p_open.h"

void p_st_init(struct p_open_st *pst) 
{
    memset(pst, 0,  sizeof(*pst));
    pst->stream = NULL;
    pst->cmd = NULL;
    pst->errmsg = NULL;
}

void p_st_destroy(struct p_open_st *pst) 
{
    if (pst->stream) {
        fclose(pst->stream);
        pst->stream = NULL;
    }
    
    if (pst->cmd) {
        free(pst->cmd);
        pst->cmd = NULL;
    }
    
    if (pst->errmsg) {
        free(pst->errmsg);
        pst->errmsg = NULL;
    }
}


FILE *p_open(struct p_open_st *pst, unsigned flags, const char *cmd,
             char *const argv[])
{
    int    pp[2];
    pid_t  pid;
    char   errmsg[1024];

    n_assert(pst->stream == NULL);
    
    if (access(cmd, R_OK | X_OK) != 0) {
        snprintf(errmsg, sizeof(errmsg), _("%s: no such file"), cmd);
        pst->errmsg = strdup(errmsg);
        return NULL;
    }
    
    if (pipe(pp) != 0) {
        snprintf(errmsg, sizeof(errmsg), "pipe: %m");
        pst->errmsg = strdup(errmsg);
        return NULL;
    }
    
    if ((pid = fork()) == 0) {
        if ((flags & P_OPEN_KEEPSTDIN) == 0) {
            int fd = open("/dev/null", O_RDWR);
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        
        close(pp[0]);

	dup2(pp[1], STDOUT_FILENO);
	dup2(pp[1], STDERR_FILENO);
	close(pp[1]);

        execv(cmd, argv);
	exit(EXIT_FAILURE);
        
    } else if (pid < 0) {
        snprintf(errmsg, sizeof(errmsg), "fork %s: %m", cmd);
        pst->errmsg = strdup(errmsg);
        
    } else {
        close(pp[1]);
        pst->fd = pp[0];
        if ((pst->stream = fdopen(pp[0], "r"))) {
            setvbuf(pst->stream, NULL, _IONBF, 0);
            pst->pid = pid;
            pst->cmd = strdup(cmd);
        }
    }

    if (pst->stream == NULL) {
        close(pp[0]);
        close(pp[1]);
    }
    
    return pst->stream;
}


int p_close(struct p_open_st *pst) 
{
    int st, rc = -1;
    char errmsg[1024];

    if (pst->errmsg)
        free(pst->errmsg);
    
    pst->errmsg = NULL;
    
    if (pst->pid == 0)
        return 0;
    
    waitpid(pst->pid, &st, 0);
    
    if (WIFEXITED(st)) {
        rc = WEXITSTATUS(st);
        
    } else if (WIFSIGNALED(st)) {
#ifdef HAVE_STRSIGNAL
        snprintf(errmsg, sizeof(errmsg), _("%s terminated by signal %s"),
                 pst->cmd, strsignal(WTERMSIG(st)));
#else
        snprintf(errmsg, sizeof(errmsg), _("%s terminated by signal %d"),
                 pst->cmd, WTERMSIG(st));
#endif        
        pst->errmsg = strdup(errmsg);
        
    } else {
        snprintf(errmsg, sizeof(errmsg),
                 _("%s (%d) died under inscrutable circumstances"),
                 pst->cmd, pst->pid);
        pst->errmsg = strdup(errmsg);
    }

    return rc;
}


#ifndef HAVE_FORKPTY

FILE *pty_open(struct p_open_st *pst, const char *cmd, char *const argv[]) 
{
    return p_open(pst, 0, cmd, argv);
}

#else 

FILE *pty_open(struct p_open_st *pst, const char *cmd, char *const argv[])
{
    struct termios  termios;
    struct winsize  winsize;
    int             fd;
    pid_t           pid;
    char            errmsg[1024];

    if (!isatty(1))
        return p_open(pst, 0, cmd, argv);
    
    if (tcgetattr(STDOUT_FILENO, &termios) != 0) {
        snprintf(errmsg, sizeof(errmsg), "tcgetattr(1): %m");
        pst->errmsg = strdup(errmsg);
        return NULL;
    }
    
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsize) != 0) {
        snprintf(errmsg, sizeof(errmsg), "ioctl(1, TIOCGWINSZ): %m");
        pst->errmsg = strdup(errmsg);
        return NULL;
    }
    
    
    if (access(cmd, R_OK | X_OK) != 0) {
        snprintf(errmsg, sizeof(errmsg), _("%s: no such file"), cmd);
        pst->errmsg = strdup(errmsg);
        return NULL;
    }
    
    if ((pid = forkpty(&fd, NULL, &termios, &winsize)) == 0) {
        close(STDIN_FILENO);
        execv(cmd, argv);
	exit(EXIT_FAILURE);
        
    } else if (pid < 0) {
        snprintf(errmsg, sizeof(errmsg), "fork %s: %m", cmd);
        pst->errmsg = strdup(errmsg);
        return NULL;
        
    } else {
        pst->fd = fd;
        pst->stream = fdopen(fd, "r");
        setvbuf(pst->stream, NULL, _IONBF, 0);
        pst->pid = pid;
        pst->cmd = strdup(cmd);
    }
    
    return pst->stream;
}
#endif
