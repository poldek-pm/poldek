/*
  $Id$
*/
#include <check.h>
#include "test.h"

extern struct test_suite test_suite_match;
extern struct test_suite test_suite_pmdb;
extern struct test_suite test_suite_op;
extern struct test_suite test_suite_config;

struct test_suite *suites[] = {
    &test_suite_match,
    &test_suite_pmdb,
    &test_suite_op,
    &test_suite_config,
    NULL,
};

extern struct test_case test_case_misc_env;
struct test_case *misc_cases[] = {
    &test_case_misc_env,
    NULL,
};

Suite *make_suite(struct test_suite *tsuite)
{
    Suite *s = suite_create(tsuite->name);
    int i = 0;

    while (tsuite->cases[i].name) {
        TCase *tc = tcase_create(tsuite->cases[i].name);
        tcase_add_test(tc, tsuite->cases[i].test_fn);
        suite_add_tcase(s, tc);
        i++;
    }
    return s;
}

Suite *make_themisc_suite(void)
{
    Suite *s = suite_create("misc");
    int i = 0;

    while (misc_cases[i]) {
        TCase *tc = tcase_create(misc_cases[i]->name);
        tcase_add_test(tc, misc_cases[i]->test_fn);
        suite_add_tcase(s, tc);
        i++;
    }
    return s;
}

/*
  tc = tcase_create("op_ts_postconf");
  tcase_add_test(tc, test_op_ts_postconf);
  suite_add_tcase (s, tc);
  
  return s;
}
*/

int main(int argc, char *argv[])
{
    int i = 0, nerr = 0;
    
    if (argc > 1 && n_str_eq(argv[1], "-v"))
        poldek_VERBOSE = 1;

    if (misc_cases[0]) {
        Suite *s = make_themisc_suite();
        SRunner *sr = srunner_create(s);
        srunner_run_all(sr, CK_NORMAL);
        nerr += srunner_ntests_failed(sr);
        srunner_free(sr);
    }

    while (suites[i]) {
        Suite *s = make_suite(suites[i]);
        SRunner *sr = srunner_create(s);
        printf("\n");
        srunner_run_all(sr, CK_NORMAL);
        nerr += srunner_ntests_failed(sr);
        srunner_free(sr);
        i++;
    }
    
    

    return (nerr == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

