/*
  Copyright (C) 2002 Pawel A. Gajda <mis@pld.org.pl>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
  $Id$
*/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <signal.h>

static volatile sig_atomic_t interrupted = 0;


static void sigint_handler(int sig) 
{
    interrupted = 1;
    signal(sig, sigint_handler);
}


void *sigint_establish(void)
{
    void *sigint_fn;

    interrupted = 0;
    sigint_fn = signal(SIGINT, SIG_IGN);

    if (sigint_fn == NULL)
        signal(SIGINT, SIG_DFL);
    else 
        signal(SIGINT, sigint_handler);
    
    return sigint_fn;
}

void sigint_restore(void *sigint_fn)
{
    signal(SIGINT, sigint_fn);
}

int sigint_reached(void)
{
    return interrupted;
}



