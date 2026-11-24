/*
  Preloaded into poldek by sh/01-create-repo: closing the file written whose
  path ends with WRITE_ERROR, or its temporary .WRITE_ERROR.tmp.XXXXXX, fails
  as it would on a full disk.  The digest and the list of diffs are smaller
  than a buffer, so their bytes reach the disk only from the close, and only a
  close that reports the failure can be tested.
*/
/* RTLD_NEXT is a GNU extension; the build defines it too, so do not redefine
   it under -Werror */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif
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
    const char *base;
    ssize_t n;
    size_t len, namelen;

    if (fd < 0 || name == NULL || *name == '\0')
        return 0;

    /* a file read before it is rewritten is closed as usual */
    if ((fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDONLY)
        return 0;

    snprintf(fdpath, sizeof(fdpath), "/proc/self/fd/%d", fd);
    if ((n = readlink(fdpath, path, sizeof(path) - 1)) <= 0)
        return 0;
    path[n] = '\0';

    len = strlen(path);
    namelen = strlen(name);
    if (len >= namelen && strcmp(path + len - namelen, name) == 0)
        return 1;

    /* the hidden .<name>.tmp.XXXXXX a file is replaced from */
    if ((base = strrchr(path, '/')) == NULL)
        return 0;
    base++;

    return base[0] == '.' && strncmp(base + 1, name, namelen) == 0 &&
        strncmp(base + 1 + namelen, ".tmp.", 5) == 0 &&
        strlen(base + 1 + namelen + 5) == 6;
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
