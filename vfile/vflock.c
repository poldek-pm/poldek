/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <trurl/nstr.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "vfile.h"
#include "vfile_intern.h"

static
int vf_lockfile(const char *lockfile)
{
    struct flock fl;
    struct stat  fst, st;
    int    fd, rc;

    DBGF("%s\n", lockfile);
 l_again:
    if ((fd = open(lockfile, O_RDWR | O_CREAT, 0644)) < 0) {
        vf_logerr("open %s: %m\n", lockfile);
        return -1;
    }

    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    if (fcntl(fd, F_SETLK, &fl) == -1) {
        int an_errno = errno;
        if (errno != EAGAIN || errno != EACCES)
            if (*vfile_verbose > 1)
                vf_logerr("fcntl %s: %m\n", lockfile);

        close(fd);
        fd = 0;
        if (an_errno == ENOLCK)
            fd = -1;

    } else {
        char buf[64];

        if (fstat(fd, &fst) != 0 ||
            ((rc = stat(lockfile, &st)) != 0 && errno != ENOENT)) {
            vf_logerr("stat %s: %m\n", lockfile);
            close(fd);
            return -1;
        }

        /* the lock is of the file under that name only: one opened before
           its holder removed it on release is not */
        if (rc != 0 || fst.st_dev != st.st_dev || fst.st_ino != st.st_ino) {
            close(fd);
            goto l_again;
        }

        if (ftruncate(fd, 0) != 0)
            vf_logerr("ftruncate %s: %m\n", lockfile);

        int n = snprintf(buf, sizeof(buf), "%d", getpid());
        if (write(fd, buf, n) != n)
            vf_logerr("write %s: %m\n", lockfile);
    }

    return fd;
}

#if 0
static
pid_t vf_readlockfile(const char *lockfile)
{
    char buf[256];
    int fd, nread;
    pid_t pid;

    fd = open(lockfile, O_RDONLY, 0444);
    if(fd < 0)
        return -1;

    nread = read(fd, buf, sizeof(buf));
    close(fd);

    if (sscanf(buf, "%d", &pid) == 1)
        return pid;

    return -1;
}
#endif

/* the locks this process holds: a fcntl() lock is of the process, which
   so gets a lock it holds again at once, and the close of either
   descriptor would let both go; one taken again is only counted */
struct held_lock {
    struct held_lock *next;
    pid_t pid;                  /* another one's: inherited by fork() */
    int   fd;
    int   count;                /* 0: inherited, released, not yet closed */
    char  path[];
};

static struct held_lock *held_locks = NULL;

#ifdef ENABLE_THREADS
# include <pthread.h>
static pthread_mutex_t hl_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t hl_atfork_once = PTHREAD_ONCE_INIT;

/* a child has only the thread that called fork(): one forked while
   another thread held the mutex would find it held for good, so fork()
   waits for it */
static void hl_atfork_lock(void)   { pthread_mutex_lock(&hl_mutex); }
static void hl_atfork_unlock(void) { pthread_mutex_unlock(&hl_mutex); }

/* pthread_once() does not run it again: after a failure a fork() while
   another thread holds the mutex may leave the child waiting for it */
static void hl_atfork(void)
{
    int rc = pthread_atfork(hl_atfork_lock, hl_atfork_unlock, hl_atfork_unlock);

    if (rc != 0)
        vf_log(VFILE_LOG_WARN, "pthread_atfork: %s\n", strerror(rc));
}

static void hl_mutex_lock(void)
{
    pthread_once(&hl_atfork_once, hl_atfork);
    pthread_mutex_lock(&hl_mutex);
}

# define hl_mutex_unlock()   pthread_mutex_unlock(&hl_mutex)
#else
# define hl_mutex_lock()     ((void) 0)
# define hl_mutex_unlock()   ((void) 0)
#endif

/* the lock of path this process holds */
static struct held_lock **find_held_lock(const char *path)
{
    struct held_lock **h;
    pid_t pid = getpid();

    for (h = &held_locks; *h; h = &(*h)->next)
        if ((*h)->pid == pid && strcmp((*h)->path, path) == 0)
            return h;

    return NULL;
}

/* the entry of vflock: of this process, or inherited with its descriptor */
static struct held_lock **find_held_entry(const struct vflock *vflock)
{
    struct held_lock **h;

    for (h = &held_locks; *h; h = &(*h)->next)
        if ((*h)->count > 0 && (*h)->fd == vflock->fd &&
            strcmp((*h)->path, vflock->path) == 0)
            return h;

    return NULL;
}

/* the inherited ones of path released while this process held its own
   lock of it, taken off the list */
static struct held_lock *take_released_inherited(const char *path)
{
    struct held_lock **h = &held_locks, *taken = NULL, *e;

    while ((e = *h) != NULL) {
        if (e->count == 0 && strcmp(e->path, path) == 0) {
            *h = e->next;
            e->next = taken;
            taken = e;
        } else {
            h = &e->next;
        }
    }

    return taken;
}

/* the name goes while the lock is held: a process that locks the file
   after the close finds it is not under the name (see vf_lockfile()) */
static int remove_lockfile(const char *path)
{
    if (!vf_valid_path(path))   /* which has told why */
        return 0;

    if (unlink(path) != 0 && errno != ENOENT) {
        vf_logerr("unlink %s: %m\n", path);
        return 0;
    }

    return 1;
}

/* one attempt; 0 when another process holds the lock */
static int lock_once(const char *path)
{
    struct held_lock **h, *held;
    int fd;

    hl_mutex_lock();
    if ((h = find_held_lock(path)) != NULL) {
        (*h)->count++;
        fd = (*h)->fd;

    } else if ((fd = vf_lockfile(path)) > 0) {
        size_t len = strlen(path) + 1;

        held = n_malloc(sizeof(*held) + len);
        held->pid = getpid();
        held->fd = fd;
        held->count = 1;
        memcpy(held->path, path, len);
        held->next = held_locks;
        held_locks = held;
    }
    hl_mutex_unlock();
    return fd;
}

static
int vf_lock_obtain(const char *path)
{
    int fd = 0, waited = 0, delay = 100;   /* ms */
    char *dirname;

    n_strdupap(path, &dirname);
    dirname = n_dirname(dirname);

    while (fd == 0) {
        if (vfile_sigint_reached(0)) {
            fd = 0;
            break;
        }
        if ((fd = lock_once(path)) == 0) {
            if (waited == 1000)
                vf_loginfo(_("Waiting for lock %s...\n"), dirname);

            if (waited >= 1000) {
                delay *= 2;
                if (delay > 1000)
                    delay = 1000; /* const delay after first second */
            }

            if (waited <= 1000)
                waited += delay;

            usleep(delay * 1000);

        } else if (fd < 0) {
            fd = 0;
            break;
        }
    }

    return fd;
}

/* the descriptor of held, and those of the inherited ones of its file */
static void close_locks(struct held_lock *held, struct held_lock **inherited)
{
    struct held_lock *e;

    close(held->fd);
    while ((e = *inherited) != NULL) {
        *inherited = e->next;
        close(e->fd);
        free(e);
    }
}

void vf_lock_release(struct vflock *vflock)
{
    struct held_lock **h, *held = NULL, *inherited = NULL;
    int close_after = 0;

    DBGF("%d %s\n", vflock->fd, vflock->path);
    hl_mutex_lock();
    h = find_held_entry(vflock);
    n_assert(h != NULL);        /* every vflock is counted in it */

    if (--(*h)->count > 0) {
        ;
    } else if ((*h)->pid != getpid()) {
        /* of an inherited one only the descriptor is of this process, and
           its close lets go a lock this process holds of the same file:
           with one held it waits for its release, else it goes now, with
           those that waited before a fork(), and before a thread locks */
        if (find_held_lock((*h)->path) == NULL) {
            held = *h;
            *h = held->next;
            inherited = take_released_inherited(held->path);
            close_locks(held, &inherited);
        }

    } else {
        held = *h;
        *h = held->next;
        inherited = take_released_inherited(held->path);

        /* with the name gone a thread that locks it makes a new file;
           with it left the close must come before that thread opens it */
        if (remove_lockfile(held->path))
            close_after = 1;
        else
            close_locks(held, &inherited);
    }
    hl_mutex_unlock();

    if (close_after)
        close_locks(held, &inherited);
    free(held);
    free(vflock);
}

/* the directory is the scope of its lock: the name of the file is the same
   whichever path the directory is reached by, and short however deep it is */
static const char lockfile_name[] = ".vflock";

struct vflock *vf_lockdir(const char *path)
{
    char lockpath[PATH_MAX];
    struct vflock *vflock = NULL;
    int fd, n;

    if (strlen(path) + sizeof(lockfile_name) + 1 > sizeof(lockpath)) {
        vf_logerr(_("%s: unable to obtain lock: %s\n"), path, strerror(ENAMETOOLONG));
        return NULL;
    }

    n = n_snprintf(lockpath, sizeof(lockpath), "%s/%s", path, lockfile_name);
    if (!(fd = vf_lock_obtain(lockpath))) {
        vf_logerr(_("%s: unable to obtain lock\n"), path);
        return NULL;
    }
    vflock = n_malloc(sizeof(*vflock) + n + 1);
    vflock->fd = fd;
    memcpy(vflock->path, lockpath, n + 1);
    return vflock;
}


struct vflock *vf_lock_mkdir(const char *path)
{
    char tmp[PATH_MAX], *dn, *bn;
    struct vflock *vflock = NULL, *subdir_vflock = NULL;
    struct stat st;


    if (!vf_valid_path(path))
        return NULL;

    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return vf_lockdir(path);

    DBGF("** vf_lock_mkdir %s\n", path);

    n_snprintf(tmp, sizeof(tmp), "%s", path);
    n_basedirnam(tmp, &dn, &bn);

    if ((subdir_vflock = vf_lockdir(dn)) == NULL)
        return NULL;

    /* another process may have made it since the stat() above */
    if (mkdir(path, 0750) != 0 &&
        (errno != EEXIST || stat(path, &st) != 0 || !S_ISDIR(st.st_mode))) {
        vf_logerr("%s: mkdir: %m\n", path);
    } else {
        vflock = vf_lockdir(path);
    }

    vf_lock_release(subdir_vflock);
    return vflock;
}
