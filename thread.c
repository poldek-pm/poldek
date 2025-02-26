#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdbool.h>

#if HAVE_LIBPTHREAD
#include <pthread.h>
#endif

#include <trurl/nassert.h>

static bool poldek_USE_THREADS = true;
static bool poldek_THREADING = false;

void poldek_threading_toggle(bool value) {
    if (!poldek_USE_THREADS)
        return;

    __atomic_store_n(&poldek_THREADING, value, __ATOMIC_RELAXED);
}

bool poldek_threading_is_on() {
    if (!poldek_USE_THREADS)
        return false;

    return __atomic_load_n(&poldek_THREADING, __ATOMIC_RELAXED);
}

void poldek_disable_threads() {
    n_assert(poldek_threading_is_on() == false);
    poldek_USE_THREADS = false;
}

bool poldek_enabled_threads() {
    return poldek_USE_THREADS;
}
