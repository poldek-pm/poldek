#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <poll.h>
#include <unistd.h>

#if HAVE_PTY_H
# include <pty.h>
#elif HAVE_LIBUTIL_H           /* FreeBSD */
# include <libutil.h>
#elif HAVE_UTIL_H
# include <util.h>
#endif

#include "../p_open.h"
#include "test.h"

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

    /* p_open() uses a pty only when its own stdout is a terminal */
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

NTEST_RUNNER("p_open", test_pty_output_ends_with_command);
#else
/* without openpty() P_OPEN_OUTPTYS falls back to a pipe: nothing to test */
int main(void) { return 77; }
#endif
