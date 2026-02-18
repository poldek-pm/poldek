/*
  Unit tests for getenv_path() - basic path validation
  Tests path validation rules without setuid restrictions
*/

#include "test.h"
#include <pwd.h>

/* Test helper: sets env var, calls getenv_path(), compares result with expected */
static int test_path_env(const char *name, const char *value, const char *expected) {
    setenv(name, value, 1);
    char *v = getenv_path(name);
    unsetenv(name);

    if (!v && !expected)
        return 1;

    if (!v && expected)
        return 0;

    if (v && !expected)
        return 0;

    return strcmp(v, expected) == 0;
}

/* Additional test helper: checks if path is rejected (returns NULL) */
static int test_path_env_rejected(const char *name, const char *value) {
    return test_path_env(name, value, NULL);
}

/* Additional test helper: checks if path is accepted and matches value */
static int test_path_env_accepted(const char *name, const char *value) {
    return test_path_env(name, value, value);
}

START_TEST (getenv_path_basic) {
    /* Absolute paths should be accepted */
    fail_unless(test_path_env_accepted("TMP", "/tmp"), "/tmp should be valid");
    fail_unless(test_path_env_accepted("TMP", "/var/cache"), "/var/cache should be valid");
    fail_unless(test_path_env_accepted("TMP", "/home/user/.poldek-cache"), "home path should be valid");

    /* Relative paths should be rejected */
    fail_unless(test_path_env_rejected("TMP", "tmp"), "relative tmp should fail");
    fail_unless(test_path_env_rejected("TMP", "./tmp"), "./tmp should fail");
    fail_unless(test_path_env_rejected("TMP", "../tmp"), "../tmp should fail");
    fail_unless(test_path_env_rejected("TMP", "subdir/path"), "relative subdir should fail");
}
END_TEST

START_TEST (getenv_path_traversal) {
    /* Directory traversal attacks should be rejected */
    fail_unless(test_path_env_rejected("TMP", "/tmp/../etc"), "/tmp/../etc should fail");
    fail_unless(test_path_env_rejected("TMP", "/tmp/foo/../../../etc"), "deep traversal should fail");
    fail_unless(test_path_env_rejected("TMP", "/tmp/.."), "trailing .. should fail");
    fail_unless(test_path_env_rejected("TMP", "/../etc"), "leading .. should fail");
    fail_unless(test_path_env_rejected("TMP", "/./../etc"), "mixed . and .. should fail");
    fail_unless(test_path_env_rejected("TMP", "/a/b/c/../../d"), "mid-path traversal rejected");
    fail_unless(test_path_env_rejected("TMP", "/a/../b/../c"), "multiple traversals rejected");
    fail_unless(test_path_env_rejected("TMP", "/.."), "root parent rejected");
    fail_unless(test_path_env_rejected("TMP", "/./.."), "./.. traversal rejected");
}
END_TEST

START_TEST (getenv_path_dots) {
    /* Single dots in filenames are OK, but ".." anywhere is not */
    fail_unless(test_path_env_accepted("TMP", "/tmp/foo.bar"), "single dot in filename is OK");
    fail_unless(test_path_env_accepted("TMP", "/tmp/./file"), "single dot as dir OK");
    fail_unless(test_path_env_rejected("TMP", "/tmp/foo..bar"), "double dot anywhere is rejected");
    fail_unless(test_path_env_rejected("TMP", "/tmp/..file"), ".. at start of name rejected");
    fail_unless(test_path_env_rejected("TMP", "/tmp/file.."), ".. at end of name rejected");
}
END_TEST

START_TEST (getenv_path_whitespace) {
    /* Paths with whitespace should be rejected */
    fail_unless(test_path_env_rejected("TMP", "/tmp/my path"), "space in path should fail");
    fail_unless(test_path_env_rejected("TMP", "/tmp/path\t"), "tab in path should fail");
    fail_unless(test_path_env_rejected("TMP", "/tmp/path\n"), "newline in path should fail");
    fail_unless(test_path_env_rejected("TMP", "/tmp/path\r"), "carriage return rejected");
    fail_unless(test_path_env_rejected("TMP", "/tmp/ path"), "leading space rejected");
    fail_unless(test_path_env_rejected("TMP", "/tmp/path "), "trailing space rejected");
}
END_TEST

START_TEST (getenv_path_special_chars) {
    /* Various shell-significant characters that vf_valid_path allows */
    fail_unless(test_path_env_accepted("TMP", "/tmp/test-file"), "dash is OK");
    fail_unless(test_path_env_accepted("TMP", "/tmp/test_file"), "underscore is OK");
    fail_unless(test_path_env_accepted("TMP", "/tmp/test+dir"), "plus is OK");
    fail_unless(test_path_env_accepted("TMP", "/tmp/test.dir"), "dot is OK");
    fail_unless(test_path_env_accepted("TMP", "/tmp/test@file"), "@ is OK");
    fail_unless(test_path_env_accepted("TMP", "/tmp/test!file"), "! is OK");
    fail_unless(test_path_env_accepted("TMP", "/tmp/test~file"), "tilde is OK");
    fail_unless(test_path_env_accepted("TMP", "/tmp/test%file"), "percent is OK");
    fail_unless(test_path_env_accepted("TMP", "/tmp/test{file}"), "braces are OK");
    fail_unless(test_path_env_accepted("TMP", "/tmp/test[file]"), "brackets are OK");
    fail_unless(test_path_env_accepted("TMP", "/tmp/test(file)"), "parens are OK");
    fail_unless(test_path_env_accepted("TMP", "/tmp/test=file"), "equals is OK");
}
END_TEST

START_TEST (getenv_path_edge_cases) {
    /* Empty and NULL */
    fail_unless(test_path_env_rejected("TMP", ""), "empty string should fail");
    
    /* Undefined env var */
    unsetenv("POLDEK_TEST_UNDEFINED_VAR");
    char *v = getenv_path("POLDEK_TEST_UNDEFINED_VAR");
    fail_if(v != NULL, "undefined env var should return NULL");
    
    /* Empty env var value */
    setenv("POLDEK_TEST_EMPTY", "", 1);
    v = getenv_path("POLDEK_TEST_EMPTY");
    unsetenv("POLDEK_TEST_EMPTY");
    fail_if(v != NULL, "empty env var should return NULL");
}
END_TEST

START_TEST (getenv_path_slashes) {
    /* Trailing slash */
    fail_unless(test_path_env_accepted("TMP", "/tmp/"), "trailing slash OK");
    fail_unless(test_path_env_accepted("TMP", "/var/cache/poldek/"), "trailing slash in deep path OK");
    
    /* Multiple slashes */
    fail_unless(test_path_env_accepted("TMP", "/tmp//subdir"), "double slash OK");
    fail_unless(test_path_env_accepted("TMP", "///tmp"), "triple leading slash OK");
    fail_unless(test_path_env_accepted("TMP", "//"), "double slash root OK");
}
END_TEST

START_TEST (getenv_path_realistic) {
    /* Realistic paths used in poldek */
    fail_unless(test_path_env_accepted("XDG_CACHE_HOME", "/home/user/.cache"), "XDG style path OK");
    fail_unless(test_path_env_accepted("HOME", "/home/user"), "normal home OK");
    fail_unless(test_path_env_accepted("HOME", "/root"), "/root as home OK");
    fail_unless(test_path_env_accepted("TMPDIR", "/tmp"), "/tmp OK");
    fail_unless(test_path_env_accepted("TMPDIR", "/var/tmp"), "/var/tmp OK");
    fail_unless(test_path_env_accepted("PATH", "/tmp/123"), "numeric dir OK");
    fail_unless(test_path_env_accepted("PATH", "/dev/null"), "/dev/null OK");
    
    /* Tilde not expanded */
    fail_unless(test_path_env_rejected("HOME", "~/.cache"), "tilde not expanded, rejected");
}
END_TEST

START_TEST (getenv_path_length) {
    /* Very long paths should fail */
    char long_path[PATH_MAX];
    memset(long_path, 'a', sizeof(long_path));
    long_path[0] = '/';
    long_path[sizeof(long_path) - 1] = '\0';

    fail_unless(test_path_env_rejected("TMP", long_path), "very long path should fail");
}
END_TEST

NTEST_RUNNER("getenv_path",
             getenv_path_basic,
             getenv_path_traversal,
             getenv_path_dots,
             getenv_path_whitespace,
             getenv_path_special_chars,
             getenv_path_edge_cases,
             getenv_path_slashes,
             getenv_path_realistic,
             getenv_path_length
);
