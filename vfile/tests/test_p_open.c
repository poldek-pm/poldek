#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <dlfcn.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/wait.h>

#if HAVE_PTY_H
# include <pty.h>
#elif HAVE_LIBUTIL_H           /* FreeBSD */
# include <libutil.h>
#elif HAVE_UTIL_H
# include <util.h>
#endif

#include "../p_open.h"
#include "test.h"

static int fail_fdopen;

/* fdopen() failing as when out of memory; libvfile calls this one */
__attribute__((visibility("default")))
FILE *fdopen(int fd, const char *mode)
{
    static FILE *(*real_fdopen)(int, const char *);

    if (fail_fdopen) {
        errno = ENOMEM;
        return NULL;
    }
    if (real_fdopen == NULL)
        real_fdopen = (FILE *(*)(int, const char *))dlsym(RTLD_NEXT, "fdopen");
    return real_fdopen(fd, mode);
}

static int term_at_exec;

/* the command gets a SIGTERM right before its exec, as when poldek asks it
   to stop just after starting it; libvfile calls this one */
__attribute__((visibility("default")))
int execv(const char *path, char *const argv[])
{
    static int (*real_execv)(const char *, char *const []);

    if (term_at_exec)
        raise(SIGTERM);
    if (real_execv == NULL)
        real_execv = (int (*)(const char *, char *const []))dlsym(RTLD_NEXT, "execv");
    return real_execv(path, argv);
}

static int term_after_fork;

/* the command gets a SIGTERM right after fork(), before it has put
   poldek's handlers away; libvfile calls this one */
__attribute__((visibility("default")))
pid_t fork(void)
{
    static pid_t (*real_fork)(void);
    pid_t pid;

    if (real_fork == NULL)
        real_fork = (pid_t (*)(void))dlsym(RTLD_NEXT, "fork");
    if ((pid = real_fork()) == 0 && term_after_fork)
        raise(SIGTERM);
    return pid;
}

/* with fdopen() failing, p_open() returns NULL and has started nothing */
static void check_fdopen_failure(unsigned flags)
{
    const char *tmpdir = getenv("TMPDIR");
    char dir[PATH_MAX], ran[PATH_MAX];
    char *argv[] = { "sh", "-c", ": > \"$0\"", ran, NULL };
    struct p_open_st pst;
    FILE *stream;
    int st;

    n_snprintf(dir, sizeof(dir), "%s/poldek-p_open-XXXXXX",
               tmpdir && *tmpdir ? tmpdir : "/tmp");
    fail_if(mkdtemp(dir) == NULL, "mkdtemp %s: %s", dir, strerror(errno));
    n_snprintf(ran, sizeof(ran), "%s/ran", dir);

    p_st_init(&pst);
    fail_fdopen = 1;
    stream = p_open(&pst, flags, "/bin/sh", argv);
    fail_fdopen = 0;

    fail_unless(stream == NULL, "p_open() succeeded with a failing fdopen()");
    fail_unless(waitpid(-1, &st, WNOHANG) == -1 && errno == ECHILD,
                "a child was started");
    fail_if(access(ran, F_OK) == 0, "the command ran");
    fail_unless(pst.errmsg && strstr(pst.errmsg, "fdopen"),
                "error message: %s", pst.errmsg ? pst.errmsg : "(none)");
    p_st_destroy(&pst);
    rmdir(dir);
}

/* with SIGCHLD ignored, as poldek may inherit it, the kernel reaps the
   command at its end and waitpid() finds no child: p_wait() still sees
   the end */
START_TEST(test_wait_sees_end_with_sigchld_ignored)
{
    char *argv[] = { "sh", "-c", ":", NULL };
    struct p_open_st pst;
    int i;

    signal(SIGCHLD, SIG_IGN);
    p_st_init(&pst);
    fail_unless(p_open(&pst, 0, "/bin/sh", argv) != NULL, "p_open: %s",
                pst.errmsg ? pst.errmsg : "?");
    for (i = 0; i < 300 && !p_wait(&pst); i++)
        usleep(10000);
    signal(SIGCHLD, SIG_DFL);

    fail_unless(p_wait(&pst), "the end not seen in 3 s");
    p_st_destroy(&pst);
}
END_TEST

static void on_signal(int sig)
{
    (void)sig;
}

static void on_siginfo(int sig, siginfo_t *info, void *context)
{
    (void)sig;
    (void)info;
    (void)context;
}

/* a SIGTERM that reaches the command before its exec, right after fork()
   or right before execv() as WHEN says, ends it: it does not run the
   handler poldek has for SIGTERM, one taking one argument or three */
static void check_signal_before_exec(unsigned flags, int *when, int siginfo)
{
    const char *tmpdir = getenv("TMPDIR");
    char dir[PATH_MAX], ran[PATH_MAX];
    char *argv[] = { "sh", "-c", ": > \"$0\"", ran, NULL };
    struct p_open_st pst;
    struct sigaction sa;
    FILE *stream;

    n_snprintf(dir, sizeof(dir), "%s/poldek-p_open-XXXXXX",
               tmpdir && *tmpdir ? tmpdir : "/tmp");
    fail_if(mkdtemp(dir) == NULL, "mkdtemp %s: %s", dir, strerror(errno));
    n_snprintf(ran, sizeof(ran), "%s/ran", dir);

    memset(&sa, 0, sizeof(sa));
    if (siginfo) {
        sa.sa_sigaction = on_siginfo;
        sa.sa_flags = SA_SIGINFO;
    } else {
        sa.sa_handler = on_signal;
    }
    sigaction(SIGTERM, &sa, NULL);
    p_st_init(&pst);
    *when = 1;
    stream = p_open(&pst, flags, "/bin/sh", argv);
    *when = 0;
    fail_unless(stream != NULL, "p_open: %s", pst.errmsg ? pst.errmsg : "?");
    p_close(&pst);
    signal(SIGTERM, SIG_DFL);

    fail_if(access(ran, F_OK) == 0, "the command ran, the signal lost");
    fail_unless(pst.errmsg && strstr(pst.errmsg, "signal"),
                "not ended by the signal: %s", pst.errmsg ? pst.errmsg : "exit 0");
    p_st_destroy(&pst);
    unlink(ran);
    rmdir(dir);
}

/* a signal ignored, also with SA_SIGINFO among the flags it was set with,
   stays ignored in the command: one that sends it to itself runs on */
static void check_ignored_stays_ignored(unsigned flags, int sa_flags)
{
    const char *tmpdir = getenv("TMPDIR");
    char dir[PATH_MAX], ran[PATH_MAX];
    char *argv[] = { "sh", "-c", "kill -USR1 $$; : > \"$0\"", ran, NULL };
    struct p_open_st pst;
    struct sigaction sa, saved;
    FILE *stream;

    n_snprintf(dir, sizeof(dir), "%s/poldek-p_open-XXXXXX",
               tmpdir && *tmpdir ? tmpdir : "/tmp");
    fail_if(mkdtemp(dir) == NULL, "mkdtemp %s: %s", dir, strerror(errno));
    n_snprintf(ran, sizeof(ran), "%s/ran", dir);

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = sa_flags;
    sigaction(SIGUSR1, &sa, &saved);
    p_st_init(&pst);
    stream = p_open(&pst, flags, "/bin/sh", argv);
    fail_unless(stream != NULL, "p_open: %s", pst.errmsg ? pst.errmsg : "?");
    p_close(&pst);
    sigaction(SIGUSR1, &saved, NULL);

    fail_unless(access(ran, F_OK) == 0, "SIGUSR1 not ignored: %s",
                pst.errmsg ? pst.errmsg : "?");
    p_st_destroy(&pst);
    unlink(ran);
    rmdir(dir);
}

/* wait up to MS milliseconds for PATH to appear */
static int wait_for_file(const char *path, int ms)
{
    while (access(path, F_OK) != 0 && ms > 0) {
        usleep(10 * 1000);
        ms -= 10;
    }
    return access(path, F_OK) == 0;
}

/* a stand-in for poldek, in a session of its own on a fresh terminal, runs a
   command with p_open(FLAGS); once the command has started, KEY is typed on
   the terminal, or the terminal hangs up when KEY is 0; returns whether the
   command ran to its end */
static int command_runs_to_end(unsigned flags, char key)
{
    const char *tmpdir = getenv("TMPDIR");
    char dir[PATH_MAX], started[PATH_MAX], done[PATH_MAX];
    char *argv[] = { "sh", "-c", ": > \"$0\"; sleep 0.5; : > \"$1\"",
                     started, done, NULL };
    int master, slave, st, ran;
    pid_t pid;

    n_snprintf(dir, sizeof(dir), "%s/poldek-p_open-XXXXXX",
               tmpdir && *tmpdir ? tmpdir : "/tmp");
    fail_if(mkdtemp(dir) == NULL, "mkdtemp %s: %s", dir, strerror(errno));
    n_snprintf(started, sizeof(started), "%s/started", dir);
    n_snprintf(done, sizeof(done), "%s/done", dir);
    fail_unless(openpty(&master, &slave, NULL, NULL, NULL) == 0, "openpty: %m");

    if ((pid = fork()) == 0) {
        struct rlimit nocore = { 0, 0 };
        struct p_open_st pst;

        /* poldek's own handler, which the command does not inherit */
        signal(SIGQUIT, on_signal);
        setrlimit(RLIMIT_CORE, &nocore);
        close(master);
        setsid();
        ioctl(slave, TIOCSCTTY, 0);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        close(slave);

        p_st_init(&pst);
        if (p_open(&pst, flags, "/bin/sh", argv) == NULL)
            _exit(2);
        _exit(p_close(&pst) == 0 ? 0 : 1);
    }
    close(slave);

    fail_unless(wait_for_file(started, 2000), "the command did not start");
    if (key)
        fail_unless(write(master, &key, 1) == 1, "write: %m");
    else
        close(master);

    ran = wait_for_file(done, 1500);
    if (key)
        close(master);
    waitpid(pid, &st, 0);

    unlink(started);
    unlink(done);
    rmdir(dir);
    return ran;
}

#define QUIT_KEY '\034'           /* ^\ */

START_TEST(test_quit_key_misses_command)
{
    fail_unless(command_runs_to_end(P_OPEN_SETSID, QUIT_KEY),
                "the command did not run to its end");
}
END_TEST

START_TEST(test_hangup_misses_command)
{
    fail_unless(command_runs_to_end(P_OPEN_SETSID, 0),
                "the command did not run to its end");
}
END_TEST

/* with P_OPEN_OUTPTYS the reader must see the end of the output once
   the command has exited, not only when its own wait times out */
START_TEST(test_pty_output_ends_with_command)
{
    char *argv[] = { "sh", "-c", ":", NULL };
    struct p_open_st pst;
    struct pollfd pfd;
    int master, slave, saved_stdout;
    FILE *stream;

    /* p_open() uses a pty only when its own stdout is a terminal, so feed
       it a pty slave as stdout; otherwise P_OPEN_OUTPTYS falls back to a
       plain pipe and the bug is not exercised. */
    fail_unless(openpty(&master, &slave, NULL, NULL, NULL) == 0, "openpty: %m");
    saved_stdout = dup(STDOUT_FILENO);
    dup2(slave, STDOUT_FILENO);

    p_st_init(&pst);
    stream = p_open(&pst, P_OPEN_OUTPTYS, "/bin/sh", argv);

    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    close(slave);
    close(master);

    fail_unless(stream != NULL, "p_open: %s", pst.errmsg ? pst.errmsg : "?");
    fail_unless(p_close(&pst) == 0, "exit code %d", pst.ec);

    pfd.fd = pst.fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    fail_unless(poll(&pfd, 1, 0) == 1, "no end of output after the command exited");

    p_st_destroy(&pst);
}
END_TEST

/* CHECK on pipes, then on a pty: p_open() uses a pty only when its own
   stdout is a terminal, so stdout is a pty slave meanwhile */
static void check_on_pipes_and_pty(void (*check)(unsigned flags))
{
    int master, slave, saved_stdout;

    check(0);

    fail_unless(openpty(&master, &slave, NULL, NULL, NULL) == 0, "openpty: %m");
    saved_stdout = dup(STDOUT_FILENO);
    dup2(slave, STDOUT_FILENO);

    check(P_OPEN_OUTPTYS);

    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    close(slave);
    close(master);
}

START_TEST(test_fdopen_fails)
{
    check_on_pipes_and_pty(check_fdopen_failure);
}
END_TEST

/* before its exec a command has the signal actions it has after it */
static void check_signal_actions_before_exec(unsigned flags)
{
    check_signal_before_exec(flags, &term_at_exec, 0);
    check_signal_before_exec(flags, &term_after_fork, 0);
    check_signal_before_exec(flags, &term_at_exec, 1);
    check_ignored_stays_ignored(flags, 0);
    check_ignored_stays_ignored(flags, SA_SIGINFO);
}

START_TEST(test_signals_before_exec_act_as_after_it)
{
    check_on_pipes_and_pty(check_signal_actions_before_exec);
}
END_TEST

START_TEST(test_pty_quit_key_misses_command)
{
    fail_unless(command_runs_to_end(P_OPEN_OUTPTYS | P_OPEN_SETSID, QUIT_KEY),
                "the command did not run to its end");
}
END_TEST

START_TEST(test_pty_hangup_misses_command)
{
    fail_unless(command_runs_to_end(P_OPEN_OUTPTYS | P_OPEN_SETSID, 0),
                "the command did not run to its end");
}
END_TEST

NTEST_RUNNER("p_open", test_fdopen_fails,
             test_wait_sees_end_with_sigchld_ignored, test_quit_key_misses_command,
             test_hangup_misses_command, test_pty_output_ends_with_command,
             test_pty_quit_key_misses_command, test_pty_hangup_misses_command,
             test_signals_before_exec_act_as_after_it);
