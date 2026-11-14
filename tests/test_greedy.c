#include "test.h"
extern int poclidek__parse_greedy(const char *arg, int *val);

struct greedy_case {
    const char *arg;
    int ok;
    int val;
};

/* --greedy takes a boolean or a non-negative level that fits an int;
   a level must not silently wrap or turn into a negative depth */
START_TEST (test_greedy_arg) {
    static const struct greedy_case cases[] = {
        { "0", 1, 0 },
        { "1", 1, 1 },
        { "5", 1, 5 },
        { "yes", 1, 1 },
        { "no", 1, 0 },
        { "true", 1, 1 },
        { "false", 1, 0 },
        { "-1", 0, 0 },
        { "4294967296", 0, 0 },
        { "99999999999999999999", 0, 0 },
        { "", 0, 0 },
        { "2x", 0, 0 },
        { "nonsense", 0, 0 },
        { NULL, 0, 0 },
    };
    int i;

    for (i = 0; cases[i].arg != NULL; i++) {
        int val = -12345;
        int ok = poclidek__parse_greedy(cases[i].arg, &val);

        fail_unless(ok == cases[i].ok, "'%s': ok %d, expected %d",
                    cases[i].arg, ok, cases[i].ok);
        if (ok)
            fail_unless(val == cases[i].val, "'%s': val %d, expected %d",
                        cases[i].arg, val, cases[i].val);
    }
}
END_TEST

NTEST_RUNNER("greedy", test_greedy_arg);
