/* $Id$ */
#ifndef POLDEK_SIGINT_H
#define POLDEK_SIGINT_H

extern void (*sigint_reached_cb)(void);

void sigint_init(void);
void sigint_destroy(void);

void sigint_push(void (*cb)(void));
void *sigint_pop(void);

int sigint_reached(void);

#endif
