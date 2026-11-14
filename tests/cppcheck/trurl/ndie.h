/* cppcheck shim for <trurl/ndie.h>, used only by cppcheck.sh.
 *
 * The real n_die() never returns, but its prototype does not say so; the
 * attribute lets cppcheck drop the code after a n_die().
 */
#ifndef TRURL_DIE_H
#define TRURL_DIE_H

#include <trurl/tfn_types.h>
#include <trurl/n_snprintf.h>

typedef void (*t_fn_die_hook) (const char *errmsg);

t_fn_die_hook n_die_set_hook(t_fn_die_hook hook);
void n_die(const char *fmt, ...)
     __attribute__((noreturn, format(printf, 1, 2)));

#define trurl_die__if_frozen(arr) \
    do {                                    \
        if ((arr)->flags & TN_ARRAY_FROZEN) \
            n_die("array is frozen");       \
    } while (0)

#endif
