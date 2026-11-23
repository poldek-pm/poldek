/*
  Preloaded into poldek by sh/16-hooks: a SIGTERM right before poldek first
  gives SIGTERM its default action back, as when one comes just as poldek
  stops holding it after rpm. Once, and in poldek alone.
*/
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static pid_t poldek_pid;

__attribute__((constructor))
static void only_poldek(void)
{
    unsetenv("LD_PRELOAD");     /* not for rpm or anything else poldek runs */
    poldek_pid = getpid();
}

__attribute__((visibility("default")))
int sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
    int (*next)(int, const struct sigaction *, struct sigaction *) =
        (int (*)(int, const struct sigaction *, struct sigaction *))
        dlsym(RTLD_NEXT, "sigaction");
    static int sent = 0;

    if (!sent && sig == SIGTERM && act != NULL && act->sa_handler == SIG_DFL &&
        getpid() == poldek_pid) {
        sent = 1;
        fputs("signal_at_restore: SIGTERM\n", stderr);
        raise(SIGTERM);
    }
    return next(sig, act, oact);
}
