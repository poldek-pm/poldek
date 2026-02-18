/*
  Unit tests for vf_valid_path() from vfile/misc.c
  Tests path validation rules used throughout vfile operations
*/

#include "test.h"
#include <vfile/vfile.h>

/* Test helper: returns 1 if path is valid, 0 if rejected */
static int test_vf_valid_path(const char *path) {
    return vf_valid_path(path);
}

START_TEST (vf_valid_path_basic_absolute) {
    /* Absolute paths must start with / */
    fail_unless(test_vf_valid_path("/tmp") == 1, "/tmp should be valid");
    fail_unless(test_vf_valid_path("/") == 1, "/ should be valid");
    fail_unless(test_vf_valid_path("/a") == 1, "/a should be valid");

    /* Relative paths rejected */
    fail_unless(test_vf_valid_path("tmp") == 0, "relative tmp rejected");
    fail_unless(test_vf_valid_path("./tmp") == 0, "./tmp rejected");
    fail_unless(test_vf_valid_path("../tmp") == 0, "../tmp rejected");
    fail_unless(test_vf_valid_path("subdir/file") == 0, "relative subdir rejected");
}
END_TEST

START_TEST (vf_valid_path_traversal) {
    fail_unless(test_vf_valid_path("/tmp/../etc") == 0, "/tmp/../etc rejected");
    fail_unless(test_vf_valid_path("/tmp/foo/../../../etc") == 0, "deep traversal rejected");
    fail_unless(test_vf_valid_path("/tmp/..") == 0, "trailing .. rejected");
    fail_unless(test_vf_valid_path("/../etc") == 0, "leading .. rejected");
    fail_unless(test_vf_valid_path("/a/b/c/../../d") == 0, "mid-path traversal rejected");
    fail_unless(test_vf_valid_path("/..") == 0, "/.. rejected");
    fail_unless(test_vf_valid_path("/./..") == 0, "/./.. rejected");
}
END_TEST

START_TEST (vf_valid_path_single_dot) {
    /* Single dot is OK (current directory) */
    fail_unless(test_vf_valid_path("/tmp/./file") == 1, "/tmp/./file OK");
    fail_unless(test_vf_valid_path("/./tmp") == 1, "/./tmp OK");
    fail_unless(test_vf_valid_path("/tmp/.") == 1, "/tmp/. OK");
    fail_unless(test_vf_valid_path("/tmp/././file") == 1, "multiple single dots OK");
}
END_TEST

START_TEST (vf_valid_path_whitespace) {
    /* No whitespace allowed */
    fail_unless(test_vf_valid_path("/tmp/my path") == 0, "space rejected");
    fail_unless(test_vf_valid_path("/tmp/path\t") == 0, "tab rejected");
    fail_unless(test_vf_valid_path("/tmp/path\n") == 0, "newline rejected");
    fail_unless(test_vf_valid_path("/tmp/path\r") == 0, "carriage return rejected");
    fail_unless(test_vf_valid_path("/tmp/ path") == 0, "leading space rejected");
    fail_unless(test_vf_valid_path("/tmp/path ") == 0, "trailing space rejected");
}
END_TEST

START_TEST (vf_valid_path_allowed_chars) {
    /* Characters explicitly allowed: -+/._@!~%{}[]()= */
    fail_unless(test_vf_valid_path("/tmp/test-file") == 1, "dash OK");
    fail_unless(test_vf_valid_path("/tmp/test+file") == 1, "plus OK");
    fail_unless(test_vf_valid_path("/tmp/test/file") == 1, "slash OK");
    fail_unless(test_vf_valid_path("/tmp/test.file") == 1, "dot OK");
    fail_unless(test_vf_valid_path("/tmp/test_file") == 1, "underscore OK");
    fail_unless(test_vf_valid_path("/tmp/test@file") == 1, "@ OK");
    fail_unless(test_vf_valid_path("/tmp/test!file") == 1, "! OK");
    fail_unless(test_vf_valid_path("/tmp/test~file") == 1, "tilde OK");
    fail_unless(test_vf_valid_path("/tmp/test%file") == 1, "percent OK");
    fail_unless(test_vf_valid_path("/tmp/test{file}") == 1, "braces OK");
    fail_unless(test_vf_valid_path("/tmp/test[file]") == 1, "brackets OK");
    fail_unless(test_vf_valid_path("/tmp/test(file)") == 1, "parens OK");
    fail_unless(test_vf_valid_path("/tmp/test=file") == 1, "equals OK");

    /* Alphanumeric */
    fail_unless(test_vf_valid_path("/tmp/abcABC123") == 1, "mixed alphanumeric OK");
    fail_unless(test_vf_valid_path("/tmp/123") == 1, "numeric OK");
}
END_TEST

START_TEST (vf_valid_path_realistic_paths) {
    /* Realistic paths used in poldek */
    fail_unless(test_vf_valid_path("/var/cache/poldek") == 1, "cache dir OK");
    fail_unless(test_vf_valid_path("/tmp/poldek-cache-user") == 1, "tmp cache OK");
    fail_unless(test_vf_valid_path("/home/user/.poldek-cache") == 1, "home cache OK");
    fail_unless(test_vf_valid_path("/home/user/.cache/poldek") == 1, "XDG cache OK");
    fail_unless(test_vf_valid_path("/var/lib/poldek") == 1, "lib dir OK");
    fail_unless(test_vf_valid_path("/etc/poldek") == 1, "etc dir OK");

    /* URL-like paths */
    fail_unless(test_vf_valid_path("/var/cache/poldek/http~example.com") == 1, "tilde in hostname OK");
    fail_unless(test_vf_valid_path("/var/cache/poldek/user@host") == 1, "@ for user OK");
}
END_TEST

START_TEST (vf_valid_path_slash_variants) {
    /* Trailing slash */
    fail_unless(test_vf_valid_path("/tmp/") == 1, "trailing slash OK");
    fail_unless(test_vf_valid_path("/var/cache/poldek/") == 1, "trailing slash in deep path OK");

    /* Multiple slashes */
    fail_unless(test_vf_valid_path("/tmp//subdir") == 1, "double slash OK");
    fail_unless(test_vf_valid_path("///tmp") == 1, "triple leading slash OK");
    fail_unless(test_vf_valid_path("//") == 1, "double slash root OK");
}
END_TEST

START_TEST (vf_valid_path_hidden_and_edge) {
    /* Hidden files */
    fail_unless(test_vf_valid_path("/tmp/.hidden") == 1, "hidden file OK");
    fail_unless(test_vf_valid_path("/tmp/.poldekrc") == 1, "hidden poldekrc OK");
    fail_unless(test_vf_valid_path("/tmp/..") == 0, ".. rejected even as hidden");

    /* Edge cases */
    fail_unless(test_vf_valid_path("/a") == 1, "single char path OK");
    fail_unless(test_vf_valid_path("/a/b/c/d/e/f/g") == 1, "deep path OK");
    fail_unless(test_vf_valid_path("") == 0, "empty string rejected");
}
END_TEST

NTEST_RUNNER("valid_path",
    vf_valid_path_basic_absolute,
    vf_valid_path_traversal,
    vf_valid_path_single_dot,
    vf_valid_path_whitespace,
    vf_valid_path_allowed_chars,
    vf_valid_path_realistic_paths,
    vf_valid_path_slash_variants,
    vf_valid_path_hidden_and_edge
);
