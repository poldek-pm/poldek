/* 
  Copyright (C) 2002 Pawel A. Gajda <mis@k2.net.pl>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* $Id$ */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <termios.h>

#include <trurl/nstr.h>
#include <trurl/nassert.h>
#include <trurl/nmalloc.h>

#include "log.h"
#include "i18n.h"

#include "pager.h"




static const char *select_pager_cmd(void) 
{
    static const char *pager_cmd = NULL;
    static int pager_cmd_notfound = 0;
    int n;
    char *cmd, *path[] = {
        "/usr/bin", "/bin", "/sbin", "/usr/sbin",
        "/usr/local/bin", "/usr/local/sbin", NULL
    };

    
    if (pager_cmd_notfound)
        return pager_cmd;
    
    if ((cmd = getenv("PAGER")) == NULL)
        cmd = "less";
    
    else if (*cmd == '\0') {
        pager_cmd_notfound = 1;
        return NULL;
    }
    
    n = 0;
    while (path[n]) {
        char cmdpath[PATH_MAX];
        snprintf(cmdpath, sizeof(cmdpath), "%s/%s", path[n], cmd);

        if (access(cmdpath, R_OK | X_OK) == 0) {
            pager_cmd = n_strdup(cmdpath);
            pager_cmd_notfound = 0;
            break;
        }
        n++;
    }

    if (path[n] == NULL)
        pager_cmd_notfound = 1;

    return pager_cmd;
}


FILE *pager(struct pager *pg)
{
    int    pp[2];
    pid_t  pid;
    const char *cmd;

    pg->stream = NULL;
    pg->pid = 0;
    pg->ec = 0;
    
    if ((cmd = select_pager_cmd()) == NULL)
        return NULL;

    if (!isatty(1))
        return NULL;

    if (access(cmd, R_OK | X_OK) != 0) {
        logn(LOGERR, _("%s: no such file"), cmd);
        return NULL;
    }
    
    if (tcgetattr(STDOUT_FILENO, &pg->_tios) != 0) {
        logn(LOGERR, "tcgetattr(stdout): %m");
        return NULL;
    }

    if (pipe(pp) != 0) {
        logn(LOGERR, "pipe: %m");
        return NULL;
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    if ((pid = fork()) == 0) {
        close(pp[1]);
	dup2(pp[0], 0);
	close(pp[0]);

        execl(cmd, n_basenam(cmd), NULL);
        logn(LOGERR, "execl %s: %m", cmd);
	exit(EXIT_FAILURE);
        
    } else if (pid < 0) {
        logn(LOGERR, "fork %s: %m", cmd);
        
    } else {
        close(pp[0]);
        if ((pg->stream = fdopen(pp[1], "w")) == NULL) {
            logn(LOGERR, "fdopen %d: %m", pp[0]);
            
        } else {
            setvbuf(pg->stream, NULL, _IONBF, 0);
            pg->pid = pid;
        }
    }
    
    if (pg->stream == NULL) {
        close(pp[0]);
        close(pp[1]);
    }
    
    return pg->stream;
}

static
int pager_waitpid(struct pager *pg, int woptions) 
{
    int     st;
    pid_t   pid;
    
    if (pg->pid == 0)
        return pg->ec;
    
    if ((pid = waitpid(pg->pid, &st, woptions)) <= 0)
        return 0;

    pg->pid = 0;
    pg->ec = 0;
    
    if (WIFEXITED(st))
        pg->ec = WEXITSTATUS(st);

    return pg->ec;
}


int pager_exited(struct pager *pg)
{
    pager_waitpid(pg, WNOHANG);
    return pg->pid == 0;       /* finished? */
}


int pager_close(struct pager *pg) 
{
    int rc = 0;

    if (pg->stream) {
        fclose(pg->stream);
        pg->stream = NULL;
    }
    
    if (pg->pid)
        pager_waitpid(pg, 0);
    
    if (tcsetattr(STDOUT_FILENO, TCSANOW, &pg->_tios) != 0 ||
        tcsetattr(STDERR_FILENO, TCSANOW, &pg->_tios) != 0) {
        
        logn(LOGERR, "tcgetattr(stdout): %m");
        rc = -1;
    }

    
    signal(SIGPIPE, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

    if (rc == 0)
        rc = pg->ec;
    
    return rc;
}

