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

#include <signal.h>
#include <trurl/nassert.h>
#include "log.h"

static volatile sig_atomic_t interrupted = 0;
void *orig_handler = NULL;
static int cnt = 0;

static void sigint_handler(int sig) 
{
    interrupted = 1;
    signal(sig, sigint_handler);
}


void sigint_establish(void)
{
    n_assert(orig_handler == NULL);
    interrupted = 0;
    orig_handler = signal(SIGINT, sigint_handler);
}

void sigint_restore(void)
{
    if (orig_handler)
        signal(SIGINT, orig_handler);
    cnt = 0;
}

int sigint_reached(void)
{
    int v = interrupted;

    if (v && cnt == 0) {
        logn(LOGNOTICE, "Interrupted %d", cnt);
        cnt++;
    }
    
    return v;
}



