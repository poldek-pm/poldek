#include "test.h"
START_TEST (test_env) {
    char buf[PATH_MAX], tmp[PATH_MAX];

    n_snprintf(buf, sizeof(buf), "${ENV1}${ENV2}");
    setenv("ENV1", "pol", 1);
    setenv("ENV2", "dek", 1);

    expand_env_vars(tmp, sizeof(tmp), buf);
    fail_unless(n_str_eq(tmp, "poldek"),
                "expand_env_vars() failed: %s -> %s", buf, tmp);
    
}
END_TEST


struct test_case test_case_misc_env = {
    "env vars expanding", test_env
};
