#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>

#include <trurl/nmalloc.h>

#include "symtab.h"

#ifdef ENABLE_THREADS
# include <threads.h>           /* thread_local before C23 */
# define SYMTAB_TLS thread_local
# define LOCK(m)   pthread_mutex_lock(m)
# define UNLOCK(m) pthread_mutex_unlock(m)
#else
# define SYMTAB_TLS
# define LOCK(m)   ((void)0)
# define UNLOCK(m) ((void)0)
#endif

/*
 * Per-thread cache of the entries last added or found, of any table: the
 * names in use are a few (x86_64, noarch, i686, linux), so a scan of the
 * slots finds them; a miss goes to slot 0 and moves the others one down,
 * dropping the oldest. Correctness never depends on hits.
 *
 * A slot is of the table at its address: tables are static and their
 * entries are never freed, so a slot never outlives what it points to.
 */
#define TLS_SLOTS 16

struct tls_slot {
    const struct symtab *t;
    const struct symtab_ent *e;
};

static SYMTAB_TLS struct tls_slot tls_cache[TLS_SLOTS];

void symtab_lock(struct symtab *t)
{
    LOCK(&t->mutex);
    (void)t;
}

void symtab_unlock(struct symtab *t)
{
    UNLOCK(&t->mutex);
    (void)t;
}

/* called with t->mutex held */
static struct symtab_ent *add_entry(struct symtab *t, const char *str)
{
    struct symtab_ent *e;
    unsigned id;
    size_t len;

    id = t->size + 1;
    if (id > SYMTAB_MAX_ID)
        return NULL;

    len = strlen(str);
    e = n_malloc(sizeof(*e) + len + 1);
    e->len = len;
    e->id = id;
    memcpy(e->str, str, len + 1);
    e->value = t->valuefn ? t->valuefn(e->str, id) : 0;

    t->ents[id - 1] = e;
    t->size = id;

    return e;
}

/* a miss of the TLS cache, a few per thread: a scan of the few names is
   as cheap as a hash of them */
static struct symtab_ent *lookup(struct symtab *t, const char *str)
{
    struct symtab_ent *e = NULL;
    unsigned i;

    LOCK(&t->mutex);

    for (i = 0; i < t->size; i++) {
        if (strcmp(t->ents[i]->str, str) == 0) {
            e = t->ents[i];
            break;
        }
    }

    if (e == NULL)
        e = add_entry(t, str);

    UNLOCK(&t->mutex);

    return e;
}

const struct symtab_ent *symtab_add(struct symtab *t, const char *str)
{
    const struct symtab_ent *e;
    unsigned i;

    for (i = 0; i < TLS_SLOTS; i++) {
        if (tls_cache[i].t == t && strcmp(tls_cache[i].e->str, str) == 0)
            return tls_cache[i].e;
    }

    if ((e = lookup(t, str)) == NULL)
        return NULL;

    memmove(&tls_cache[1], &tls_cache[0], (TLS_SLOTS - 1) * sizeof(tls_cache[0]));
    tls_cache[0].t = t;
    tls_cache[0].e = e;

    return e;
}
