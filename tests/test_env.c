#include "test.h"
START_TEST (test_env) {
    char buf[PATH_MAX], tmp[PATH_MAX];
    const char *s;

    n_snprintf(buf, sizeof(buf), "${ENV1}${ENV2}");
    setenv("ENV1", "pol", 1);
    setenv("ENV2", "dek", 1);
    
    s = expand_env_vars(tmp, sizeof(tmp), buf);
    fail_if(s == NULL, "expand_env_vars failed");
    
    fail_if(n_str_ne(s, "poldek"),
            "expand_env_vars() failed: %s -> %s", buf, s);

    fail_if(n_str_ne(tmp, "poldek"),
            "expand_env_vars() failed: %s -> %s", buf, tmp);

    
    n_snprintf(buf, sizeof(buf), "foobar");
    s = expand_env_vars(tmp, sizeof(tmp), buf);

    fail_if(s == NULL, "expand_env_vars failed");
    fail_if(n_str_ne(s, buf),
                "expand_env_vars() failed: %s -> %s", buf, s);
}
END_TEST


struct test_case test_case_misc_env = {
    "env vars expanding", test_env
};
