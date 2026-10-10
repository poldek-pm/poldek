/*
 * Network fault tests for vfile
 * Regression coverage for crashes and connection reuse on broken links
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#include "../vfile.h"
#include "test.h"
#include <trurl/nstr.h>

static char server_host[256] = "127.0.0.1";
static int ftp_port = 0;
static int http_port = 0;
static char tmpdir[PATH_MAX];

#define TEST_FILE_SMALL "small.txt"

static int vfile_initialized = 0;

static void net_setup(void)
{
    static int verbose = 0;

    if (getenv("VERBOSE"))
        verbose = 2;

    if (!vfile_initialized) {
        vfile_configure(VFILE_CONF_VERBOSE, &verbose);
        vfile_configure(VFILE_CONF_CACHEDIR, "/tmp/vfile_test_cache");
        vfile_configure(VFILE_CONF_STUBBORN_RETR, 0); /* disable retrying */
        vfile_setup();
        vfile_initialized = 1;
    }

    const char *host = getenv("TEST_SERVER_HOST");
    const char *fport = getenv("TEST_FTP_PORT");
    const char *hport = getenv("TEST_HTTP_PORT");

    if (host) n_strncpy(server_host, host, sizeof(server_host));
    if (fport) ftp_port = atoi(fport);
    if (hport) http_port = atoi(hport);

    snprintf(tmpdir, sizeof(tmpdir), "/tmp/vfile_nettest_%d_%ld", getpid(),
             (long)time(NULL));
    mkdir(tmpdir, 0755);
}

static void net_teardown(void)
{
    DIR *dir;
    struct dirent *ent;
    char path[PATH_MAX];

    /* successful fetches leave the file and its lock behind */
    if ((dir = opendir(tmpdir)) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;
            if ((size_t)snprintf(path, sizeof(path), "%s/%s", tmpdir,
                                 ent->d_name) < sizeof(path))
                unlink(path);
        }
        closedir(dir);
    }

    rmdir(tmpdir);
}

static int fetch_ftp(const char *path)
{
    char url[512];

    snprintf(url, sizeof(url), "ftp://%s:%d/%s", server_host, ftp_port, path);
    return vf_fetch(url, tmpdir, 0, NULL, NULL);
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

START_TEST(test_ftp_get_small)
{
    fail_if(ftp_port == 0);

    fail_if(fetch_ftp(TEST_FILE_SMALL) == 0);
    fail_if(!have_file(TEST_FILE_SMALL));
}
END_TEST

/* server drops the control connection while poldek waits for the PASV reply;
   used to segfault on vfff_set_err() with the unread response as format */
START_TEST(test_ftp_pasv_control_drop)
{
    fail_if(ftp_port == 0);

    fail_if(fetch_ftp("pasv-drop/" TEST_FILE_SMALL) != 0);
}
END_TEST

/* 227 reply with no address in it; the scan for the first digit used to run
   off the end of the response buffer */
START_TEST(test_ftp_pasv_no_digits)
{
    fail_if(ftp_port == 0);

    fail_if(fetch_ftp("pasv-nodigits/" TEST_FILE_SMALL) != 0);
}
END_TEST

/* an aborted data transfer leaves the reply to RETR unread, so the control
   connection must not be handed out again from the pool */
START_TEST(test_ftp_reuse_after_aborted_transfer)
{
    fail_if(ftp_port == 0);

    /* first transfer succeeds and stamps the connection as alive */
    fail_if(fetch_ftp(TEST_FILE_SMALL) == 0);
    drop_file(TEST_FILE_SMALL);

    fail_if(fetch_ftp("data-reset/" TEST_FILE_SMALL) != 0);

    /* within VCN_ALIVE_TTL, so the pool would skip its NOOP probe */
    fail_if(fetch_ftp(TEST_FILE_SMALL) == 0);
    fail_if(!have_file(TEST_FILE_SMALL));
}
END_TEST

/* stat over a redirect that switches protocol and then fails; the failed
   branch used to dereference the already freed request */
START_TEST(test_stat_redirect_failed)
{
    char url[512];
    struct vf_stat vfst;

    fail_if(http_port == 0);

    snprintf(url, sizeof(url), "http://%s:%d/to-dead-https/" TEST_FILE_SMALL,
             server_host, http_port);

    fail_if(vf_stat(url, tmpdir, &vfst, NULL) != 0);
}
END_TEST

/* the retried request is built from req->url, which used to lose the port */
START_TEST(test_redirect_keeps_port)
{
    char url[512];

    fail_if(http_port == 0 || ftp_port == 0);

    snprintf(url, sizeof(url), "http://%s:%d/to-ftp/" TEST_FILE_SMALL,
             server_host, http_port);

    fail_if(vf_fetch(url, tmpdir, 0, NULL, NULL) == 0);
    fail_if(!have_file(TEST_FILE_SMALL));
}
END_TEST

/* a relative Location is resolved against the request itself, which used to
   drop the port and send the retry to the protocol default */
START_TEST(test_relative_redirect_keeps_port)
{
    char url[512];

    fail_if(http_port == 0 || ftp_port == 0);

    snprintf(url, sizeof(url), "http://%s:%d/rel-to-ftp/" TEST_FILE_SMALL,
             server_host, http_port);

    fail_if(vf_fetch(url, tmpdir, 0, NULL, NULL) == 0);
    fail_if(!have_file(TEST_FILE_SMALL));
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

/* the server closed the pooled control connection without a 421, as on an
   idle timeout; the next fetch can only succeed by reconnecting transparently */
START_TEST(test_ftp_ctrl_closed_idle)
{
    fail_if(ftp_port == 0);

    fail_if(fetch_ftp("ctrl-close-idle/" TEST_FILE_SMALL) == 0);
    drop_file(TEST_FILE_SMALL);

    fail_if(fetch_ftp(TEST_FILE_SMALL) == 0);
    fail_if(!have_file(TEST_FILE_SMALL));
}
END_TEST

/* the reply to RETR breaks off mid-line: the server did answer, so the
   connection is not stale and the request must not be repeated silently */
START_TEST(test_ftp_reply_cut_short)
{
    fail_if(ftp_port == 0);

    fail_if(fetch_ftp(TEST_FILE_SMALL) == 0);    /* pool the connection */
    drop_file(TEST_FILE_SMALL);

    fail_if(fetch_ftp("retr-cut-reply/" TEST_FILE_SMALL) != 0);
}
END_TEST

/* control connection lost after the whole file arrived: the stale-connection
   retry must not kick in and silently download everything again; the fetch
   fails and is left to the resuming retry */
START_TEST(test_ftp_ctrl_drop_after_data)
{
    fail_if(ftp_port == 0);

    fail_if(fetch_ftp(TEST_FILE_SMALL) == 0);    /* pool the connection */
    drop_file(TEST_FILE_SMALL);

    fail_if(fetch_ftp("ctrl-drop-after-data/" TEST_FILE_SMALL) != 0);
}
END_TEST

static Suite *vfnet_suite(void)
{
    Suite *s = suite_create("vfnet");

    TCase *tc = tcase_create("net");
    tcase_add_checked_fixture(tc, net_setup, net_teardown);
    tcase_add_test(tc, test_ftp_get_small);
    tcase_add_test(tc, test_ftp_pasv_control_drop);
    tcase_add_test(tc, test_ftp_pasv_no_digits);
    tcase_add_test(tc, test_ftp_reuse_after_aborted_transfer);
    tcase_add_test(tc, test_stat_redirect_failed);
    tcase_add_test(tc, test_redirect_keeps_port);
    tcase_add_test(tc, test_relative_redirect_keeps_port);
    tcase_add_test(tc, test_http_keepalive_dropped);
    tcase_add_test(tc, test_http_keepalive_reset);
    tcase_add_test(tc, test_ftp_ctrl_closed_idle);
    tcase_add_test(tc, test_ftp_reply_cut_short);
    tcase_add_test(tc, test_ftp_ctrl_drop_after_data);
    suite_add_tcase(s, tc);

    return s;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int nerr;
    Suite *s = vfnet_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_ENV);
    nerr = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (nerr == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
