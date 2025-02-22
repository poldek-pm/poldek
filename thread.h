#ifndef POLDEK_THREAD_H
#define POLDEK_THREAD_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef ENABLE_THREADS
# include <stdbool.h>
# include <pthread.h>

void poldek_threads_toggle(bool value);
bool poldek_threads_enabled();

# define mutex_lock(m) (poldek_threads_enabled() ? pthread_mutex_lock(m) : ((void) 0))
# define mutex_unlock(m) (poldek_threads_enabled() ? pthread_mutex_unlock(m) : ((void) 0))
#else
# define mutex_lock(m) ((void) 0)
# define mutex_unlock(m) ((void) 0)
#endif

#endif
