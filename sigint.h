/* $Id$ */
/* $Id$ */
#ifndef POLDEK_SIGINT_H
#define POLDEK_SIGINT_H

void sigint_establish(void);
void sigint_restore(void);
int sigint_reached(void);

#endif
