/*
  pkgdir__rmf() of a directory that cannot be read after it was locked
  leaves it unlocked; one of a file it cannot remove says so.
*/
#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>
#include <vfile/vfile.h>
#include "test.h"
#include "pkgdir_intern.h"

static int opendir_errno;       /* opendir() fails with it */
static int unlink_errno;        /* unlink() of a file not a lock fails with it */

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

/* vf_localunlink() calls it */
__attribute__((visibility("default")))
int unlink(const char *path)
{
    static int (*real_unlink)(const char *);
    const char *bn = strrchr(path, '/');

    if (real_unlink == NULL)
        real_unlink = (int (*)(const char *))dlsym(RTLD_NEXT, "unlink");
    if (unlink_errno && strncmp(bn ? bn + 1 : path, ".vflock", 7) != 0) {
        errno = unlink_errno;
        return -1;
    }
    return real_unlink(path);
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

/* a file that cannot be removed makes it fail, and the directory is left
   unlocked; the next run removes the file */
START_TEST (test_rmf_unlink_error) {
    char dir[PATH_MAX], file[PATH_MAX];
    int verbose = -1, rc;
    FILE *f;

    vfile_configure(VFILE_CONF_VERBOSE, &verbose);
    n_snprintf(dir, sizeof(dir), "%s", NTEST_TMPPATH("rmf-unlink"));
    mkdir(dir, 0755);
    vfile_configure(VFILE_CONF_CACHEDIR, dir);  /* vf_localunlink() takes its files */
    n_snprintf(file, sizeof(file), "%s/f", dir);
    fail_if((f = fopen(file, "w")) == NULL, "%s: %s", file, strerror(errno));
    fclose(f);

    unlink_errno = EACCES;
    rc = pkgdir__rmf(dir, NULL, 0);
    unlink_errno = 0;
    expect_int(rc, 0);
    expect_int(nlockfiles(dir), 0);
    expect_int(access(file, F_OK), 0);

    expect_int(pkgdir__rmf(dir, NULL, 0), 1);
    expect_int(access(file, F_OK), -1);
    rmdir(dir);
}
END_TEST

NTEST_RUNNER("pkgdir_clean", test_rmf_opendir_error, test_rmf_unlink_error);
