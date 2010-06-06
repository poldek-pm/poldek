/* c-compiler dependant macros */

#ifndef POLDEK_COMPILER_H
#define POLDEK_COMPILER_H

#ifdef __GNUC__
#  define EXPORT extern __attribute__((visibility("default")))
#else
#  define EXPORT extern
#  undef __attribute__
#  define __attribute__(x) /* noop */
#endif

#endif
