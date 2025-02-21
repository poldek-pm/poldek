/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_TERM_H
#define POLDEK_TERM_H

#define PRCOLOR_BLACK    0
#define PRCOLOR_RED      1
#define PRCOLOR_GREEN    2
#define PRCOLOR_YELLOW   3
#define PRCOLOR_BLUE     4
#define PRCOLOR_MAGNETA  5
#define PRCOLOR_CYAN     6

#define PRAT_BOLD        (1 << 15)

#define TERM_DEFAULT_WIDTH  80
#define TERM_DEFAULT_HEIGHT 24

#include <stddef.h>           /* for size_t     */

#ifndef __GNUC__
#  undef __attribute__
#  define __attribute__(x) /*nothing*/
#endif

#ifndef EXPORT
#  define EXPORT extern
#endif

EXPORT int poldek_term_init(int force_color);
EXPORT int poldek_term_get_width(void);
EXPORT int poldek_term_get_height(void);

EXPORT int poldek_term_vprintf_c(int color, const char *fmt, va_list args);

EXPORT int poldek_term_printf_c(int color, const char *fmt, ...)
    __attribute__((format(printf,2,3)));

EXPORT int poldek_term_snprintf_c(int color, char *str, size_t size,
                           const char *fmt, ...)
   __attribute__((format(printf,4,5)));

EXPORT int poldek_term_vsnprintf_c(int color, char *str, size_t size,
                            const char *fmt, va_list args);

EXPORT int poldek_term_puts_c(int color, const char *s);

EXPORT int poldek_term_ask(int fd, const char *validchrs, const char *msg);

#endif
