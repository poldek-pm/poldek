/*
  Preloaded into poldek by sh/17-interrupt: a SIGTERM that comes while poldek
  starts rpm, after the fork and before poldek has rpm's pid, and an rpm slow
  to get to its exec. Only a fork with SIGTERM caught counts, as when poldek
  on the command line starts rpm.
*/
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

__attribute__((constructor))
static void only_poldek(void)
{
    unsetenv("LD_PRELOAD");     /* not for rpm or anything else poldek runs */
}

__attribute__((visibility("default")))
pid_t fork(void)
{
    pid_t (*next)(void) = (pid_t (*)(void))dlsym(RTLD_NEXT, "fork");
    struct sigaction sa;
    pid_t pid;

    sigaction(SIGTERM, NULL, &sa);
    pid = next();
    if (sa.sa_handler == SIG_DFL || sa.sa_handler == SIG_IGN)
        return pid;

    if (pid > 0) {
        fputs("signal_at_fork: SIGTERM\n", stderr);
        raise(SIGTERM);
    } else if (pid == 0) {
        sleep(1);
    }
    return pid;
}
