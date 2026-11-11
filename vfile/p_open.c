/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/


#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_STRSIGNAL
# define _GNU_SOURCE 1
#endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#if HAVE_SYS_PRCTL_H
# include <sys/prctl.h>
#endif

#if HAVE_PTY_H
# include <pty.h>
#elif HAVE_LIBUTIL_H           /* FreeBSD */
# include <libutil.h>
#elif HAVE_UTIL_H
# include <util.h>
#endif
#include <termios.h>

#include <trurl/nassert.h>
#include <trurl/nmalloc.h>

#include "compiler.h"
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
    /* a caller must p_st_destroy() and p_st_init() before it opens again:
       a stream, a command name or a message here is left over from an
       earlier attempt and would be lost or written over */
    n_assert(pst->stream == NULL);
    n_assert(pst->cmd == NULL);
    n_assert(pst->errmsg == NULL);

    if (flags & P_OPEN_OUTPTYS)
        return pty_open(pst, flags, cmd, argv);

    return pp_open(pst, flags, cmd, argv);
}


/* the child runs none of poldek's handlers, so a SIGTERM before its exec
   ends it; exec_command() lets the signals in after the child's setup.
   *EXECFD gets the child's or the parent's end of a pipe the exec closes */
static pid_t fork_command(sigset_t *mask, int *execfd)
{
    sigset_t all;
    pid_t pid;
#ifdef PR_SET_PDEATHSIG
    pid_t parent = getpid();
#endif
    int sig, ep[2];

    if (pipe2(ep, O_CLOEXEC) != 0)
        return -1;

    sigfillset(&all);
    sigprocmask(SIG_BLOCK, &all, mask);
    if ((pid = fork()) != 0) {
        int err = errno;

        sigprocmask(SIG_SETMASK, mask, NULL);
        close(ep[1]);
        if (pid < 0) {
            close(ep[0]);
            errno = err;        /* a failed close() would clobber it */
        } else {
            *execfd = ep[0];
        }
        return pid;
    }

    close(ep[0]);
    *execfd = ep[1];

#ifdef PR_SET_PDEATHSIG
    /* the command ends with poldek, a SIGKILL or an OOM kill included;
       a SIGTERM lets rpm stop where it can */
    if (prctl(PR_SET_PDEATHSIG, SIGTERM) == 0 && getppid() != parent)
        _exit(EXIT_FAILURE);        /* poldek ended before prctl() */
#endif

    for (sig = 1; sig < NSIG; sig++) {
        struct sigaction sa;

        /* sa_handler shares its storage with sa_sigaction: both kinds */
        if (sigaction(sig, NULL, &sa) == 0 &&
            sa.sa_handler != SIG_DFL && sa.sa_handler != SIG_IGN)
            signal(sig, SIG_DFL);
    }
    return 0;
}

/* the child ends here: pp_open() and pty_open() go on as the parent.
   A failed exec writes its errno to EXECFD; _exit(), as exit() would flush
   the stdio buffers the child got from poldek and write them again */
static __attribute__((noreturn))
void exec_command(const char *cmd, char *const argv[], const sigset_t *mask,
                  int execfd)
{
    ssize_t n;
    int err;

    sigprocmask(SIG_SETMASK, mask, NULL);
    execv(cmd, argv);
    err = errno;
    n = write(execfd, &err, sizeof(err));
    (void)n;                    /* if it fails, the exit code still tells */
    _exit(EXIT_FAILURE);
}

/* whether the command's exec failed; if so, its child is reaped and errmsg
   says why */
static int exec_failed(struct p_open_st *pst, int execfd, pid_t pid,
                       const char *cmd)
{
    char errmsg[1024];
    ssize_t n;
    int err;

    while ((n = read(execfd, &err, sizeof(err))) < 0 && errno == EINTR)
        ;
    close(execfd);
    if (n != sizeof(err))
        return 0;

    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
        ;
    errno = err;
    snprintf(errmsg, sizeof(errmsg), "execv %s: %m", cmd);
    pst->errmsg = n_strdup(errmsg);
    return 1;
}

/* the parent's side of fork_command(): PST reads the command's output from
   FD, or, with no command started, has no stream and says why */
static FILE *command_started(struct p_open_st *pst, pid_t pid, int execfd,
                             int fd, const char *cmd)
{
    char errmsg[1024];

    if (pid < 0) {
        snprintf(errmsg, sizeof(errmsg), "cannot start %s: %m", cmd);
        pst->errmsg = n_strdup(errmsg);
    }

    if (pid < 0 || exec_failed(pst, execfd, pid, cmd)) {
        fclose(pst->stream);
        pst->stream = NULL;
        return NULL;
    }

    pst->fd = fd;
    pst->pid = pid;
    pst->cmd = n_strdup(cmd);
    return pst->stream;
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
    int    pp[2], execfd = -1;
    pid_t  pid;
    sigset_t mask;
    char   errmsg[1024];

    n_assert(pst->stream == NULL);

    if (access(cmd, R_OK | X_OK) != 0) {
        snprintf(errmsg, sizeof(errmsg), _("%s: no such file"), cmd);
        pst->errmsg = n_strdup(errmsg);
        return NULL;
    }

    if (pipe(pp) != 0) {
        snprintf(errmsg, sizeof(errmsg), "pipe: %m");
        pst->errmsg = n_strdup(errmsg);
        return NULL;
    }

    /* before fork(), so that a NULL return means nothing was started */
    if ((pst->stream = fdopen(pp[0], "r")) == NULL) {
        snprintf(errmsg, sizeof(errmsg), "fdopen: %m");
        pst->errmsg = n_strdup(errmsg);
        close(pp[0]);
        close(pp[1]);
        return NULL;
    }
    setvbuf(pst->stream, NULL, _IONBF, 0);

    if ((pid = fork_command(&mask, &execfd)) == 0) {
        p_dupnull(STDIN_FILENO, flags);
        if (flags & P_OPEN_SETSID)
            setsid();
        if (flags & P_OPEN_IGNSIGINT)
            signal(SIGINT, SIG_IGN);
        close(pp[0]);

	dup2(pp[1], STDOUT_FILENO);
	dup2(pp[1], STDERR_FILENO);
	close(pp[1]);

        exec_command(cmd, argv, &mask, execfd);
    }

    close(pp[1]);
    return command_started(pst, pid, execfd, pp[0], cmd);
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


    if ((pid = waitpid(pst->pid, &st, woptions)) <= 0) {
        /* with SIGCHLD ignored the kernel reaps the command at its end,
           its exit status lost */
        if (pid < 0 && errno == ECHILD)
            pst->pid = 0;
        return 0;
    }

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
        pst->errmsg = n_strdup(errmsg);

    } else {
        snprintf(errmsg, sizeof(errmsg),
                 _("%s (%d) died under inscrutable circumstances"),
                 pst->cmd, pst->pid);
        pst->errmsg = n_strdup(errmsg);
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


static pid_t forkptys(int master, int slave, sigset_t *mask, int *execfd)
{
    pid_t pid;

    if ((pid = fork_command(mask, execfd)) == 0) {
        close(master);
        /* poldek's terminal on stdin would make sudo with use_pty read it,
           a ^C too; rpm only needs a terminal there to wait for its lock */
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > STDERR_FILENO)
            close(slave);
        return 0;
    }

    /* the master sees the end of output only when nobody holds the slave open */
    close(slave);
    return pid;
}

static
FILE *pty_open(struct p_open_st *pst, unsigned flags, const char *cmd,
               char *const argv[])
{
    struct termios  termios;
    struct winsize  winsize;
    int             fd, slave, execfd = -1;
    pid_t           pid;
    sigset_t        mask;
    char            errmsg[1024];


    if (!isatty(STDOUT_FILENO))
        return pp_open(pst, flags, cmd, argv);

    pst->stream = NULL;

    if (tcgetattr(STDOUT_FILENO, &termios) != 0) {
        snprintf(errmsg, sizeof(errmsg), "tcgetattr(1): %m");
        pst->errmsg = n_strdup(errmsg);
        return NULL;
    }

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsize) != 0) {
        snprintf(errmsg, sizeof(errmsg), "ioctl(1, TIOCGWINSZ): %m");
        pst->errmsg = n_strdup(errmsg);
        return NULL;
    }

    if (access(cmd, R_OK | X_OK) != 0) {
        snprintf(errmsg, sizeof(errmsg), _("%s: no such file"), cmd);
        pst->errmsg = n_strdup(errmsg);
        return NULL;
    }

    if (openpty(&fd, &slave, NULL, &termios, &winsize) != 0) {
        snprintf(errmsg, sizeof(errmsg), "openpty: %m");
        pst->errmsg = n_strdup(errmsg);
        return NULL;
    }

    /* before fork(): after a NULL, callers may retry without a pty */
    if ((pst->stream = fdopen(fd, "r")) == NULL) {
        snprintf(errmsg, sizeof(errmsg), "fdopen: %m");
        pst->errmsg = n_strdup(errmsg);
        close(fd);
        close(slave);
        return NULL;
    }
    setvbuf(pst->stream, NULL, _IONBF, 0);

    if ((pid = forkptys(fd, slave, &mask, &execfd)) == 0) {
        if (flags & P_OPEN_SETSID)
            setsid();
        if (flags & P_OPEN_IGNSIGINT)
            signal(SIGINT, SIG_IGN);
        exec_command(cmd, argv, &mask, execfd);
    }

    return command_started(pst, pid, execfd, fd, cmd);
}
