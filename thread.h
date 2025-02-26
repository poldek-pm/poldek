#ifndef POLDEK_THREAD_H
#define POLDEK_THREAD_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdbool.h>

bool poldek_enabled_threads();
void poldek_disable_threads();

#ifdef ENABLE_THREADS
# include <pthread.h>

void poldek_threading_toggle(bool value);
bool poldek_threading_is_on();

# define mutex_lock(m) (poldek_threading_is_on() ? pthread_mutex_lock(m) : ((void) 0))
# define mutex_unlock(m) (poldek_threading_is_on() ? pthread_mutex_unlock(m) : ((void) 0))

#else  /* ENABLE_THREADS */

# define mutex_lock(m) ((void) 0)
# define mutex_unlock(m) ((void) 0)
#endif

#endif
