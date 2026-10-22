/*
 * HTTP/HTTPS Tests for vfile
 * Tests HTTP fetching, redirects, auth, and HTTPS downgrade protection
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include "../vfile.h"
#include "test.h"
#include <trurl/nstr.h>

/* Server configuration from environment or defaults */
static char server_host[256] = "127.0.0.1";
static int http_port = 0;
static int https_port = 0;
static char test_data_dir[PATH_MAX] = "./data";

/* Test file paths */
#define TEST_FILE_SMALL "small.txt"
#define TEST_FILE_BINARY "binary.bin"

/* Temp directory for downloads */
static char tmpdir[PATH_MAX];

/* Saved SIGPIPE disposition, restored in teardown */
static struct sigaction sigpipe_old;

/* Flag for vfile initialization */
static int vfile_initialized = 0;

/* Helper: Read file content into buffer */
static ssize_t read_file(const char *path, char *buf, size_t size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    ssize_t n = read(fd, buf, size - 1);
    close(fd);

    if (n >= 0) buf[n] = '\0';
    return n;
}

/* Helper: Compare downloaded file with expected */
static int compare_with_expected(const char *downloaded, const char *expected_name)
{
    char expected_path[8192];
    char buf1[4096], buf2[4096];
    ssize_t n1, n2;
    int ret;

    ret = snprintf(expected_path, sizeof(expected_path), "%s/%s",
                   test_data_dir, expected_name);
    if ((size_t)ret >= sizeof(expected_path))
        return -1;  /* path truncated */

    n1 = read_file(downloaded, buf1, sizeof(buf1));
    n2 = read_file(expected_path, buf2, sizeof(buf2));

    if (n1 < 0 || n2 < 0) return -1;
    if (n1 != n2) return -1;

    return memcmp(buf1, buf2, n1);
}

static int have_file(const char *name)
{
    char dest[PATH_MAX];

    if ((size_t)snprintf(dest, sizeof(dest), "%s/%s", tmpdir, name) >= sizeof(dest))
        ck_abort_msg("path truncated");
    return access(dest, F_OK) == 0;
}

static void drop_file(const char *name)
{
    char dest[PATH_MAX];

    if ((size_t)snprintf(dest, sizeof(dest), "%s/%s", tmpdir, name) >= sizeof(dest))
        ck_abort_msg("path truncated");
    unlink(dest);
}

/* Setup: Called before each test */
static void http_setup(void)
{
    static int verbose = 0;

    save_tz();

    if (getenv("VERBOSE"))
        verbose = 2;

    //static int nretries = 2;  /* Limit retries to 2 for faster tests */

    /* Initialize vfile once */
    if (!vfile_initialized) {
        vfile_configure(VFILE_CONF_VERBOSE, &verbose);
        vfile_configure(VFILE_CONF_CACHEDIR, "/tmp/vfile_test_cache");
        vfile_configure(VFILE_CONF_STUBBORN_RETR, 0); /* disable retrying */
        //vfile_configure(VFILE_CONF_STUBBORN_NRETRIES, nretries);
        vfile_setup();
        vfile_initialized = 1;
    }

    /* Get server config from environment */
    const char *host = getenv("TEST_SERVER_HOST");
    const char *http = getenv("TEST_HTTP_PORT");
    const char *https = getenv("TEST_HTTPS_PORT");
    const char *datadir = getenv("TEST_DATA_DIR");

    if (host) n_strncpy(server_host, host, sizeof(server_host));
    if (http) http_port = atoi(http);
    if (https) https_port = atoi(https);
    if (datadir) n_strncpy(test_data_dir, datadir, sizeof(test_data_dir));

    /* Create temp directory */
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/vfile_test_%d_%ld", getpid(), (long)time(NULL));
    mkdir(tmpdir, 0755);

    /* The harness may have left SIGPIPE ignored; start each test with DFL
       and restore the previous disposition afterwards. */
    {
        struct sigaction act;
        memset(&act, 0, sizeof(act));
        act.sa_handler = SIG_DFL;
        sigemptyset(&act.sa_mask);
        sigaction(SIGPIPE, &act, &sigpipe_old);
    }
}

/* Teardown: Called after each test */
static void http_teardown(void)
{
    /* Restore the SIGPIPE disposition the harness had before this test */
    sigaction(SIGPIPE, &sigpipe_old, NULL);

    restore_tz();

    /* Cleanup temp directory */
    /* Note: vf_fetch deletes files, but we clean up the dir */
    rmdir(tmpdir);
}

/* === HTTP Tests === */

START_TEST(test_http_get_small)
{
    char url[512], dest[PATH_MAX];
    int rc;

    fail_if(http_port == 0);

    snprintf(url, sizeof(url), "http://%s:%d/file/%s",
             server_host, http_port, TEST_FILE_SMALL);

    rc = vf_fetch(url, tmpdir, 0, NULL, NULL);
    /* vfile uses 1 for success, 0 for failure */
    fail_if(rc == 0);

    /* Verify file was created */
    if ((size_t)snprintf(dest, sizeof(dest), "%s/%s", tmpdir, TEST_FILE_SMALL) >= sizeof(dest)) ck_abort_msg("path truncated");
    fail_if(access(dest, F_OK) != 0);

    /* Verify content */
    fail_if(compare_with_expected(dest, TEST_FILE_SMALL) != 0);
}
END_TEST

/* the fetched file gets the server's Last-Modified, whatever the local zone */
START_TEST(test_http_keeps_server_mtime)
{
    char url[512], dest[PATH_MAX];
    struct stat st;
    time_t expected;

    fail_if(http_port == 0);
    use_zone_with_dst();

    snprintf(url, sizeof(url), "http://%s:%d/file/%s",
             server_host, http_port, TEST_FILE_SMALL);
    fail_if(vf_fetch(url, tmpdir, 0, NULL, NULL) == 0);

    if ((size_t)snprintf(dest, sizeof(dest), "%s/%s", tmpdir, TEST_FILE_SMALL) >= sizeof(dest))
        ck_abort_msg("path truncated");
    fail_if(stat(dest, &st) != 0);
    expected = server_mtime(TEST_FILE_SMALL);
    ck_assert_msg(st.st_mtime == expected,
                  "mtime %ld != server %ld", (long)st.st_mtime, (long)expected);
}
END_TEST

START_TEST(test_http_get_binary)
{
    char url[512], dest[PATH_MAX];
    int rc;

    fail_if(http_port == 0);

    snprintf(url, sizeof(url), "http://%s:%d/file/%s",
             server_host, http_port, TEST_FILE_BINARY);

    rc = vf_fetch(url, tmpdir, 0, NULL, NULL);
    fail_if(rc == 0);

    if ((size_t)snprintf(dest, sizeof(dest), "%s/%s", tmpdir, TEST_FILE_BINARY) >= sizeof(dest)) ck_abort_msg("path truncated");
    fail_if(access(dest, F_OK) != 0);

    fail_if(compare_with_expected(dest, TEST_FILE_BINARY) != 0);
}
END_TEST

START_TEST(test_http_404)
{
    char url[512];
    int rc;

    fail_if(http_port == 0);

    snprintf(url, sizeof(url), "http://%s:%d/file/nonexistent.txt",
             server_host, http_port);

    /* Should fail with 404 - rc == 0 means failure */
    rc = vf_fetch(url, tmpdir, 0, NULL, NULL);
    fail_if(rc != 0);
}
END_TEST

START_TEST(test_http_redirect_302)
{
    char url[512], dest[PATH_MAX];
    int rc;

    fail_if(http_port == 0);

    /* Single redirect to small.txt */
    snprintf(url, sizeof(url), "http://%s:%d/redirect/1/file/%s",
             server_host, http_port, TEST_FILE_SMALL);

    rc = vf_fetch(url, tmpdir, 0, NULL, NULL);
    fail_if(rc == 0);

    if ((size_t)snprintf(dest, sizeof(dest), "%s/%s", tmpdir, TEST_FILE_SMALL) >= sizeof(dest)) ck_abort_msg("path truncated");
    fail_if(access(dest, F_OK) != 0);
}
END_TEST

START_TEST(test_http_redirect_chain)
{
    char url[512], dest[PATH_MAX];
    int rc;

    fail_if(http_port == 0);

    /* 3 redirects then to small.txt */
    snprintf(url, sizeof(url), "http://%s:%d/redirect/3/file/%s",
             server_host, http_port, TEST_FILE_SMALL);

    rc = vf_fetch(url, tmpdir, 0, NULL, NULL);
    fail_if(rc == 0);

    if ((size_t)snprintf(dest, sizeof(dest), "%s/%s", tmpdir, TEST_FILE_SMALL) >= sizeof(dest)) ck_abort_msg("path truncated");
    fail_if(access(dest, F_OK) != 0);
}
END_TEST

/* stat over a redirect that switches protocol and then fails; the failed
   branch used to dereference the already freed request */
START_TEST(test_http_stat_redirect_failed)
{
    char url[512];
    struct vf_stat vfst;

    fail_if(http_port == 0);

    snprintf(url, sizeof(url), "http://%s:%d/to-dead-https/%s",
             server_host, http_port, TEST_FILE_SMALL);

    fail_if(vf_stat(url, tmpdir, &vfst, NULL) != 0);
}
END_TEST

/* Through a proxy vfile sends the absolute request URL in the request line
   (vfffmod.c: vreq.uri = req->proxy_host ? req->url : req->uri).  An explicit
   port in the requested URL must survive vf_request_new(), otherwise the proxy
   is told to fetch the very same resource from the protocol default port. */
START_TEST(test_http_proxy_keeps_port)
{
    char url[512], proxy[256], expect[512], dest[PATH_MAX];
    char line[1024];
    const char *proxy_log = getenv("TEST_PROXY_LOG");
    FILE *f;
    int found = 0;

    fail_if(http_port == 0);
    fail_if(proxy_log == NULL || *proxy_log == '\0', "TEST_PROXY_LOG not set");

    /* proxy is the test server itself, reached on its non-default port */
    if ((size_t)snprintf(proxy, sizeof(proxy), "http://%s:%d",
                         server_host, http_port) >= sizeof(proxy))
        ck_abort_msg("proxy url truncated");

    if ((size_t)snprintf(url, sizeof(url), "http://%s:%d/file/%s",
                         server_host, http_port, TEST_FILE_SMALL) >= sizeof(url))
        ck_abort_msg("url truncated");

    if ((size_t)snprintf(expect, sizeof(expect), "http://%s:%d/file/%s",
                         server_host, http_port, TEST_FILE_SMALL) >= sizeof(expect))
        ck_abort_msg("expect truncated");

    setenv("http_proxy", proxy, 1);
    setenv("no_proxy", "", 1);
    setenv("VFILE_CONF_NOPROXY", "", 1);

    fail_if(vf_fetch(url, tmpdir, 0, NULL, NULL) == 0);

    unsetenv("http_proxy");

    if ((size_t)snprintf(dest, sizeof(dest), "%s/%s", tmpdir, TEST_FILE_SMALL) >= sizeof(dest))
        ck_abort_msg("path truncated");
    fail_if(access(dest, F_OK) != 0);
    fail_if(compare_with_expected(dest, TEST_FILE_SMALL) != 0);

    /* the proxy must have seen the absolute URI, port included;
       proxy.log format is "<fileno> <uri>" */
    f = fopen(proxy_log, "r");
    fail_if(f == NULL, "cannot read proxy log %s", proxy_log);
    while (fgets(line, sizeof(line), f) != NULL) {
        char *eol = strchr(line, '\n');
        char *uri;
        if (eol)
            *eol = '\0';
        uri = strchr(line, ' ');
        if (uri && strcmp(uri + 1, expect) == 0)
            found = 1;
    }
    fclose(f);

    fail_if(found == 0, "proxy did not receive '%s'", expect);
}
END_TEST

/* After dropping the HTTP keep-alive liveness probe, a pooled connection
   through a proxy should be reused instead of opening a new one every time. */
START_TEST(test_http_proxy_keepalive_reuse)
{
    char url[512], proxy[256], dest[PATH_MAX];
    char line[1024];
    const char *proxy_log = getenv("TEST_PROXY_LOG");
    FILE *f;
    int first_fd = -1, other_fd = -1, reused = 1;

    fail_if(http_port == 0);
    fail_if(proxy_log == NULL || *proxy_log == '\0', "TEST_PROXY_LOG not set");

    /* proxy is the test server itself, reached on its non-default port */
    if ((size_t)snprintf(proxy, sizeof(proxy), "http://%s:%d",
                         server_host, http_port) >= sizeof(proxy))
        ck_abort_msg("proxy url truncated");

    if ((size_t)snprintf(url, sizeof(url), "http://%s:%d/file/%s",
                         server_host, http_port, TEST_FILE_SMALL) >= sizeof(url))
        ck_abort_msg("url truncated");

    setenv("http_proxy", proxy, 1);
    setenv("no_proxy", "", 1);
    setenv("VFILE_CONF_NOPROXY", "", 1);

    /* start with an empty log */
    f = fopen(proxy_log, "w");
    fail_if(f == NULL, "cannot truncate proxy log %s", proxy_log);
    fclose(f);

    /* first fetch opens a new connection to the proxy */
    fail_if(vf_fetch(url, tmpdir, 0, NULL, NULL) == 0);
    if ((size_t)snprintf(dest, sizeof(dest), "%s/%s", tmpdir, TEST_FILE_SMALL) >= sizeof(dest))
        ck_abort_msg("path truncated");
    unlink(dest);

    /* second fetch should reuse the same proxy connection */
    fail_if(vf_fetch(url, tmpdir, 0, NULL, NULL) == 0);

    unsetenv("http_proxy");

    if ((size_t)snprintf(dest, sizeof(dest), "%s/%s", tmpdir, TEST_FILE_SMALL) >= sizeof(dest))
        ck_abort_msg("path truncated");
    fail_if(access(dest, F_OK) != 0);
    fail_if(compare_with_expected(dest, TEST_FILE_SMALL) != 0);

    /* both proxy requests must have come over the same connection */
    f = fopen(proxy_log, "r");
    fail_if(f == NULL, "cannot read proxy log %s", proxy_log);
    while (fgets(line, sizeof(line), f) != NULL) {
        char *eol = strchr(line, '\n');
        int fd;
        if (eol)
            *eol = '\0';
        if (sscanf(line, "%d ", &fd) != 1)
            continue;
        if (first_fd == -1)
            first_fd = fd;
        else if (fd != first_fd) {
            other_fd = fd;
            reused = 0;
        }
    }
    fclose(f);

    fail_if(first_fd == -1, "proxy did not log any request");
    fail_if(reused == 0, "proxy connection was not reused (fd %d != %d)",
            first_fd, other_fd);
}
END_TEST

/* second fetch reuses the pooled connection the server has already closed;
   with retrying disabled it can only succeed by reconnecting transparently */
START_TEST(test_http_keepalive_dropped)
{
    char url[512];
    int i;

    fail_if(http_port == 0);

    snprintf(url, sizeof(url), "http://%s:%d/keepalive-drop/" TEST_FILE_SMALL,
             server_host, http_port);

    for (i = 0; i < 2; i++) {
        fail_if(vf_fetch(url, tmpdir, 0, NULL, NULL) == 0);
        fail_if(!have_file(TEST_FILE_SMALL));
        drop_file(TEST_FILE_SMALL);
    }
}
END_TEST

/* like above but the server resets the connection, so it is the write of
   the second request that fails (ECONNRESET), not the read */
START_TEST(test_http_keepalive_reset)
{
    char url[512];

    fail_if(http_port == 0);

    snprintf(url, sizeof(url), "http://%s:%d/keepalive-reset/" TEST_FILE_SMALL,
             server_host, http_port);

    fail_if(vf_fetch(url, tmpdir, 0, NULL, NULL) == 0);
    drop_file(TEST_FILE_SMALL);

    usleep(200000);             /* let the RST arrive before the next write */

    fail_if(vf_fetch(url, tmpdir, 0, NULL, NULL) == 0);
    fail_if(!have_file(TEST_FILE_SMALL));
}
END_TEST

START_TEST(test_http_auth_success)
{
    char url[512];
    int rc;

    fail_if(http_port == 0);

    /* Valid credentials: test:pass */
    snprintf(url, sizeof(url), "http://test:pass@%s:%d/auth/basic",
             server_host, http_port);

    rc = vf_fetch(url, tmpdir, 0, NULL, NULL);
    fail_if(rc == 0);
}
END_TEST

START_TEST(test_http_auth_fail)
{
    char url[512];
    int rc;

    fail_if(http_port == 0);

    /* Invalid credentials - should fail (rc == 0) */
    snprintf(url, sizeof(url), "http://wrong:wrong@%s:%d/auth/basic",
             server_host, http_port);

    rc = vf_fetch(url, tmpdir, 0, NULL, NULL);
    fail_if(rc != 0);
}
END_TEST

/* === HTTPS Tests === */

START_TEST(test_https_get)
{
    char url[512];
    int rc;

    fail_if(https_port == 0);

    snprintf(url, sizeof(url), "https://%s:%d/file/%s",
             server_host, https_port, TEST_FILE_SMALL);

    /* With strict SSL verification (default), self-signed cert should fail.
     * This test verifies that certificate verification is active and
     * rejects self-signed certificates. */
    rc = vf_fetch(url, tmpdir, VFM_QUITERR, NULL, NULL);

    /* rc == 0 means failure - self-signed cert should be rejected */
    fail_if(rc != 0);  /* Expect failure - cert verification working */
}
END_TEST

START_TEST(test_https_noverify)
{
    char url[512], dest[PATH_MAX];
    int rc;

    fail_if(https_port == 0);

    /* Disable SSL verification for self-signed test cert */
    vfile_configure(VFILE_CONF_SSL_VERIFY_NONE, 1);

    snprintf(url, sizeof(url), "https://%s:%d/file/%s",
             server_host, https_port, TEST_FILE_SMALL);

    /* Should succeed with verification disabled */
    rc = vf_fetch(url, tmpdir, 0, NULL, NULL);
    fail_if(rc == 0);  /* rc == 0 is failure */

    /* Verify file was downloaded */
    if ((size_t)snprintf(dest, sizeof(dest), "%s/%s", tmpdir, TEST_FILE_SMALL) >= sizeof(dest)) ck_abort_msg("path truncated");
    fail_if(access(dest, F_OK) != 0);
    fail_if(compare_with_expected(dest, TEST_FILE_SMALL) != 0);

    /* Re-enable verification for other tests */
    vfile_configure(VFILE_CONF_SSL_VERIFY_NONE, 0);
}
END_TEST

/* the server closes a pooled TLS connection without close_notify; OpenSSL
   answers the bare EOF on the next request with an alert, and that write to
   the closed connection must not kill the process before it reconnects */
START_TEST(test_https_keepalive_no_close_notify)
{
    char url[512], dest[PATH_MAX];

    fail_if(https_port == 0);

    /* SIGPIPE is set to SIG_DFL in http_setup */
    vfile_configure(VFILE_CONF_SSL_VERIFY_NONE, 1);

    snprintf(url, sizeof(url), "https://%s:%d/keepalive-drop/%s",
             server_host, https_port, TEST_FILE_SMALL);
    if ((size_t)snprintf(dest, sizeof(dest), "%s/%s", tmpdir, TEST_FILE_SMALL) >= sizeof(dest)) ck_abort_msg("path truncated");

    fail_if(vf_fetch(url, tmpdir, 0, NULL, NULL) == 0);
    unlink(dest);
    usleep(200000);             /* let the server's FIN arrive */

    fail_if(vf_fetch(url, tmpdir, 0, NULL, NULL) == 0);
    fail_if(access(dest, F_OK) != 0);
    unlink(dest);

    vfile_configure(VFILE_CONF_SSL_VERIFY_NONE, 0);
}
END_TEST

START_TEST(test_https_downgrade_redirect)
{
    char url[512];
    int rc;

    fail_if(https_port == 0);
    fail_if(http_port == 0);

    /* Test that HTTPS->HTTP downgrade redirects are rejected.
     * This is a security measure to prevent MITM attacks that
     * try to downgrade connections from HTTPS to HTTP. */

    /* Disable SSL verification since we're testing redirect handling,
     * not certificate validation */
    vfile_configure(VFILE_CONF_SSL_VERIFY_NONE, 1);

    /* Use HTTPS URL that will redirect to HTTP */
    snprintf(url, sizeof(url), "https://%s:%d/https-downgrade",
             server_host, https_port);

    /* Should fail - HTTPS to HTTP downgrade not allowed */
    rc = vf_fetch(url, tmpdir, VFM_QUITERR, NULL, NULL);
    fail_if(rc != 0);  /* Expect failure - downgrade rejected */

    /* Re-enable verification for other tests */
    vfile_configure(VFILE_CONF_SSL_VERIFY_NONE, 0);
}
END_TEST

START_TEST(test_http_upgrade_redirect)
{
    char url[512], dest[PATH_MAX];
    int rc;

    fail_if(http_port == 0);
    fail_if(https_port == 0);

    /* Test that HTTP->HTTPS upgrade redirects work correctly.
     * Upgrading from HTTP to HTTPS should be allowed (security improvement). */

    /* Disable SSL verification for self-signed test cert */
    vfile_configure(VFILE_CONF_SSL_VERIFY_NONE, 1);

    snprintf(url, sizeof(url), "http://%s:%d/http-upgrade",
             server_host, http_port);

    /* Should succeed - HTTP to HTTPS upgrade is allowed */
    rc = vf_fetch(url, tmpdir, 0, NULL, NULL);
    fail_if(rc == 0);  /* rc == 0 is failure */

    /* Verify file was downloaded (saved as 'http-upgrade', the URL basename) */
    if ((size_t)snprintf(dest, sizeof(dest), "%s/http-upgrade", tmpdir) >= sizeof(dest)) ck_abort_msg("path truncated");
    fail_if(access(dest, F_OK) != 0);
    fail_if(compare_with_expected(dest, TEST_FILE_SMALL) != 0);

    /* Re-enable verification for other tests */
    vfile_configure(VFILE_CONF_SSL_VERIFY_NONE, 0);
}
END_TEST

START_TEST(test_https_redirect_ok)
{
    char url[512], dest[PATH_MAX];
    int rc;

    fail_if(https_port == 0);

    /* Test that HTTPS->HTTPS redirects work correctly.
     * Same-protocol redirects should be followed successfully. */

    /* Disable SSL verification for self-signed test cert */
    vfile_configure(VFILE_CONF_SSL_VERIFY_NONE, 1);

    snprintf(url, sizeof(url), "https://%s:%d/https-redirect",
             server_host, https_port);

    /* Should succeed - same protocol redirect is allowed */
    rc = vf_fetch(url, tmpdir, 0, NULL, NULL);
    fail_if(rc == 0);  /* rc == 0 is failure */

    /* Verify file was downloaded (saved as 'https-redirect', the URL basename) */
    if ((size_t)snprintf(dest, sizeof(dest), "%s/https-redirect", tmpdir) >= sizeof(dest)) ck_abort_msg("path truncated");
    fail_if(access(dest, F_OK) != 0);
    fail_if(compare_with_expected(dest, TEST_FILE_SMALL) != 0);

    /* Re-enable verification for other tests */
    vfile_configure(VFILE_CONF_SSL_VERIFY_NONE, 0);
}
END_TEST

/* === Test Suite Setup === */

static Suite *vhttp_suite(void)
{
    Suite *s = suite_create("vhttp");

    /* HTTP tests */
    TCase *tc_http = tcase_create("http");
    tcase_add_checked_fixture(tc_http, http_setup, http_teardown);
    tcase_add_test(tc_http, test_http_get_small);
    tcase_add_test(tc_http, test_http_keeps_server_mtime);
    tcase_add_test(tc_http, test_http_get_binary);
    tcase_add_test(tc_http, test_http_404);
    tcase_add_test(tc_http, test_http_redirect_302);
    tcase_add_test(tc_http, test_http_redirect_chain);
    tcase_add_test(tc_http, test_http_stat_redirect_failed);
    tcase_add_test(tc_http, test_http_auth_success);
    tcase_add_test(tc_http, test_http_auth_fail);
    tcase_add_test(tc_http, test_http_upgrade_redirect);
    tcase_add_test(tc_http, test_http_proxy_keeps_port);
    tcase_add_test(tc_http, test_http_proxy_keepalive_reuse);
    tcase_add_test(tc_http, test_http_keepalive_dropped);
    tcase_add_test(tc_http, test_http_keepalive_reset);
    suite_add_tcase(s, tc_http);

    /* HTTPS tests */
    TCase *tc_https = tcase_create("https");
    tcase_add_checked_fixture(tc_https, http_setup, http_teardown);
    tcase_add_test(tc_https, test_https_get);
    tcase_add_test(tc_https, test_https_noverify);
    tcase_add_test(tc_https, test_https_downgrade_redirect);
    tcase_add_test(tc_https, test_https_redirect_ok);
    tcase_add_test(tc_https, test_https_keepalive_no_close_notify);
    suite_add_tcase(s, tc_https);

    return s;
}

int main(int argc, char **argv)
{
    (void)argc;  /* unused */
    (void)argv;  /* unused */
    (void)argc;  /* unused */
    (void)argv;  /* unused */
    int nerr;
    Suite *s = vhttp_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_ENV);
    nerr = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (nerr == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
