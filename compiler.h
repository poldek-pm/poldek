/*
  C-compiler dependant macros
  Copyright (C) 2010 sparky <sparky@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_COMPILER_H
#define POLDEK_COMPILER_H

#ifdef __GNUC__
#  undef EXPORT
#  define EXPORT extern __attribute__((visibility("default")))
#else
#  undef EXPORT
#  define EXPORT extern
#  undef __attribute__
#  undef extern__inline
#  define __attribute__(x) /* noop */
#endif

#endif
