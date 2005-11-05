#include "test.h"
START_TEST (test_env) {
    char buf[PATH_MAX], tmp[PATH_MAX];
    const char *s;

    n_snprintf(buf, sizeof(buf), "${ENV1}${ENV2}");
    setenv("ENV1", "pol", 1);
    setenv("ENV2", "dek", 1);
    
    s = poldek_util_expand_env_vars(tmp, sizeof(tmp), buf);
    fail_if(s == NULL, "expand_env_vars failed");
    
    fail_if(n_str_ne(s, "poldek"),
            "expand_env_vars() failed: %s -> %s", buf, s);

    fail_if(n_str_ne(tmp, "poldek"),
            "expand_env_vars() failed: %s -> %s", buf, tmp);

    
    n_snprintf(buf, sizeof(buf), "foobar");
    s = poldek_util_expand_env_vars(tmp, sizeof(tmp), buf);

    fail_if(s == NULL, "expand_env_vars failed");
    fail_if(n_str_ne(s, buf),
                "expand_env_vars() failed: %s -> %s", buf, s);
}
END_TEST



START_TEST (test_var) {
    char buf[PATH_MAX], tmp[PATH_MAX];
    const char *s;
    tn_hash *vars;
    
    n_snprintf(buf, sizeof(buf), "%{1}%{2}");

    vars = n_hash_new(16, NULL);
    n_hash_insert(vars, "1", "pol");
    n_hash_insert(vars, "2", "dek");
    
    s = poldek_util_expand_vars(tmp, sizeof(tmp), buf, '%', vars);
    fail_if(s == NULL, "expand_vars failed");
    
    fail_if(n_str_ne(s, "poldek"),
            "expand_vars() failed: %s -> %s", buf, s);

    fail_if(n_str_ne(tmp, "poldek"),
            "expand_vars() failed: %s -> %s", buf, tmp);

    
    n_snprintf(buf, sizeof(buf), "foobar");
    s = poldek_util_expand_vars(tmp, sizeof(tmp), buf, '%', vars);

    fail_if(s == NULL, "expand_vars failed");
    fail_if(n_str_ne(s, buf),
                "expand_vars() failed: %s -> %s", buf, s);
}
END_TEST


struct test_case test_case_misc_env0 = {
    "env vars expanding", test_env, 
};

struct test_case test_case_misc_env1 = {
    "vars expanding", test_var
};
