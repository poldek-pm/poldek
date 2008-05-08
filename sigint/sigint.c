/*
  Copyright (C) 2002 Pawel A. Gajda <mis@pld-linux.org>

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
#include <signal.h>
#include <trurl/nassert.h>
#include <trurl/narray.h>
 
#include "sigint.h"

static volatile sig_atomic_t interrupted = 0;
static tn_array *cb_stack = NULL;
static void (*orig_sighandler)(int) = NULL;
static void (*sigint_reached_cb)(void) = NULL;
static int cnt = 0;
static volatile sig_atomic_t enabled = 1;

static void sigint_handler(int sig) 
{
    int i;

    signal(sig, sigint_handler);
    
    //printf("SIGINT %d %d\n", enabled, interrupted);
    
    if (enabled == 0)
        return;
    
    interrupted = 1;
    
    for (i=0; i < n_array_size(cb_stack); i++) {
        void (*cb)(void) = n_array_nth(cb_stack, i);
        cb();
    }

    if (orig_sighandler)
        orig_sighandler(sig);
}

void sigint_emit(void)
{
    int i;

    if (enabled == 0)
        return;

    interrupted = 1;

    for (i = 0; i < n_array_size(cb_stack); i++) {
        void (*cb)(void) = n_array_nth(cb_stack, i);
        cb();
    }
}

void sigint_enable(int v) 
{
    enabled = v;
}


void sigint_reset(void) 
{
    interrupted = 0;
    cnt = 0;
}


void sigint_init(void)
{
    orig_sighandler = NULL;
    interrupted = 0;
    cnt = 0;
    orig_sighandler = signal(SIGINT, sigint_handler);
    cb_stack = n_array_new(4, NULL, NULL);
}

void sigint_destroy(void)
{
    if (orig_sighandler)
        signal(SIGINT, orig_sighandler);
    else
        signal(SIGINT, SIG_DFL);
    
}

void sigint_push(void (*cb)(void)) 
{
    n_array_push(cb_stack, cb);
}


void *sigint_pop(void) 
{
    return n_array_pop(cb_stack);
}


int sigint_reached(void)
{
    int v = interrupted;

    if (v) {
        //printf("REACHED %d %p\n", cnt, sigint_reached_cb);
        if (cnt == 0 && sigint_reached_cb)
            sigint_reached_cb();
        cnt++;
    }

    return v;
}

int sigint_reached_reset(int reset)
{
    int v = sigint_reached();
    if (reset)
        sigint_reset();
    return v;
}



