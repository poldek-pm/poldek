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


static void vf_sigint_handler(int sig) 
{
    interrupted = 1;
    signal(sig, vf_sigint_handler);
}


void *vf_sigint_establish(void)
{
    void *vf_sigint_fn;

    interrupted = 0;
    vf_sigint_fn = signal(SIGINT, SIG_IGN);

    if (vf_sigint_fn == NULL)
        signal(SIGINT, SIG_DFL);
    else 
        signal(SIGINT, vf_sigint_handler);
    
    return vf_sigint_fn;
}

void vf_sigint_restore(void *vf_sigint_fn)
{
    signal(SIGINT, vf_sigint_fn);
}

int vf_sigint_reached(void)
{
    return interrupted;
}



