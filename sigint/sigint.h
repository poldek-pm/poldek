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
