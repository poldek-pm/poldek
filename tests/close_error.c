/*
  Preloaded into poldek by sh/01-create-repo: closing the file whose path ends
  with WRITE_ERROR fails as it would on a full disk.  The digest and the list
  of diffs are smaller than a buffer, so their bytes reach the disk only from
  the close, and only a close that reports the failure can be tested.
*/
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

__attribute__((constructor))
static void only_poldek(void)
{
    unsetenv("LD_PRELOAD");     /* not for rpm or anything else poldek runs */
}

static int targeted(int fd)
{
    const char *name = getenv("WRITE_ERROR");
    char fdpath[64], path[4096];
    ssize_t n;
    size_t len, namelen;

    if (fd < 0 || name == NULL || *name == '\0')
        return 0;

    snprintf(fdpath, sizeof(fdpath), "/proc/self/fd/%d", fd);
    if ((n = readlink(fdpath, path, sizeof(path) - 1)) <= 0)
        return 0;
    path[n] = '\0';

    len = strlen(path);
    namelen = strlen(name);
    return len >= namelen && strcmp(path + len - namelen, name) == 0;
}

/* a stdio stream is closed by fclose(), which does not go through close() */
__attribute__((visibility("default")))
int fclose(FILE *f)
{
    static int (*next)(FILE *);
    int fd;

    if (next == NULL)
        next = (int (*)(FILE *))dlsym(RTLD_NEXT, "fclose");

    fd = fileno(f);
    if (targeted(fd)) {
        int full = open("/dev/full", O_WRONLY);

        if (full >= 0) {        /* the flush of what is buffered now fails */
            dup2(full, fd);
            close(full);
        }
    }

    return next(f);
}

/* zlib and zstd write through write() and close() */
__attribute__((visibility("default")))
int close(int fd)
{
    static int (*next)(int);

    if (next == NULL)
        next = (int (*)(int))dlsym(RTLD_NEXT, "close");

    if (targeted(fd)) {
        errno = ENOSPC;
        return -1;
    }

    return next(fd);
}
