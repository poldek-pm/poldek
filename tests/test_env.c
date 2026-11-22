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

START_TEST (test_env_percent) {
    char tmp[PATH_MAX];
    const char *s;

    /* the text around a variable is copied as is, '%' included */
    setenv("ENV1", "pol", 1);
    s = poldek_util_expand_env_vars(tmp, sizeof(tmp), "/srv/my%20dir/$ENV1%20d");
    fail_if(s == NULL || n_str_ne(s, "/srv/my%20dir/pol%20d"),
            "expand_env_vars() failed: %s", s ? s : "NULL");
}
END_TEST



static void check_env(const char *src, const char *expected)
{
    char tmp[PATH_MAX];
    const char *s;

    s = poldek_util_expand_env_vars(tmp, sizeof(tmp), src);
    fail_if(s == NULL || n_str_ne(s, expected),
            "expand_env_vars(%s): %s, expected %s", src, s ? s : "NULL",
            expected);
}

/* a name takes '_', and ${ needs its } */
START_TEST (test_env_underscore) {
    setenv("ENV1", "pol", 1);
    setenv("releasever", "9", 1);
    setenv("releasever_major", "9x", 1);
    setenv("MY", "my", 1);
    setenv("MY_VAR", "dek", 1);
    unsetenv("ENV_UNSET_1");

    check_env("$releasever_major/$releasever", "9x/9");
    check_env("${MY_VAR}", "dek");
    check_env("$ENV1/${MY_VAR}/", "pol/dek/");
    check_env("$ENV_UNSET_1/x", "$ENV_UNSET_1/x");
    check_env("${ENV1", "${ENV1");
    check_env("${ENV1.x}", "${ENV1.x}");
    check_env("${ENV1}_x", "pol_x");
}
END_TEST

/* an alias's %1 is followed by "_x", not read as %1_x */
START_TEST (test_var_underscore) {
    char tmp[PATH_MAX];
    const char *s;
    tn_hash *vars;

    vars = n_hash_new(16, NULL);
    n_hash_insert(vars, "1", "pol");

    s = poldek_util_expand_vars(tmp, sizeof(tmp), "%1_x", '%', vars, 0);
    fail_if(s == NULL || n_str_ne(s, "pol_x"),
            "expand_vars(%%1_x): %s", s ? s : "NULL");
    n_hash_free(vars);
}
END_TEST

START_TEST (test_var) {
    char buf[PATH_MAX], tmp[PATH_MAX];
    const char *s;
    tn_hash *vars;

    n_snprintf(buf, sizeof(buf), "%%{1}%%{2}");

    vars = n_hash_new(16, NULL);
    n_hash_insert(vars, "1", "pol");
    n_hash_insert(vars, "2", "dek");

    s = poldek_util_expand_vars(tmp, sizeof(tmp), buf, '%', vars, 0);
    fail_if(s == NULL, "expand_vars failed");

    fail_if(n_str_ne(s, "poldek"),
            "expand_vars() failed: %s -> %s", buf, s);

    fail_if(n_str_ne(tmp, "poldek"),
            "expand_vars() failed: %s -> %s", buf, tmp);


    n_snprintf(buf, sizeof(buf), "foobar");
    s = poldek_util_expand_vars(tmp, sizeof(tmp), buf, '%', vars, 0);

    fail_if(s == NULL, "expand_vars failed");
    fail_if(n_str_ne(s, buf),
                "expand_vars() failed: %s -> %s", buf, s);
}
END_TEST

NTEST_RUNNER("env", test_env, test_env_percent, test_env_underscore, test_var,
             test_var_underscore);
