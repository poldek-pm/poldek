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
#include <trurl/nbuf.h>

static char server_host[256] = "127.0.0.1";
static int ftp_port = 0;
static int http_port = 0;
static char tmpdir[PATH_MAX];

#define TEST_FILE_SMALL "small.txt"

static int vfile_initialized = 0;

/* what vfile logged as errors */
static tn_buf *logged_err;

static void log_cb(int pri, const char *fmt, va_list ap)
{
    if (pri & VFILE_LOG_ERR)
        n_buf_vprintf(logged_err, fmt, ap);
}

static void net_setup(void)
{
    static int verbose = 0;

    save_tz();

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

    restore_tz();

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

/* is the fetched file what the server has? */
static int file_as_served(const char *name)
{
    char path[PATH_MAX], got[4096], served[4096];
    int ngot, nserved;
    FILE *f;

    if ((size_t)snprintf(path, sizeof(path), "%s/%s", tmpdir, name) >= sizeof(path))
        ck_abort_msg("path truncated");
    if ((f = fopen(path, "r")) == NULL)
        return 0;
    ngot = fread(got, 1, sizeof(got), f);
    fclose(f);

    if ((size_t)snprintf(path, sizeof(path), "%s/%s", getenv("TEST_DATA_DIR"),
                         name) >= sizeof(path))
        ck_abort_msg("path truncated");
    if ((f = fopen(path, "r")) == NULL)
        return 0;
    nserved = fread(served, 1, sizeof(served), f);
    fclose(f);

    return ngot > 0 && ngot == nserved && memcmp(got, served, ngot) == 0;
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

/* the fetched file gets the MDTM the server gives, whatever the local zone */
START_TEST(test_ftp_keeps_server_mtime)
{
    char dest[PATH_MAX];
    struct stat st;
    time_t expected = 1720000000;   /* July, summer time in the zone below */

    fail_if(ftp_port == 0);
    use_zone_with_dst();
    fail_if(set_server_mtime(TEST_FILE_SMALL, expected) != 0);

    fail_if(fetch_ftp(TEST_FILE_SMALL) == 0);
    if ((size_t)snprintf(dest, sizeof(dest), "%s/%s", tmpdir, TEST_FILE_SMALL) >= sizeof(dest))
        ck_abort_msg("path truncated");
    fail_if(stat(dest, &st) != 0);
    ck_assert_msg(st.st_mtime == expected,
                  "mtime %ld != server %ld", (long)st.st_mtime, (long)expected);
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

/* a reply line longer than vfff's line buffer is named as such, as it is
   over HTTP */
START_TEST(test_ftp_reply_line_too_long)
{
    fail_if(ftp_port == 0);

    logged_err = n_buf_new(0);
    vfile_configure(VFILE_CONF_LOGCB, log_cb);
    fail_if(fetch_ftp("retr-long-reply/" TEST_FILE_SMALL) != 0);
    vfile_configure(VFILE_CONF_LOGCB, NULL);

    fail_if(n_buf_size(logged_err) == 0 ||
            strstr(n_buf_ptr(logged_err), "vfff: response line too long\n") == NULL,
            "%s", n_buf_size(logged_err) ? (char *)n_buf_ptr(logged_err) : "");
    n_buf_free(logged_err);
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

/* a reply to RETR that says "not now" (4yz) is reported as the server gave
   it and the fetch is tried again; "no such file" is for a file that is not
   there, and is final */
START_TEST(test_ftp_retr_450_retried)
{
    const char *err;

    fail_if(ftp_port == 0);

    logged_err = n_buf_new(0);
    vfile_configure(VFILE_CONF_LOGCB, log_cb);
    vfile_configure(VFILE_CONF_STUBBORN_RETR, 1);
    vfile_configure(VFILE_CONF_STUBBORN_NRETRIES, 2);

    fail_if(fetch_ftp("retr-busy/" TEST_FILE_SMALL) == 0, "not retried after 450");
    fail_if(!have_file(TEST_FILE_SMALL));
    err = n_buf_size(logged_err) ? (char *)n_buf_ptr(logged_err) : "";
    ck_assert_str_eq(err, "vfff: /retr-busy/" TEST_FILE_SMALL ": FTP 450 File busy\n");

    n_buf_clean(logged_err);
    fail_if(fetch_ftp("no-such-file") != 0);
    err = n_buf_size(logged_err) ? (char *)n_buf_ptr(logged_err) : "";
    ck_assert_str_eq(err, "vfff: /no-such-file: no such file (serv said: no such file)\n");

    vfile_configure(VFILE_CONF_STUBBORN_RETR, 0);
    vfile_configure(VFILE_CONF_LOGCB, NULL);
    n_buf_free(logged_err);
}
END_TEST

/* with a 421 the server closes the connection: the next try needs a new one */
START_TEST(test_ftp_retr_421_retried)
{
    fail_if(ftp_port == 0);

    vfile_configure(VFILE_CONF_STUBBORN_RETR, 1);
    vfile_configure(VFILE_CONF_STUBBORN_NRETRIES, 2);

    fail_if(fetch_ftp("retr-421/" TEST_FILE_SMALL) == 0, "not retried after 421");
    fail_if(!have_file(TEST_FILE_SMALL));

    vfile_configure(VFILE_CONF_STUBBORN_RETR, 0);
}
END_TEST

/* a server may begin the transfer with 125 as well as with 150 */
START_TEST(test_ftp_retr_125)
{
    fail_if(ftp_port == 0);

    fail_if(fetch_ftp("retr-125/" TEST_FILE_SMALL) == 0);
    fail_if(!file_as_served(TEST_FILE_SMALL));
}
END_TEST

/* and end it with 250 as well as with 226 */
START_TEST(test_ftp_retr_end_250)
{
    fail_if(ftp_port == 0);

    fail_if(fetch_ftp("retr-end-250/" TEST_FILE_SMALL) == 0);
    fail_if(!file_as_served(TEST_FILE_SMALL));
}
END_TEST

static Suite *vftp_suite(void)
{
    Suite *s = suite_create("vftp");

    TCase *tc = tcase_create("net");
    tcase_add_checked_fixture(tc, net_setup, net_teardown);
    tcase_add_test(tc, test_ftp_get_small);
    tcase_add_test(tc, test_ftp_keeps_server_mtime);
    tcase_add_test(tc, test_ftp_pasv_control_drop);
    tcase_add_test(tc, test_ftp_pasv_no_digits);
    tcase_add_test(tc, test_ftp_reuse_after_aborted_transfer);
    tcase_add_test(tc, test_redirect_keeps_port);
    tcase_add_test(tc, test_relative_redirect_keeps_port);
    tcase_add_test(tc, test_ftp_ctrl_closed_idle);
    tcase_add_test(tc, test_ftp_reply_cut_short);
    tcase_add_test(tc, test_ftp_reply_line_too_long);
    tcase_add_test(tc, test_ftp_ctrl_drop_after_data);
    tcase_add_test(tc, test_ftp_retr_450_retried);
    tcase_add_test(tc, test_ftp_retr_421_retried);
    tcase_add_test(tc, test_ftp_retr_125);
    tcase_add_test(tc, test_ftp_retr_end_250);
    suite_add_tcase(s, tc);

    return s;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int nerr;
    Suite *s = vftp_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_ENV);
    nerr = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (nerr == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
