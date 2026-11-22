/*
  pkgdir__rmf() of a directory that cannot be read after it was locked
  leaves it unlocked.
*/
#include <dirent.h>
#include <dlfcn.h>
#include "test.h"
#include "pkgdir_intern.h"

static int opendir_errno;       /* opendir() fails with it */

/* pkgdir__rmf() calls it */
__attribute__((visibility("default")))
DIR *opendir(const char *path)
{
    static DIR *(*real_opendir)(const char *);

    if (real_opendir == NULL)
        real_opendir = (DIR *(*)(const char *))dlsym(RTLD_NEXT, "opendir");
    if (opendir_errno) {
        errno = opendir_errno;
        return NULL;
    }
    return real_opendir(path);
}

/* the number of files of locks in dir */
static int nlockfiles(const char *dir)
{
    struct dirent *ent;
    DIR *d;
    int n = 0;

    fail_if((d = opendir(dir)) == NULL, "opendir %s: %s", dir, strerror(errno));
    while ((ent = readdir(d)) != NULL)
        n += strncmp(ent->d_name, ".vflock_", 8) == 0;
    closedir(d);
    return n;
}

START_TEST (test_rmf_opendir_error) {
    char dir[PATH_MAX];
    int rc;

    n_snprintf(dir, sizeof(dir), "%s", NTEST_TMPPATH("rmf-opendir"));
    mkdir(dir, 0755);

    opendir_errno = EMFILE;
    rc = pkgdir__rmf(dir, NULL, 0);
    opendir_errno = 0;
    expect_int(rc, 1);
    expect_int(nlockfiles(dir), 0);
    rmdir(dir);
}
END_TEST

NTEST_RUNNER("pkgdir_clean", test_rmf_opendir_error);
