#include "test.h"
#include "symtab.h"

#ifdef ENABLE_THREADS
# include <pthread.h>
#endif

static int nvalues;             /* valuefn calls */

static int value_of(const char *str, unsigned id)
{
    nvalues++;
    return (int)(strlen(str) * 100 + id);
}

/* a string is added once: the second add of it gives the same entry, a
   second string gets the next id */
static struct symtab add_t = SYMTAB_INITIALIZER(value_of);

START_TEST (test_add) {
    const struct symtab_ent *a, *b;

    nvalues = 0;
    fail_unless(symtab_get(&add_t, 1) == NULL, "an entry in an empty table");

    a = symtab_add(&add_t, "x86_64");
    fail_unless(a && a->id == 1 && a->len == 6 && strcmp(a->str, "x86_64") == 0);
    b = symtab_add(&add_t, "noarch");
    fail_unless(b && b->id == 2, "the second string: %p", b);

    fail_unless(symtab_add(&add_t, "x86_64") == a);
    fail_unless(symtab_add(&add_t, "noarch") == b);
    expect_int(nvalues, 2);
    expect_int(add_t.size, 2);

    fail_unless(symtab_get(&add_t, 2) == b);
    fail_unless(symtab_get(&add_t, 0) == NULL);
    fail_unless(symtab_get(&add_t, 3) == NULL);
    fail_unless(strcmp(symtab_str(&add_t, 1), "x86_64") == 0);
    fail_unless(symtab_str(&add_t, 3) == NULL);
    expect_int(symtab_value(&add_t, 2), 602);
    expect_int(symtab_value(&add_t, 3), 0);
}
END_TEST

/* the cache is of all tables: a string of one table is not the entry of
   another */
static struct symtab arch_t = SYMTAB_INITIALIZER(value_of);
static struct symtab os_t = SYMTAB_INITIALIZER(NULL);

START_TEST (test_two_tables) {
    const struct symtab_ent *a, *o;

    a = symtab_add(&arch_t, "linux");
    o = symtab_add(&os_t, "linux");
    fail_unless(a && o && a != o, "one entry of two tables: %p", a);
    expect_int(o->value, 0);
    fail_unless(symtab_add(&arch_t, "linux") == a);
    fail_unless(symtab_add(&os_t, "linux") == o);
}
END_TEST

/* ids in the order of the strings, more of them than the TLS cache holds;
   a table is full at SYMTAB_MAX_ID: the next string is refused, the others
   are still there */
static struct symtab full_t = SYMTAB_INITIALIZER(NULL);

START_TEST (test_full) {
    char s[32];
    unsigned i;

    for (i = 1; i <= SYMTAB_MAX_ID; i++) {
        snprintf(s, sizeof(s), "s%u", i);
        expect_int(symtab_add(&full_t, s)->id, i);
    }
    fail_unless(symtab_add(&full_t, "one more") == NULL);
    fail_unless(symtab_get(&full_t, SYMTAB_MAX_ID + 1) == NULL);
    expect_int(full_t.size, SYMTAB_MAX_ID);

    for (i = 1; i <= SYMTAB_MAX_ID; i++) {
        snprintf(s, sizeof(s), "s%u", i);
        fail_unless(strcmp(symtab_str(&full_t, i), s) == 0, "%u: %s", i,
                    symtab_str(&full_t, i));
        expect_int(symtab_add(&full_t, s)->id, i);
    }
}
END_TEST

#ifdef ENABLE_THREADS
# define NTHREADS  8
# define NSTRINGS  60           /* past the TLS cache, within the table */

static struct symtab shared_t = SYMTAB_INITIALIZER(NULL);
static unsigned ids[NTHREADS][NSTRINGS];

/* each thread adds the same strings, in an order of its own */
static void *add_all(void *arg)
{
    unsigned n = (unsigned)(uintptr_t)arg, i;
    char s[32];

    for (i = 0; i < NSTRINGS; i++) {
        unsigned k = (i * 7 + n * 13) % NSTRINGS;

        snprintf(s, sizeof(s), "str%u", k);
        ids[n][k] = symtab_add(&shared_t, s)->id;
    }
    return NULL;
}

/* threads adding at once get one id for each string, and its entry is read
   by id after the join without a lock */
START_TEST (test_threads) {
    pthread_t th[NTHREADS];
    char s[32];
    unsigned i, k;

    for (i = 0; i < NTHREADS; i++)
        fail_unless(pthread_create(&th[i], NULL, add_all, (void *)(uintptr_t)i) == 0);
    for (i = 0; i < NTHREADS; i++)
        pthread_join(th[i], NULL);

    expect_int(shared_t.size, NSTRINGS);
    for (k = 0; k < NSTRINGS; k++) {
        snprintf(s, sizeof(s), "str%u", k);
        for (i = 1; i < NTHREADS; i++)
            expect_int(ids[i][k], ids[0][k]);
        fail_unless(strcmp(symtab_str(&shared_t, ids[0][k]), s) == 0);
    }
}
END_TEST
# define THREAD_TESTS , test_threads
#else
# define THREAD_TESTS
#endif

NTEST_RUNNER("symtab", test_add, test_two_tables, test_full THREAD_TESTS);
