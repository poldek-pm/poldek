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

#if HAVE_OPENPTY
# include <pty.h>
# include <termios.h>
#endif

#include <trurl/nassert.h>

#include "i18n.h"
#include "p_open.h"

#define P_OPEN_EXITED   (1 << 16) 

static
FILE *pty_open(struct p_open_st *pst, unsigned flags, const char *cmd,
               char *const argv[]);

static
FILE *pp_open(struct p_open_st *pst, unsigned flags, const char *cmd,
              char *const argv[]);


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
#ifdef HAVE_OPENPTY         
    if (flags & P_OPEN_OUTPTYS)
        return pty_open(pst, flags, cmd, argv);
#endif
    
    return pp_open(pst, flags, cmd, argv);
}


static void p_dupnull(int fdno, unsigned p_open_flags) 
{
    switch (fdno) {
        case STDIN_FILENO:
            if ((p_open_flags & P_OPEN_KEEPSTDIN) == 0) {
                int fd;
                if ((fd = open("/dev/null", O_RDONLY)) < 0) {
                    fprintf(stderr, "open /dev/null: %m\n");
                    return;
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            break;
            
        default:
            n_assert(0);
    }
}


static
FILE *pp_open(struct p_open_st *pst, unsigned flags, const char *cmd,
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
        p_dupnull(STDIN_FILENO, flags);
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


static
int p_waitpid(struct p_open_st *pst, int woptions) 
{
    int st, rc = -1;
    char errmsg[1024];
    pid_t pid;


    if (pst->pid == 0)          /* exited */
        return pst->ec;
    
    if (pst->errmsg)
        free(pst->errmsg);
    
    pst->errmsg = NULL;
    
    
    if ((pid = waitpid(pst->pid, &st, woptions)) <= 0)
        return 0;
    
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

    pst->ec = rc;
    pst->pid = 0;
    return rc;
}


int p_wait(struct p_open_st *pst) 
{
    p_waitpid(pst, WNOHANG);
    return pst->pid == 0;       /* finished? */
}


int p_close(struct p_open_st *pst) 
{
    p_waitpid(pst, 0);
    return pst->ec;
}


#ifdef HAVE_OPENPTY
pid_t forkptys(int *master, struct termios *tios, struct winsize *wsize) 
{
    int slave;
    pid_t pid;
    
    
    if (openpty(master, &slave, NULL, tios, wsize) != 0)
        return -1;
    
    if ((pid = fork()) == 0) {
        close(*master);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        return 0;
    }
    return pid;
}

static
FILE *pty_open(struct p_open_st *pst, unsigned flags, const char *cmd,
               char *const argv[])
{
    struct termios  termios;
    struct winsize  winsize;
    int             fd;
    pid_t           pid;
    char            errmsg[1024];


    if (!isatty(STDOUT_FILENO))
        return pp_open(pst, flags, cmd, argv);

    pst->stream = NULL;
    
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
    
    if ((pid = forkptys(&fd, &termios, &winsize)) == 0) {
        p_dupnull(STDIN_FILENO, flags);
        execv(cmd, argv);
	exit(EXIT_FAILURE);
        
    } else if (pid < 0) {
        snprintf(errmsg, sizeof(errmsg), "fork %s: %m", cmd);
        pst->errmsg = strdup(errmsg);
        
        
    } else {
        pst->fd = fd;
        pst->stream = fdopen(fd, "r");
        setvbuf(pst->stream, NULL, _IONBF, 0);
        pst->pid = pid;
        pst->cmd = strdup(cmd);
    }
    
    return pst->stream;
}

#endif /* HAVE_OPENPTY */
