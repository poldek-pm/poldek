/*
  Copyright (C) 2002 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <signal.h>
#include <string.h>
#include <trurl/nassert.h>
#include <trurl/narray.h>
 
#include "compiler.h"
#include "sigint.h"

static volatile sig_atomic_t interrupted = 0;
static volatile sig_atomic_t count = 0;   /* SIGINTs since the last reset */
static tn_array *cb_stack = NULL;
static void (*orig_sighandler)(int) = NULL;
static void (*sigint_reached_cb)(void) = NULL;
static int cnt = 0;
static volatile sig_atomic_t enabled = 1;
static int depth = 0;           /* sigint_init() calls not undone yet */

static volatile sig_atomic_t ending = 0; /* a bit for each signal poldek is
                                            to end with */
static const int held_signals[] = { SIGHUP, SIGTERM, SIGQUIT, SIGPIPE };
#define NHELD (sizeof(held_signals) / sizeof(held_signals[0]))
static struct sigaction held_saved[NHELD];
static int held = 0;            /* between sigint_hold() and sigint_unhold() */
static int kept = 0;            /* signals noted, still caught after that */

static void sigint_handler(int sig) 
{
    int i;

    signal(sig, sigint_handler);
    
    //printf("SIGINT %d %d\n", enabled, interrupted);
    
    if (enabled == 0)
        return;
    
    interrupted = 1;
    count++;
    
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
    count++;

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
    count = 0;
    cnt = 0;
}


void sigint_init(void)
{
    if (depth++ > 0)            /* already set up, e.g. by the shell */
        return;

    /* before the handler, which reads it; never freed, as
       sigint_emit() may come after sigint_destroy() */
    if (cb_stack == NULL)
        cb_stack = n_array_new(4, NULL, NULL);

    orig_sighandler = NULL;
    interrupted = 0;
    count = 0;
    cnt = 0;
    orig_sighandler = signal(SIGINT, sigint_handler);
    if (orig_sighandler == SIG_IGN)     /* started with ^C ignored: keep it so */
        signal(SIGINT, SIG_IGN);
}

/* SET gets the signals sigint_hold() holds */
static void held_set(sigset_t *set)
{
    unsigned i;

    sigemptyset(set);
    for (i = 0; i < NHELD; i++)
        sigaddset(set, held_signals[i]);
}

static void sigint_note_end(int sig)
{
    ending |= 1 << sig;
    interrupted = 1;
}

/* the signals noted do what they would have done: those held go back to
   the handler they had, and each is raised */
void sigint_release(void)
{
    int noted = ending;
    unsigned i;

    for (i = 0; i < NHELD; i++) {
        struct sigaction sa;

        /* unless the caller has set its own handler since */
        if ((kept & (1 << held_signals[i])) &&
            sigaction(held_signals[i], NULL, &sa) == 0 &&
            sa.sa_handler == sigint_note_end)
            sigaction(held_signals[i], &held_saved[i], NULL);
    }

    ending = kept = 0;
    for (i = 0; i < NHELD; i++)
        if (noted & (1 << held_signals[i]))
            raise(held_signals[i]);
}

void sigint_destroy(void)
{
    if (depth == 0 || --depth > 0)
        return;

    if (orig_sighandler)
        signal(SIGINT, orig_sighandler);
    else
        signal(SIGINT, SIG_DFL);
}

void sigint_hold(void)
{
    struct sigaction sa;
    unsigned i;

    n_assert(!held);
    held = 1;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_note_end;
    sa.sa_flags = SA_RESTART;
    held_set(&sa.sa_mask);

    for (i = 0; i < NHELD; i++) {
        if (kept & (1 << held_signals[i]))      /* still caught */
            continue;
        sigaction(held_signals[i], NULL, &held_saved[i]);
        if (held_saved[i].sa_handler != SIG_IGN)
            sigaction(held_signals[i], &sa, NULL);
    }
}

void sigint_unhold(void)
{
    sigset_t set, saved;
    unsigned i;

    n_assert(held);
    held = 0;

    /* a signal noted stays caught until the operation is over: a broken
       output raises SIGPIPE again at every write; one that comes meanwhile
       waits until this is settled, and counts as one after rpm */
    held_set(&set);
    sigprocmask(SIG_BLOCK, &set, &saved);
    for (i = 0; i < NHELD; i++) {
        int bit = 1 << held_signals[i];

        if (ending & bit)
            kept |= bit;
        else if (!(kept & bit))
            sigaction(held_signals[i], &held_saved[i], NULL);
    }
    sigprocmask(SIG_SETMASK, &saved, NULL);
}

int sigint_ending(void)
{
    return ending;
}

int sigint_stop_asked(void)
{
    return count > 1 || (ending & (1 << SIGHUP | 1 << SIGTERM | 1 << SIGQUIT));
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

int sigint_count(void)
{
    return count;
}

int sigint_reached_reset(int reset)
{
    int v = sigint_reached();
    if (reset)
        sigint_reset();
    return v;
}



