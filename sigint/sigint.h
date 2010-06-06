/* $Id$ */
#ifndef POLDEK_SIGINT_H
#define POLDEK_SIGINT_H

#ifndef EXPORT
#  define EXPORT extern
#endif

EXPORT void sigint_init(void);
EXPORT void sigint_destroy(void);
EXPORT void sigint_reset(void);

/* 
 * emit sigint. Can be used in some external applications 
 * using libpoldek to interrupt given action (eg. searching,
 * processing dependencies and others)
 */
EXPORT void sigint_emit(void);

EXPORT void sigint_push(void (*cb)(void));
EXPORT void *sigint_pop(void);

EXPORT int sigint_reached(void);

/* sigint_reached(); sigint_reset() if reset */
EXPORT int sigint_reached_reset(int reset);

#endif
