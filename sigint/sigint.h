/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_SIGINT_H
#define POLDEK_SIGINT_H

#ifndef EXPORT
#  define EXPORT extern
#endif

/* calls nest: only the outermost pair sets up and restores the handler */
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

/* number of SIGINTs since the last reset, to tell a repeated ^C */
EXPORT int sigint_count(void);

/*
 * While a command runs, from sigint_hold() to sigint_unhold(), a hangup, a
 * SIGTERM, a ^\ and a broken output wait for its end instead of ending poldek
 * at once: each interrupts the operation as a ^C does and stays caught until
 * poldek_ts_run() ends the operation with sigint_release(). A signal ignored,
 * as under nohup, stays so, and a handler the caller sets meanwhile stays.
 * The calls do not nest; other threads should block these signals.
 */
EXPORT void sigint_hold(void);
EXPORT void sigint_unhold(void);

/* the signals noted go back to the handler they had and are raised */
EXPORT void sigint_release(void);

/* note SIGHUP, SIGTERM, SIGQUIT or SIGPIPE for sigint_release(), as one that
   came while a command ran; safe in a signal handler */
EXPORT void sigint_note_end(int sig);

/* nonzero once such a signal came, until sigint_release(); sigint_reset()
   keeps it */
EXPORT int sigint_ending(void);

/* the command running is to stop: a second ^C, or a hangup, a SIGTERM or a
   ^\; a broken output alone may come from a ^C that ended a reader */
EXPORT int sigint_stop_asked(void);

#endif
