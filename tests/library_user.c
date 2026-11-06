/*
  Preloaded into poldek by sh/16-hooks as a program that uses libpoldek and
  handles a hangup itself: it logs "caller" to $CALLER_LOG and goes on. With
  CALLER_NO_SIGINT_INIT set it never calls sigint_init(), and poldek's own
  sigint_init() and sigint_destroy() calls do nothing.
*/
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *log_path = NULL;
static int no_sigint_init = 0;

static void on_hangup(int sig)
{
    int fd;

    (void)sig;
    if (log_path && (fd = open(log_path, O_WRONLY | O_APPEND | O_CREAT, 0644)) >= 0) {
        ssize_t n = write(fd, "caller\n", 7);
        (void)n;
        close(fd);
    }
}

__attribute__((constructor))
static void library_user(void)
{
    struct sigaction sa;
    const char *s;

    unsetenv("LD_PRELOAD");     /* not for rpm or anything else poldek runs */
    if ((s = getenv("CALLER_LOG")) != NULL)
        log_path = strdup(s);
    no_sigint_init = getenv("CALLER_NO_SIGINT_INIT") != NULL;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_hangup;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGHUP, &sa, NULL);
}

__attribute__((visibility("default")))
void sigint_init(void)
{
    if (!no_sigint_init)
        ((void (*)(void))dlsym(RTLD_NEXT, "sigint_init"))();
}

__attribute__((visibility("default")))
void sigint_destroy(void)
{
    if (!no_sigint_init)
        ((void (*)(void))dlsym(RTLD_NEXT, "sigint_destroy"))();
}
