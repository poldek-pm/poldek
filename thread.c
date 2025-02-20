#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdbool.h>
#if HAVE_LIBPTHREAD
#include <pthread.h>
#endif

static bool poldek_THREADS = false;

void poldek_threads_toggle(bool value) {
    __atomic_store_n(&poldek_THREADS, value, __ATOMIC_RELAXED);
}

bool poldek_threads_enabled() {
    return __atomic_load_n(&poldek_THREADS, __ATOMIC_RELAXED);
}
