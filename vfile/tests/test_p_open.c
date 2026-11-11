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

/* pipe2() in fork_command() fails, so the command is not forked at all and
   the message names no syscall that did not fail */
static void check_pipe2_failure(void)
{
    char *argv[] = { "sh", "-c", ":", NULL };
    struct p_open_st pst;
    struct rlimit rl, nofd;
    FILE *stream;
    int fd, st;

    /* the lowest free descriptor: pipe(pp) takes fd and fd + 1, so pipe2()
       cannot have fd + 2 */
    fail_if(getrlimit(RLIMIT_NOFILE, &rl) != 0, "getrlimit: %m");
    fail_if((fd = dup(STDIN_FILENO)) < 0, "dup: %m");
    close(fd);
    nofd = rl;
    nofd.rlim_cur = fd + 2;
    fail_if(setrlimit(RLIMIT_NOFILE, &nofd) != 0, "setrlimit: %m");

    p_st_init(&pst);
    stream = p_open(&pst, 0, "/bin/sh", argv);
    fail_if(setrlimit(RLIMIT_NOFILE, &rl) != 0, "setrlimit: %m");

    fail_unless(stream == NULL, "p_open() succeeded with a failing pipe2()");
    fail_unless(waitpid(-1, &st, WNOHANG) == -1 && errno == ECHILD,
                "a child was started");
    fail_unless(pst.errmsg && strstr(pst.errmsg, "cannot start") &&
                strstr(pst.errmsg, strerror(EMFILE)),
                "error message: %s", pst.errmsg ? pst.errmsg : "(none)");
    p_st_destroy(&pst);
}

/* with a command execv() refuses, p_open() returns NULL, leaves no child
   and says why; the child writes nothing poldek buffered a second time */
static void check_exec_failure(unsigned flags, const char *script, int xerrno)
{
    const char *tmpdir = getenv("TMPDIR");
    char dir[PATH_MAX], path[PATH_MAX], out[PATH_MAX], buf[64];
    char *argv[] = { "script", NULL };
    struct p_open_st pst;
    FILE *stream, *f;
    size_t n;
    int st;

    n_snprintf(dir, sizeof(dir), "%s/poldek-p_open-XXXXXX",
               tmpdir && *tmpdir ? tmpdir : "/tmp");
    fail_if(mkdtemp(dir) == NULL, "mkdtemp %s: %s", dir, strerror(errno));

    /* access() lets it through as executable, execv() refuses it */
    n_snprintf(path, sizeof(path), "%s/script", dir);
    fail_if((f = fopen(path, "w")) == NULL, "fopen %s: %m", path);
    fputs(script, f);
    fclose(f);
    fail_if(chmod(path, 0755) != 0, "chmod %s: %m", path);

    n_snprintf(out, sizeof(out), "%s/out", dir);
    fail_if((f = fopen(out, "w")) == NULL, "fopen %s: %m", out);
    fputs("buffered\n", f);

    p_st_init(&pst);
    stream = p_open(&pst, flags, path, argv);
    fail_unless(stream == NULL,
                "p_open() started a command whose exec fails with %s",
                strerror(xerrno));
    fail_unless(waitpid(-1, &st, WNOHANG) == -1 && errno == ECHILD,
                "a child was left");
    fail_unless(pst.errmsg && strstr(pst.errmsg, path) &&
                strstr(pst.errmsg, strerror(xerrno)),
                "error message: %s", pst.errmsg ? pst.errmsg : "(none)");
    p_st_destroy(&pst);

    fclose(f);
    fail_if((f = fopen(out, "r")) == NULL, "fopen %s: %m", out);
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    fail_unless(strcmp(buf, "buffered\n") == 0, "%s has: %s", out, buf);

    unlink(out);
    unlink(path);
    rmdir(dir);
}

/* no #!: the kernel refuses the script itself */
static void check_exec_failure_enoexec(unsigned flags)
{
    check_exec_failure(flags, "echo ran\n", ENOEXEC);
}

/* #! names an interpreter that is not installed, so execv() fails after the
   kernel has opened the script */
static void check_exec_failure_enoent(unsigned flags)
{
    check_exec_failure(flags, "#!/nonexistent/interpreter\n", ENOENT);
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

enum event { QUIT_KEY, HANGUP };

/* a stand-in for poldek, in a session of its own on a fresh terminal, runs a
   command with p_open(FLAGS); once the command has started, ^\ is typed on
   the terminal (QUIT_KEY) or the terminal hangs up (HANGUP); returns whether
   the command ran to its end */
static int command_runs_to_end(unsigned flags, enum event event)
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
    switch (event) {
    case QUIT_KEY:
        fail_unless(write(master, "\034", 1) == 1, "write: %m");
        break;
    case HANGUP:
        close(master);
        break;
    }

    ran = wait_for_file(done, 1500);
    if (event != HANGUP)
        close(master);
    waitpid(pid, &st, 0);

    unlink(started);
    unlink(done);
    rmdir(dir);
    return ran;
}

START_TEST(test_quit_key_misses_command)
{
    fail_unless(command_runs_to_end(P_OPEN_SETSID, QUIT_KEY),
                "on pipes, the command did not run to its end");
    fail_unless(command_runs_to_end(P_OPEN_OUTPTYS | P_OPEN_SETSID, QUIT_KEY),
                "on a pty, the command did not run to its end");
}
END_TEST

START_TEST(test_hangup_misses_command)
{
    fail_unless(command_runs_to_end(P_OPEN_SETSID, HANGUP),
                "on pipes, the command did not run to its end");
    fail_unless(command_runs_to_end(P_OPEN_OUTPTYS | P_OPEN_SETSID, HANGUP),
                "on a pty, the command did not run to its end");
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

/* failures before a command is forked: fdopen() and pipe2() */
START_TEST(test_start_fails)
{
    check_on_pipes_and_pty(check_fdopen_failure);
    check_pipe2_failure();
}
END_TEST

START_TEST(test_exec_fails)
{
    check_on_pipes_and_pty(check_exec_failure_enoexec);
    check_on_pipes_and_pty(check_exec_failure_enoent);
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

NTEST_RUNNER("p_open", test_start_fails, test_exec_fails,
             test_wait_sees_end_with_sigchld_ignored, test_quit_key_misses_command,
             test_hangup_misses_command, test_pty_output_ends_with_command,
             test_signals_before_exec_act_as_after_it);
