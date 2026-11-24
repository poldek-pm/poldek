/*
  Preloaded into poldek by sh/17-interrupt: one SIGINT right after poldek
  sets up its ^C handler, as from a ^C typed before an operation gets to rpm.
*/
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "sigint/sigint.h"

__attribute__((constructor))
static void only_poldek(void)
{
    unsetenv("LD_PRELOAD");     /* not for rpm or anything else poldek runs */
}

__attribute__((visibility("default")))
void sigint_init(void)
{
    static int sent = 0;
    void (*next)(void) = (void (*)(void))dlsym(RTLD_NEXT, "sigint_init");

    next();
    if (!sent) {
        sent = 1;
        fputs("sigint_at_init: SIGINT\n", stderr);
        raise(SIGINT);
    }
}
