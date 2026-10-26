#include <sys/resource.h>
#include "test.h"
#include "poldek_util.h"

/* when cp cannot be started, copying fails and stdin is left alone */
START_TEST (test_copy_file_cp_not_started) {
    char dir[PATH_MAX], src[PATH_MAX], dst[PATH_MAX], buf[16];
    const char *tmpdir = getenv_path("TMPDIR");
    struct rlimit rl, nofd;
    int in[2], fd, rc;

    n_snprintf(dir, sizeof(dir), "%s/poldek-copy-file-XXXXXX",
               tmpdir ? tmpdir : "/tmp");
    fail_if(mkdtemp(dir) == NULL, "mkdtemp %s: %s", dir, strerror(errno));
    n_snprintf(src, sizeof(src), "%s/src", dir);
    n_snprintf(dst, sizeof(dst), "%s/dst", dir);

    FILE *f = fopen(src, "wx");
    fail_if(f == NULL, "cannot create %s: %s", src, strerror(errno));
    fputs("data\n", f);
    fclose(f);

    /* an answer typed ahead, waiting on stdin */
    fail_if(pipe(in) != 0, "pipe: %s", strerror(errno));
    fail_if(write(in[1], "y\n", 2) != 2, "write: %s", strerror(errno));
    dup2(in[0], STDIN_FILENO);
    close(in[0]);

    /* no free descriptor for the pipe from cp, so p_open() fails */
    fail_if(getrlimit(RLIMIT_NOFILE, &rl) != 0, "getrlimit: %s", strerror(errno));
    fd = dup(STDIN_FILENO);     /* the lowest free one */
    close(fd);
    nofd = rl;
    nofd.rlim_cur = fd;
    fail_if(setrlimit(RLIMIT_NOFILE, &nofd) != 0, "setrlimit: %s", strerror(errno));
    rc = poldek_util_copy_file(src, dst);
    setrlimit(RLIMIT_NOFILE, &rl);

    fail_if(rc, "copying reported success, but cp never ran");
    fail_if(access(dst, F_OK) == 0, "%s exists", dst);

    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    fail_unless(read(STDIN_FILENO, buf, sizeof(buf)) == 2, "stdin was read");

    close(in[1]);
    unlink(src);
    rmdir(dir);
}
END_TEST

NTEST_RUNNER("copy_file", test_copy_file_cp_not_started);
