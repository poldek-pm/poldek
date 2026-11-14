/* cppcheck shim for <trurl/nassert.h>, used only by cppcheck.sh.
 *
 * cppcheck does not follow the real macro's "call n_assert_fail() and
 * continue" shape, so every n_assert()-guarded dereference looks unsafe.
 * Here the failure branch calls abort(), which cppcheck knows does not
 * return, and the valueflow then understands the guard.
 */
#ifndef TRURL_NASSERT_H
#define TRURL_NASSERT_H

#include <stdlib.h>

void (*n_assert_set_hook(void (*hook)(const char*, const char*, int)))
     (const char*, const char*, int);

void n_assert_fail(const char *expr, const char *file, int line);

#define n_assert(expr) \
   do { if (!(expr)) abort(); } while (0)

#endif
