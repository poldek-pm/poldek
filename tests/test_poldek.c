/*
  $Id$
*/
#include "test.h"

#include "test_env.c"
#include "test_match.c"

Suite *poldek_suite(void)
{
  Suite *s = suite_create("poldek");
  TCase *tc_misc, *tc_match;

  tc_misc = tcase_create("misc");
  tcase_add_test(tc_misc, test_env);
  
  tc_match = tcase_create("match");
  tcase_add_test(tc_misc, test_match);
  
  suite_add_tcase (s, tc_misc);
  suite_add_tcase (s, tc_match);
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

