/*
  Preloaded into poldek by sh/19-questions: a front end that answers the
  questions itself, as the Python binding and PackageKit do, with the numbers
  in ANSWER_CONFIRM, ANSWER_TSCONFIRM and ANSWER_CHOOSE. A question whose
  variable is not set stays with the terminal. Each answer is printed as
  "front end: <callback> <answer>".
*/
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "poldek.h"

static int answer(const char *callback, const char *var)
{
    int a = atoi(getenv(var));

    printf("front end: %s %d\n", callback, a);
    fflush(stdout);
    return a;
}

static int confirm(void *data, const void *ts, int hint, const char *message)
{
    (void)data; (void)ts; (void)hint; (void)message;
    return answer("confirm", "ANSWER_CONFIRM");
}

static int ts_confirm(void *data, const void *ts)
{
    (void)data; (void)ts;
    return answer("ts_confirm", "ANSWER_TSCONFIRM");
}

static int choose_equiv(void *data, const void *ts, const void *pkg,
                        const char *cap, void *candidates, int hint)
{
    (void)data; (void)ts; (void)pkg; (void)cap; (void)candidates; (void)hint;
    return answer("choose_equiv", "ANSWER_CHOOSE");
}

__attribute__((visibility("default")))
int poldek_setup(struct poldek_ctx *ctx)
{
    unsetenv("LD_PRELOAD");     /* not for rpm or anything else poldek runs */

    if (getenv("ANSWER_CONFIRM"))
        poldek_configure(ctx, POLDEK_CONF_CONFIRM_CB, confirm, NULL);
    if (getenv("ANSWER_TSCONFIRM"))
        poldek_configure(ctx, POLDEK_CONF_TSCONFIRM_CB, ts_confirm, NULL);
    if (getenv("ANSWER_CHOOSE"))
        poldek_configure(ctx, POLDEK_CONF_CHOOSEEQUIV_CB, choose_equiv, NULL);

    return ((int (*)(struct poldek_ctx *))dlsym(RTLD_NEXT, "poldek_setup"))(ctx);
}
