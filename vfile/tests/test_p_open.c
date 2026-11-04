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

START_TEST(test_pipe_fdopen_fails)
{
    check_fdopen_failure(0);
}
END_TEST

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

/* the same with a pty; stdout is a pty slave, as above */
START_TEST(test_pty_fdopen_fails)
{
    int master, slave, saved_stdout;

    fail_unless(openpty(&master, &slave, NULL, NULL, NULL) == 0, "openpty: %m");
    saved_stdout = dup(STDOUT_FILENO);
    dup2(slave, STDOUT_FILENO);

    check_fdopen_failure(P_OPEN_OUTPTYS);

    dup2(saved_stdout, STDOUT_FILENO);
    close(saved_stdout);
    close(slave);
    close(master);
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

NTEST_RUNNER("p_open", test_pipe_fdopen_fails,
             test_wait_sees_end_with_sigchld_ignored, test_quit_key_misses_command,
             test_hangup_misses_command, test_pty_output_ends_with_command,
             test_pty_fdopen_fails, test_pty_quit_key_misses_command,
             test_pty_hangup_misses_command);
