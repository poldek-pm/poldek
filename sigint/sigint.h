/* $Id$ */
#ifndef POLDEK_SIGINT_H
#define POLDEK_SIGINT_H

void sigint_init(void);
void sigint_destroy(void);
void sigint_reset(void);

void sigint_reset(void);

void sigint_push(void (*cb)(void));
void *sigint_pop(void);

int sigint_reached(void);

/* sigint_reached(); sigint_reset() if reset */
int sigint_reached_reset(int reset);

#endif
