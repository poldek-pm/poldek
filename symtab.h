/*
 * symtab - thread-safe symbol table (string interning).
 *
 * Maps a string to a symbol - a small integer id (1..SYMTAB_MAX_ID;
 * 0 = "none") - and back. A table is static (SYMTAB_INITIALIZER) and its
 * entries are never moved or freed, so id -> entry lookups are lock-free
 * and need no atomics.
 *
 * The symtab_add() hot path uses a per-thread (TLS) cache keyed by
 * (table, string) - no atomics, no shared mutable state.
 *
 * Memory model: symtab_get()/symtab_str()/symtab_value() may be called
 * without locking for any id that the calling thread obtained with a
 * happens-before relation to its addition (added by this thread,
 * or received via a mutex, thread create/join, queue etc.). This is exactly
 * the case for an id stored in a struct pkg that was handed over in a
 * properly synchronized way.
 */
#ifndef POLDEK_SYMTAB_H
#define POLDEK_SYMTAB_H

#ifdef HAVE_CONFIG_H            /* ENABLE_THREADS changes struct symtab */
# include "config.h"
#endif

#include <stddef.h>

#ifdef ENABLE_THREADS
# include <pthread.h>
#endif

/* the entries of a table: the arches and oses of packages are a handful,
   a name past them is refused; 0 is "none" */
#define SYMTAB_MAX_ID      64

struct symtab_ent {
    unsigned id;                /* 1-based */
    int      value;             /* computed by valuefn when added */
    int      len;
    char     str[];
};

/* Called once per new string, under the table lock; must not call
   back into the same table. */
typedef int (*symtab_valuefn)(const char *str, unsigned id);

struct symtab {
    unsigned size;              /* ents in use */
    symtab_valuefn valuefn;
    struct symtab_ent *ents[SYMTAB_MAX_ID];   /* of id 1.. */
#ifdef ENABLE_THREADS
    pthread_mutex_t mutex;
#endif
};

#ifdef ENABLE_THREADS
# define SYMTAB__MUTEX_INIT , .mutex = PTHREAD_MUTEX_INITIALIZER
#else
# define SYMTAB__MUTEX_INIT
#endif

/* static struct symtab t = SYMTAB_INITIALIZER(fn); */
#define SYMTAB_INITIALIZER(fn) { .valuefn = (fn) SYMTAB__MUTEX_INIT }

/* Returns entry of str (adding it if needed), NULL if the table is full. */
const struct symtab_ent *symtab_add(struct symtab *t, const char *str);

/* Hold the table lock: for pthread_atfork() handlers, so that a fork()
   waits for a string being added and the child gets the table whole. */
void symtab_lock(struct symtab *t);
void symtab_unlock(struct symtab *t);

static inline
const struct symtab_ent *symtab_get(const struct symtab *t, unsigned id)
{
    if (id == 0 || id > SYMTAB_MAX_ID)
        return NULL;

    return t->ents[id - 1];
}

static inline
const char *symtab_str(const struct symtab *t, unsigned id)
{
    const struct symtab_ent *e = symtab_get(t, id);
    return e ? e->str : NULL;
}

static inline
int symtab_value(const struct symtab *t, unsigned id)
{
    const struct symtab_ent *e = symtab_get(t, id);
    return e ? e->value : 0;
}

#endif /* POLDEK_SYMTAB_H */
