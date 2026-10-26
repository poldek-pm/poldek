#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <dlfcn.h>
#include <poll.h>
#include <unistd.h>
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

#ifdef HAVE_OPENPTY

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

NTEST_RUNNER("p_open", test_pipe_fdopen_fails, test_pty_output_ends_with_command,
             test_pty_fdopen_fails);
#else
/* without openpty() P_OPEN_OUTPTYS falls back to a pipe */
NTEST_RUNNER("p_open", test_pipe_fdopen_fails);
#endif
