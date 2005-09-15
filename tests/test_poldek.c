/*
  $Id$
*/
#include "test.h"

#include "test_env.c"
#include "test_match.c"
#include "test_pmdb.c"

Suite *poldek_suite(void)
{
  Suite *s = suite_create("poldek");
  TCase *tc;

  tc = tcase_create("misc");
  tcase_add_test(tc, test_env);
  suite_add_tcase (s, tc);

  
  tc = tcase_create("match");
  tcase_add_test(tc, test_match);
  suite_add_tcase (s, tc);

  tc = tcase_create("pmdb");
  tcase_add_test(tc, test_pmdb);
  suite_add_tcase (s, tc);
  
  return s;
}

int main(int argc, char *argv[])
{
  int nf;
  
  if (argc > 1 && n_str_eq(argv[1], "-v"))
      poldek_VERBOSE = 1;
      
  Suite *s = poldek_suite();
  SRunner *sr = srunner_create(s);
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

