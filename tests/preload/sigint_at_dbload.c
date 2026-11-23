/*
  Preloaded into poldek by sh/08-rpm: one SIGINT as poldek reads the first
  package of the rpm database, as from a ^C typed while it loads the
  installed packages. Once, and in poldek alone.
*/
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <rpm/rpmdb.h>

__attribute__((constructor))
static void only_poldek(void)
{
    unsetenv("LD_PRELOAD");     /* not for rpm or anything else poldek runs */
}

__attribute__((visibility("default")))
Header rpmdbNextIterator(rpmdbMatchIterator mi)
{
    static int sent = 0;
    Header (*next)(rpmdbMatchIterator) =
        (Header (*)(rpmdbMatchIterator))dlsym(RTLD_NEXT, "rpmdbNextIterator");

    if (!sent) {
        sent = 1;
        fputs("sigint_at_dbload: SIGINT\n", stderr);
        raise(SIGINT);
    }
    return next(mi);
}
