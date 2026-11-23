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

/* a '%' of an alias that starts no name is left as written; the lookup of
   the empty name ended poldek in n_hash_get()'s assertion */
START_TEST (test_var_not_names) {
    const char *srcs[] = { "%", "%%", "%{}", "%_x", "ls 100%", NULL };
    char tmp[PATH_MAX];
    const char *s;
    tn_hash *vars;
    int i;

    vars = n_hash_new(16, NULL);
    n_hash_insert(vars, "1", "pol");

    for (i = 0; srcs[i]; i++) {
        s = poldek_util_expand_vars(tmp, sizeof(tmp), srcs[i], '%', vars, 0);
        fail_if(s == NULL || n_str_ne(s, srcs[i]),
                "expand_vars(%s): %s", srcs[i], s ? s : "NULL");
    }
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

static const char *table_var(const char *name, void *ctx)
{
    return n_hash_get(ctx, name);
}

static void macro_unset(tn_buf *out, const char *name, void *ctx)
{
    (void)ctx;
    n_buf_printf(out, "%%{_%s}", name);
}

static void check_vars(const struct util__expand *ex, const char *src,
                       const char *expected)
{
    tn_buf *nbuf = util__expand_vars(src, ex);
    const char *s = n_buf_ptr(nbuf);

    fail_if(n_str_ne(s, expected), "util__expand_vars(%s): %s, expected %s",
            src, s, expected);
    n_buf_free(nbuf);
}

/* "<mark>{name}" only: conf.c's %{macros} */
START_TEST (test_util_vars_braced) {
    tn_hash *h = n_hash_new(16, NULL);
    struct util__expand ex = {
        .mark = '%', .xchars = "_-", .get = table_var, .ctx = h,
    };

    n_hash_insert(h, "a", "A");
    n_hash_insert(h, "x-y", "XY");

    check_vars(&ex, "%{a}/%{x-y}/%a", "A/XY/%a");
    check_vars(&ex, "%{nosuch}/%{a.b}/%{}/%{a", "%{nosuch}/%{a.b}/%{}/%{a");
    check_vars(&ex, "100%/%%{a}", "100%/%A");
    n_hash_free(h);
}
END_TEST

/* all of it: repo-dnf.c's dnf variables */
START_TEST (test_util_vars_dnf) {
    tn_hash *h = n_hash_new(16, NULL);
    struct util__expand ex = {
        .mark = '$',
        .flags = UTIL__EXPAND_BARE | UTIL__EXPAND_DEFAULTS |
                 UTIL__EXPAND_ESCAPES,
        .xchars = "_", .get = table_var, .unset = macro_unset, .ctx = h,
    };
    char name[301], src[512], expected[512];

    n_hash_insert(h, "a", "A");
    n_hash_insert(h, "a_b", "AB");

    check_vars(&ex, "$a_b/$a/${a}x/$a.x", "AB/A/Ax/A.x");
    check_vars(&ex, "$nosuch/${nosuch}", "%{_nosuch}/%{_nosuch}");
    check_vars(&ex, "${nosuch:-w$a}/${a:-w}", "wA/A");
    check_vars(&ex, "${a:+w$a}/${nosuch:+w}/", "wA//");
    check_vars(&ex, "${nosuch:-${nosuch2:-${a}}}/", "A/");
    check_vars(&ex, "\\$a/\\\\$a/\\x", "$a/\\A/\\x");
    /* an escaped mark in a word opens nothing */
    check_vars(&ex, "${nosuch:-x\\${y}/", "x${y/");
    check_vars(&ex, "${a:=w}/${a.b}/${a/$/${}", "${a:=w}/${a.b}/${a/$/${}");

    /* a name longer than 255 characters is not one */
    memset(name, 'n', sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    n_snprintf(src, sizeof(src), "$%s", name);
    check_vars(&ex, src, src);

    /* the words of ${:-} nest 8 deep, the rest stays as written */
    n_snprintf(src, sizeof(src), "x");
    for (int i = 0; i < 12; i++) {
        if (i == 3)
            n_snprintf(expected, sizeof(expected), "%s", src);
        n_snprintf(name, sizeof(name), "%s", src);
        n_snprintf(src, sizeof(src), "${n:-%s}", name);
    }
    check_vars(&ex, src, expected);
    n_hash_free(h);
}
END_TEST

NTEST_RUNNER("env", test_env, test_env_percent, test_env_underscore, test_var,
             test_var_underscore, test_var_not_names, test_util_vars_braced,
             test_util_vars_dnf);
