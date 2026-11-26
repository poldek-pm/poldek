#ifdef HAVE_CONFIG_H            /* _FILE_OFFSET_BITS as libvfile has it */
# include "config.h"
#endif

#include <dirent.h>
#include <dlfcn.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/wait.h>
#ifdef ENABLE_THREADS
# include <pthread.h>
#endif

#include <trurl/nbuf.h>
#include <trurl/nstr.h>

#include "test.h"

void append(const char *path, int vft_io)
{
    struct vfile *vf;

    vf = vfile_open(path, vft_io, VFM_APPEND);
    fail_if(vf == NULL);
    fail_if(n_stream_write(vf->vf_tnstream, "foo\n", 4) != 4);
    vfile_close(vf);
}

START_TEST (test_vfile_append) {
    int ec;

    char *gzpath = strdup(NTEST_TMPPATH("tmp.txt.gz"));
    append(gzpath, VFT_TRURLIO);
    append(gzpath, VFT_TRURLIO);
    append(gzpath, VFT_TRURLIO);

    char *path = strdup(NTEST_TMPPATH("tmp.txt"));
    append(path, VFT_TRURLIO);
    append(path, VFT_TRURLIO);
    append(path, VFT_TRURLIO);


    char cmd[1024];
    n_snprintf(cmd, sizeof(cmd), "zdiff %s %s\n", gzpath, path);
    ec = system(cmd);
    fail_if(ec != 0);
}
END_TEST

START_TEST (test_valid_path) {
    char *inv_paths[] = {
        "../ala/ma/kota",
        "foo/bar",
        "/ala/../foo",
        NULL
    };
    char *valid_paths[] = {
        "/",
        "/ala/ma/kota",
        "/foo/..bar",
        "/ala../foo",
	"/home/foo/.poldek-cache/_www.rpm.xx.redhat-7.3../.vflock__home.foo..poldek-cache..www.rpm.xx.redhat-7.3..",
        NULL
    };
    int i;

    i = 0;
    while (inv_paths[i] != NULL) {
        fail_if(vf_valid_path(inv_paths[i]),
                "validated invalid '%s'", inv_paths[i]);
        i++;
    }

    i = 0;
    while (valid_paths[i] != NULL) {
        fail_if(!vf_valid_path(valid_paths[i]),
                "invalid valid '%s'", valid_paths[i]);
        i++;
    }
}
END_TEST


START_TEST (test_url_as_path_percent) {
    char buf[PATH_MAX];

    vf_url_as_path(buf, sizeof(buf), "http://host/my%20dir/a%20file.rpm");
    fail_if(strcmp(buf, "http_host.my.20dir/a%20file.rpm") != 0,
            "got '%s'", buf);
}
END_TEST


/* only the password of login:password@host is masked, as vf_parse_url() splits it */
START_TEST (test_url_hidepasswd) {
    const char *urls[][2] = {
        { "ftp://user:secret@host/dir/", "ftp://user:xxxxxx@host/dir/" },
        { "ftp://user:p@ss@host/dir/", "ftp://user:xxxx@host/dir/" },
        { "http://host:8080/r1@x/", "http://host:8080/r1@x/" },
        { "http://host/a:b@c/", "http://host/a:b@c/" },
    };
    char buf[256];
    unsigned i;

    for (i = 0; i < sizeof(urls) / sizeof(urls[0]); i++) {
        const char *url = vf_url_hidepasswd(buf, sizeof(buf), urls[i][0]);
        fail_if(strcmp(url, urls[i][1]) != 0, "%s: got '%s'", urls[i][0], url);
    }
}
END_TEST

/* a URL longer than the buffer is cut, and what is left of the password is masked */
START_TEST (test_url_hidepasswd_short_buffer) {
    const char *urls[][2] = {
        { "http://u:secretpassword@host/", "http://u:xxxxxx" },
        { "http://login-of-twenty:pw@host/", "http://login-of" },
    };
    char buf[16];
    unsigned i;

    for (i = 0; i < sizeof(urls) / sizeof(urls[0]); i++) {
        const char *url = vf_url_hidepasswd(buf, sizeof(buf), urls[i][0]);
        fail_if(strcmp(url, urls[i][1]) != 0, "%s: got '%s'", urls[i][0], url);
    }
}
END_TEST


/* %D together with a multi-URL %Pn: there is no single basename, but the
   fetcher must not dereference the NULL one */
START_TEST (test_fetcha_ext_multi_dirbn) {
    int verbose = 0;
    tn_array *protos, *urls;

    vfile_configure(VFILE_CONF_VERBOSE, &verbose);

    protos = n_array_new(2, NULL, NULL);
    n_array_push(protos, "testfetch");
    fail_unless(vfile_register_ext_handler("testfetch", protos,
                                           "/bin/true %D %Pn"),
                "cannot register the test fetcher");
    n_array_free(protos);

    urls = n_array_new(2, NULL, NULL);
    n_array_push(urls, "testfetch://host/a.rpm");
    n_array_push(urls, "testfetch://host/b.rpm");

    fail_unless(vf_fetcha(urls, NTEST_TMPPATH("vfile-fetcha"),
                          VF_FETCH_NOPROGRESS, NULL, 1, 2) == 1,
                "vf_fetcha failed");

    n_array_free(urls);
}
END_TEST


/* "://" makes a URL only when what precedes it looks like a scheme; a path
   that happens to contain "://" is still a path */
START_TEST (test_url_type_scheme) {
    fail_unless(vf_url_type("/abs/path") == VFURL_PATH);
    fail_unless(vf_url_type("relative/path") == VFURL_PATH);
    fail_unless(vf_url_type("http://host/x") == VFURL_HTTP);
    fail_unless(vf_url_type("custom://host/x") == VFURL_UNKNOWN);
    fail_unless(vf_url_type("s3://host/x") == VFURL_UNKNOWN);
    fail_unless(vf_url_type("git+ssh://host/x") == VFURL_UNKNOWN);
    fail_unless(vf_url_type("3s://host/x") == VFURL_PATH,
                "a scheme begins with a letter");
    fail_unless(vf_url_type("./dir/a://b") == VFURL_PATH,
                "a path containing '://' is not a URL");
}
END_TEST


/* a failed open says why in errno: what open() said of a local file, EIO
   from a fetch command, which gives only its exit code */
START_TEST (test_open_errno) {
    int verbose = -1;
    tn_array *protos;

    vfile_configure(VFILE_CONF_VERBOSE, &verbose);
    vfile_configure(VFILE_CONF_CACHEDIR, NTEST_TMPPATH("vfile-cache"));

    errno = 0;
    fail_unless(vfile_open(NTEST_TMPPATH("no-such-file"), VFT_IO, VFM_RO) == NULL);
    fail_unless(errno == ENOENT, "local: errno %d", errno);

    protos = n_array_new(2, NULL, NULL);
    n_array_push(protos, "failfetch");
    fail_unless(vfile_register_ext_handler("failfetch", protos,
                                           "/bin/false %P %D"),
                "cannot register the test fetcher");
    n_array_free(protos);

    errno = 0;
    fail_unless(vfile_open("failfetch://host/x.toc", VFT_IO, VFM_RO) == NULL);
    fail_unless(errno == EIO, "fetch command: errno %d", errno);
}
END_TEST


static const char *mkdir_meanwhile;

/* mkdir() of mkdir_meanwhile finds it made by another process; libvfile
   calls this one */
__attribute__((visibility("default")))
int mkdir(const char *path, mode_t mode)
{
    static int (*real_mkdir)(const char *, mode_t);

    if (real_mkdir == NULL)
        real_mkdir = (int (*)(const char *, mode_t))dlsym(RTLD_NEXT, "mkdir");
    if (mkdir_meanwhile && strcmp(path, mkdir_meanwhile) == 0) {
        mkdir_meanwhile = NULL;
        real_mkdir(path, mode);
    }
    return real_mkdir(path, mode);
}

/* of two processes making one directory, the second locks the one the first made */
START_TEST (test_lock_mkdir_made_meanwhile) {
    char dir[PATH_MAX];
    struct vflock *lock;

    n_snprintf(dir, sizeof(dir), "%s", NTEST_TMPPATH("vflock-meanwhile"));
    rmdir(dir);
    mkdir_meanwhile = dir;
    lock = vf_lock_mkdir(dir);
    fail_if(mkdir_meanwhile != NULL, "%s: mkdir() not called", dir);
    fail_if(lock == NULL, "%s: no lock of the directory made meanwhile", dir);
    vf_lock_release(lock);
    rmdir(dir);
}
END_TEST


/* a directory whose name only starts with the name of the cache directory
   is not in it: its cache path is made, and its files are not removed */
START_TEST (test_cachedir_prefix) {
    char cache[PATH_MAX], beside[PATH_MAX], path[PATH_MAX], file[PATH_MAX];
    int verbose = -1;
    FILE *f;

    vfile_configure(VFILE_CONF_VERBOSE, &verbose);
    n_snprintf(cache, sizeof(cache), "%s", NTEST_TMPPATH("vfcache"));
    n_snprintf(beside, sizeof(beside), "%s-repo", cache);
    mkdir(cache, 0755);
    mkdir(beside, 0755);
    vfile_configure(VFILE_CONF_CACHEDIR, cache);

    vf_cachepath(path, sizeof(path), beside);
    fail_if(strcmp(path, beside) == 0, "%s: taken for a directory in %s", beside, cache);

    n_snprintf(file, sizeof(file), "%s/sub", cache);
    vf_cachepath(path, sizeof(path), file);
    fail_if(strcmp(path, file) != 0, "%s: got %s", file, path);

    n_snprintf(file, sizeof(file), "%s/file", beside);
    fail_if((f = fopen(file, "w")) == NULL, "%s: %s", file, strerror(errno));
    fclose(f);
    fail_if(vf_localunlink(file), "%s: removed as a file of %s", file, cache);
    fail_if(access(file, F_OK) != 0, "%s: removed", file);
    unlink(file);

    /* the cache directory given with trailing slashes is the same one */
    n_snprintf(path, sizeof(path), "%s//", cache);
    vfile_configure(VFILE_CONF_CACHEDIR, path);
    n_snprintf(file, sizeof(file), "%s/sub", cache);
    vf_cachepath(path, sizeof(path), file);
    fail_if(strcmp(path, file) != 0, "%s: got %s with the cache given as %s//", file, path, cache);

    n_snprintf(file, sizeof(file), "%s/file", cache);
    fail_if((f = fopen(file, "w")) == NULL, "%s: %s", file, strerror(errno));
    fclose(f);
    fail_if(!vf_localunlink(file), "%s: not removed with the cache given as %s//", file, cache);

    rmdir(beside);
    rmdir(cache);
}
END_TEST


/* a process that takes the lock of dir and writes its number to fd, then
   holds it until killed */
static pid_t lock_holder(const char *dir, int fd, char n)
{
    pid_t pid = fork();

    fail_if(pid < 0, "fork: %s", strerror(errno));  /* not kill(-1) later */
    if (pid == 0) {
        alarm(60);              /* not left behind by a failed test */
        if (vf_lockdir(dir) != NULL && write(fd, &n, 1) == 1)
            pause();
        _exit(1);
    }
    return pid;
}

/* whether a process wrote to fd within ms */
static int wrote_within(int fd, int ms)
{
    struct pollfd p = { fd, POLLIN, 0 };
    char c;

    return poll(&p, 1, ms) == 1 && read(fd, &c, 1) == 1;
}

/* dir with the files of the locks its killed holders left in it */
static void remove_lockdir(const char *dir)
{
    char path[PATH_MAX];
    struct dirent *ent;
    DIR *d;

    if ((d = opendir(dir)) != NULL) {
        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".vflock") == 0) {
                n_snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
                unlink(path);
            }
        }
        closedir(d);
    }
    rmdir(dir);
}

/* the third process, started while the second holds the lock of dir, gets
   it only when the second ends */
static void third_waits_for_second(const char *dir, pid_t second)
{
    int fds[2];
    pid_t third;

    fail_if(pipe(fds) != 0, "pipe: %s", strerror(errno));
    third = lock_holder(dir, fds[1], '3');
    fail_if(wrote_within(fds[0], 300), "the third process got the lock the second holds");

    kill(second, SIGKILL);
    waitpid(second, NULL, 0);
    fail_if(!wrote_within(fds[0], 15000), "the third process got no lock after the second ended");
    kill(third, SIGKILL);
    waitpid(third, NULL, 0);
}

/* the names libvfile calls these by: on a 32-bit system with
   _FILE_OFFSET_BITS=64 <fcntl.h> and <sys/stat.h> pick the 64-bit ones */
#ifdef __USE_FILE_OFFSET64
# define OPEN_SYMBOL "open64"
#else
# define OPEN_SYMBOL "open"
#endif
#if defined __USE_TIME64_REDIRECTS
# define FSTAT_SYMBOL "__fstat64_time64"
#elif defined __USE_FILE_OFFSET64
# define FSTAT_SYMBOL "fstat64"
#else
# define FSTAT_SYMBOL "fstat"
#endif

static struct {
    int        close_fd;        /* the second locks right after its close */
    const char *open_path;      /* the second waits for go after its open */
    const char *dir;
    int        report[2];       /* the second writes to [1] once locked */
    int        opened[2], go[2];
    pid_t      second;
    int        second_locked;
    int        fstat_errno;     /* fstat() fails with it */
    int        opendir_errno;   /* opendir() fails with it */
    int        unlink_errno;    /* unlink() of a file of a lock fails with it */
    int        thread_close_fd; /* a thread takes the lock before its close */
    int        thread_rc;       /* of pthread_create() */
    int        thread_locked;   /* before that close */
    FILE       *fclose_error;   /* its fclose() fails with ENOSPC */
#ifdef ENABLE_THREADS
    pthread_t  thread;
#endif
} hook = {
    .close_fd = -1, .report = { -1, -1 }, .opened = { -1, -1 }, .go = { -1, -1 },
    .thread_close_fd = -1,
};

#ifdef ENABLE_THREADS
static void *take_lock(void *dir)
{
    struct vflock *lock = vf_lockdir(dir);

    if (lock && write(hook.report[1], "t", 1) != 1)
        return NULL;
    return lock;
}
#endif

/* libvfile calls these */
__attribute__((visibility("default")))
int close(int fd)
{
    static int (*real_close)(int);
    int rc;

    if (real_close == NULL)
        real_close = (int (*)(int))dlsym(RTLD_NEXT, "close");
#ifdef ENABLE_THREADS
    if (hook.thread_close_fd != -1 && fd == hook.thread_close_fd) {
        hook.thread_close_fd = -1;
        hook.thread_rc = pthread_create(&hook.thread, NULL, take_lock, (void *)hook.dir);
        /* unless it waits for the release to end */
        hook.thread_locked = hook.thread_rc == 0 && wrote_within(hook.report[0], 300);
    }
#endif
    rc = real_close(fd);
    if (hook.close_fd != -1 && fd == hook.close_fd) {
        hook.close_fd = -1;
        hook.second = lock_holder(hook.dir, hook.report[1], '2');
        /* what the release does after the close, it does with the lock
           held by the second */
        hook.second_locked = wrote_within(hook.report[0], 10000);
    }
    return rc;
}

__attribute__((visibility("default")))
int open(const char *path, int flags, ...)
{
    static int (*real_open)(const char *, int, ...);
    mode_t mode = 0;
    va_list ap;
    int fd;
    char c;

    if (real_open == NULL)
        real_open = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, OPEN_SYMBOL);
    if (flags & O_CREAT) {
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    fd = real_open(path, flags, mode);
    if (hook.open_path && strcmp(path, hook.open_path) == 0) {
        hook.open_path = NULL;
        if (write(hook.opened[1], "o", 1) != 1 || read(hook.go[0], &c, 1) != 1)
            _exit(1);
    }
    return fd;
}

__attribute__((visibility("default")))
int unlink(const char *path)
{
    static int (*real_unlink)(const char *);
    const char *bn = strrchr(path, '/');

    if (real_unlink == NULL)
        real_unlink = (int (*)(const char *))dlsym(RTLD_NEXT, "unlink");
    if (hook.unlink_errno && bn && strcmp(bn, "/.vflock") == 0) {
        errno = hook.unlink_errno;
        return -1;
    }
    return real_unlink(path);
}

__attribute__((visibility("default")))
int fstat(int fd, struct stat *st)
{
    static int (*real_fstat)(int, struct stat *);

    if (real_fstat == NULL)
        real_fstat = (int (*)(int, struct stat *))dlsym(RTLD_NEXT, FSTAT_SYMBOL);
    if (hook.fstat_errno) {
        errno = hook.fstat_errno;
        return -1;
    }
    return real_fstat(fd, st);
}

__attribute__((visibility("default")))
DIR *opendir(const char *path)
{
    static DIR *(*real_opendir)(const char *);

    if (real_opendir == NULL)
        real_opendir = (DIR *(*)(const char *))dlsym(RTLD_NEXT, "opendir");
    if (hook.opendir_errno) {
        errno = hook.opendir_errno;
        return NULL;
    }
    return real_opendir(path);
}

__attribute__((visibility("default")))
int fclose(FILE *stream)
{
    static int (*real_fclose)(FILE *);

    if (real_fclose == NULL)
        real_fclose = (int (*)(FILE *))dlsym(RTLD_NEXT, "fclose");
    if (stream == hook.fclose_error) {
        hook.fclose_error = NULL;
        real_fclose(stream);
        errno = ENOSPC;
        return EOF;
    }
    return real_fclose(stream);
}

/* the number of files of locks in dir */
static int nlockfiles(const char *dir)
{
    struct dirent *ent;
    DIR *d;
    int n = 0;

    fail_if((d = opendir(dir)) == NULL, "opendir %s: %s", dir, strerror(errno));
    while ((ent = readdir(d)) != NULL)
        n += strcmp(ent->d_name, ".vflock") == 0;
    closedir(d);
    return n;
}

/* a cache directory that cannot be read after it was locked is left
   unlocked */
START_TEST (test_cachedir_clean_opendir_error) {
    char cache[PATH_MAX];
    int verbose = -1, rc;

    vfile_configure(VFILE_CONF_VERBOSE, &verbose);
    n_snprintf(cache, sizeof(cache), "%s", NTEST_TMPPATH("vfcache-opendir"));
    mkdir(cache, 0755);
    vfile_configure(VFILE_CONF_CACHEDIR, cache);

    hook.opendir_errno = EMFILE;
    rc = vfile_cachedir_clean();
    hook.opendir_errno = 0;
    expect_int(rc, 3);
    expect_int(nlockfiles(cache), 0);
    remove_lockdir(cache);
}
END_TEST

/* the second process takes the lock the first releases as soon as it can,
   and the third gets it only when the second ends */
START_TEST (test_lock_taken_at_release) {
    char dir[PATH_MAX];
    struct vflock *lock;

    n_snprintf(dir, sizeof(dir), "%s", NTEST_TMPPATH("vflock-release"));
    mkdir(dir, 0755);
    fail_if(pipe(hook.report) != 0, "pipe: %s", strerror(errno));

    fail_if((lock = vf_lockdir(dir)) == NULL, "%s: no lock", dir);
    hook.dir = dir;
    hook.close_fd = lock->fd;
    vf_lock_release(lock);
    fail_if(hook.second == 0, "the release closed no descriptor");
    fail_if(!hook.second_locked, "the second process got no lock");

    third_waits_for_second(dir, hook.second);
    remove_lockdir(dir);
}
END_TEST

/* the same with the second having opened the file of the lock before the
   first released it */
START_TEST (test_lock_opened_before_release) {
    char dir[PATH_MAX];
    struct vflock *lock;
    pid_t second;

    n_snprintf(dir, sizeof(dir), "%s", NTEST_TMPPATH("vflock-opened"));
    mkdir(dir, 0755);
    fail_if(pipe(hook.report) != 0 || pipe(hook.opened) != 0 || pipe(hook.go) != 0,
            "pipe: %s", strerror(errno));

    fail_if((lock = vf_lockdir(dir)) == NULL, "%s: no lock", dir);
    hook.open_path = lock->path;    /* in the second process only */
    second = lock_holder(dir, hook.report[1], '2');
    hook.open_path = NULL;
    fail_if(!wrote_within(hook.opened[0], 10000), "the second process opened no lock");

    vf_lock_release(lock);
    fail_if(write(hook.go[1], "g", 1) != 1, "write: %s", strerror(errno));
    fail_if(!wrote_within(hook.report[0], 10000), "the second process got no lock");

    third_waits_for_second(dir, second);
    remove_lockdir(dir);
}
END_TEST

/* a lock taken again by its holder, as a fetch redirected to another
   protocol does, stays until the holder lets the first one go */
START_TEST (test_lock_taken_again) {
    char dir[PATH_MAX], lockpath[PATH_MAX];
    struct vflock *outer, *inner;
    int fds[2];
    pid_t second;

    n_snprintf(dir, sizeof(dir), "%s", NTEST_TMPPATH("vflock-again"));
    mkdir(dir, 0755);
    fail_if(pipe(fds) != 0, "pipe: %s", strerror(errno));

    fail_if((outer = vf_lockdir(dir)) == NULL, "%s: no lock", dir);
    fail_if((inner = vf_lockdir(dir)) == NULL, "%s: no lock taken again", dir);
    n_snprintf(lockpath, sizeof(lockpath), "%s", outer->path);
    vf_lock_release(inner);
    fail_if(access(lockpath, F_OK) != 0, "%s: removed with the first lock held", lockpath);

    second = lock_holder(dir, fds[1], '2');
    fail_if(wrote_within(fds[0], 300), "another process got the lock held");
    vf_lock_release(outer);
    fail_if(!wrote_within(fds[0], 15000), "another process got no lock after its release");
    kill(second, SIGKILL);
    waitpid(second, NULL, 0);
    remove_lockdir(dir);
}
END_TEST

/* a child releasing the locks it inherits closes their descriptor once,
   and the lock of its parent stays */
START_TEST (test_lock_released_in_child) {
    char dir[PATH_MAX], lockpath[PATH_MAX];
    struct vflock *outer, *inner;
    int fds[2], st;
    pid_t child, second;

    n_snprintf(dir, sizeof(dir), "%s", NTEST_TMPPATH("vflock-child"));
    mkdir(dir, 0755);
    fail_if(pipe(fds) != 0, "pipe: %s", strerror(errno));
    fail_if((outer = vf_lockdir(dir)) == NULL, "%s: no lock", dir);
    fail_if((inner = vf_lockdir(dir)) == NULL, "%s: no lock taken again", dir);
    n_snprintf(lockpath, sizeof(lockpath), "%s", outer->path);

    if ((child = fork()) == 0) {
        int fd;

        vf_lock_release(inner);
        fd = open("/dev/null", O_RDONLY);   /* the number of a closed one */
        vf_lock_release(outer);
        _exit(fcntl(fd, F_GETFD) == -1);
    }
    fail_if(child < 0, "fork: %s", strerror(errno));
    fail_if(waitpid(child, &st, 0) != child, "waitpid: %s", strerror(errno));
    fail_if(!WIFEXITED(st) || WEXITSTATUS(st) != 0,
            "the child closed a file of its own releasing the locks it inherited");
    fail_if(access(lockpath, F_OK) != 0, "%s: removed by the child", lockpath);

    second = lock_holder(dir, fds[1], '2');
    fail_if(wrote_within(fds[0], 300), "another process got the lock held");
    vf_lock_release(inner);
    vf_lock_release(outer);
    fail_if(!wrote_within(fds[0], 15000), "another process got no lock after its release");
    kill(second, SIGKILL);
    waitpid(second, NULL, 0);
    remove_lockdir(dir);
}
END_TEST

/* a child that took its own lock of the file of an inherited one, which
   its parent left when it ended, keeps it when it releases the inherited */
START_TEST (test_lock_inherited_of_own_file) {
    char dir[PATH_MAX];
    int fds[2], st;
    pid_t parent, second, child = 0;

    n_snprintf(dir, sizeof(dir), "%s", NTEST_TMPPATH("vflock-own"));
    mkdir(dir, 0755);
    fail_if(pipe(hook.report) != 0 || pipe(fds) != 0, "pipe: %s", strerror(errno));

    if ((parent = fork()) == 0) {
        struct vflock *inherited;

        if ((inherited = vf_lockdir(dir)) == NULL || (child = fork()) < 0)
            _exit(1);
        if (child == 0) {
            struct vflock *own;
            int inherited_fd = inherited->fd, own_fd, gst;
            pid_t grandchild;
            char c = 'c';

            alarm(60);
            /* taken once the parent ends, of the file it left */
            if ((own = vf_lockdir(dir)) == NULL)
                _exit(1);
            own_fd = own->fd;
            vf_lock_release(inherited);

            /* a grandchild releasing the one it inherits closes both */
            if ((grandchild = fork()) == 0) {
                vf_lock_release(own);
                _exit(fcntl(inherited_fd, F_GETFD) != -1 || fcntl(own_fd, F_GETFD) != -1);
            }
            if (grandchild < 0 || waitpid(grandchild, &gst, 0) != grandchild ||
                !WIFEXITED(gst) || WEXITSTATUS(gst) != 0)
                c = 'g';
            if (write(hook.report[1], &c, 1) == 1)
                pause();
            _exit(1);
        }
        if (write(fds[1], &child, sizeof(child)) != sizeof(child))
            _exit(1);
        _exit(0);               /* with the lock, its file left */
    }
    fail_if(parent < 0, "fork: %s", strerror(errno));
    fail_if(waitpid(parent, &st, 0) != parent, "waitpid: %s", strerror(errno));
    fail_if(!WIFEXITED(st) || WEXITSTATUS(st) != 0, "the parent failed");
    fail_if(read(fds[0], &child, sizeof(child)) != sizeof(child), "no child");
    {
        struct pollfd p = { hook.report[0], POLLIN, 0 };
        char c = 0;

        fail_if(poll(&p, 1, 15000) != 1 || read(hook.report[0], &c, 1) != 1,
                "the child got no lock of its own");
        fail_if(c == 'g', "a grandchild left a descriptor of a released lock open");
    }

    second = lock_holder(dir, fds[1], '2');
    fail_if(wrote_within(fds[0], 300), "another process got the lock the child holds");
    kill(child, SIGKILL);
    fail_if(!wrote_within(fds[0], 15000), "another process got no lock after the child ended");
    kill(second, SIGKILL);
    waitpid(second, NULL, 0);
    remove_lockdir(dir);
}
END_TEST

#ifdef ENABLE_THREADS
/* with the file of the lock left by a failed removal, a thread that takes
   the lock as it is released still holds it after the release */
START_TEST (test_lock_unlink_error) {
    char dir[PATH_MAX];
    struct vflock *lock;
    void *thread_lock;
    int fds[2], verbose = -1;
    pid_t second;

    vfile_configure(VFILE_CONF_VERBOSE, &verbose);
    n_snprintf(dir, sizeof(dir), "%s", NTEST_TMPPATH("vflock-unlink"));
    mkdir(dir, 0755);
    fail_if(pipe(hook.report) != 0 || pipe(fds) != 0, "pipe: %s", strerror(errno));

    fail_if((lock = vf_lockdir(dir)) == NULL, "%s: no lock", dir);
    hook.dir = dir;
    hook.unlink_errno = EACCES;
    hook.thread_close_fd = lock->fd;
    vf_lock_release(lock);
    hook.unlink_errno = 0;
    fail_if(hook.thread_rc != 0, "pthread_create: %s", strerror(hook.thread_rc));
    if (!hook.thread_locked)
        fail_if(!wrote_within(hook.report[0], 10000), "the thread got no lock");
    pthread_join(hook.thread, &thread_lock);
    fail_if(thread_lock == NULL, "the thread got no lock");

    second = lock_holder(dir, fds[1], '2');
    fail_if(wrote_within(fds[0], 300), "another process got the lock the thread holds");
    vf_lock_release(thread_lock);
    fail_if(!wrote_within(fds[0], 15000), "another process got no lock after its release");
    kill(second, SIGKILL);
    waitpid(second, NULL, 0);
    remove_lockdir(dir);
}
END_TEST
/* a thread of a child that takes the lock its parent left as the child
   releases the inherited one still holds it after the release */
START_TEST (test_lock_inherited_released_as_thread_locks) {
    char dir[PATH_MAX];
    int fds[2], go[2], done[2], st;
    pid_t parent, second, child = 0;
    char c;

    n_snprintf(dir, sizeof(dir), "%s", NTEST_TMPPATH("vflock-thread"));
    mkdir(dir, 0755);
    fail_if(pipe(hook.report) != 0 || pipe(fds) != 0 || pipe(go) != 0 || pipe(done) != 0,
            "pipe: %s", strerror(errno));

    if ((parent = fork()) == 0) {
        struct vflock *inherited;

        if ((inherited = vf_lockdir(dir)) == NULL || (child = fork()) < 0)
            _exit(1);
        if (child == 0) {
            void *thread_lock = NULL;

            alarm(60);
            if (read(go[0], &c, 1) != 1)        /* the parent has ended */
                _exit(1);
            hook.dir = dir;
            hook.thread_close_fd = inherited->fd;
            vf_lock_release(inherited);
            if (hook.thread_rc != 0 ||
                (!hook.thread_locked && !wrote_within(hook.report[0], 10000)) ||
                pthread_join(hook.thread, &thread_lock) != 0 || thread_lock == NULL)
                _exit(1);
            if (write(done[1], "t", 1) == 1)
                pause();
            _exit(1);
        }
        if (write(fds[1], &child, sizeof(child)) != sizeof(child))
            _exit(1);
        _exit(0);               /* with the lock, its file left */
    }
    fail_if(parent < 0, "fork: %s", strerror(errno));
    fail_if(waitpid(parent, &st, 0) != parent, "waitpid: %s", strerror(errno));
    fail_if(!WIFEXITED(st) || WEXITSTATUS(st) != 0, "the parent failed");
    fail_if(read(fds[0], &child, sizeof(child)) != sizeof(child), "no child");
    fail_if(write(go[1], "g", 1) != 1, "write: %s", strerror(errno));
    fail_if(!wrote_within(done[0], 15000), "the thread of the child got no lock");

    second = lock_holder(dir, fds[1], '2');
    fail_if(wrote_within(fds[0], 300), "another process got the lock the thread holds");
    kill(child, SIGKILL);
    fail_if(!wrote_within(fds[0], 15000), "another process got no lock after the child ended");
    kill(second, SIGKILL);
    waitpid(second, NULL, 0);
    remove_lockdir(dir);
}
END_TEST

static int forking[2] = { -1, -1 };

/* registered after the handler of vflock.c, so run before it */
static void fork_prepare(void)
{
    if (forking[1] != -1 && write(forking[1], "f", 1) == 1)
        forking[1] = -1;
}

/* a fork() that does not wait for the thread has made its child by then */
static void *go_after_fork(void *arg)
{
    char c;

    if (read(forking[0], &c, 1) != 1)
        return NULL;
    usleep(200000);
    return write(hook.go[1], "g", 1) == 1 ? arg : NULL;
}

/* a child forked while a thread is taking a lock takes one of its own */
START_TEST (test_lock_fork_while_thread_locks) {
    char dir[PATH_MAX], other[PATH_MAX], lockpath[PATH_MAX];
    struct vflock *lock;
    pthread_t timer;
    void *thread_lock;
    pid_t child;
    int st;

    n_snprintf(dir, sizeof(dir), "%s", NTEST_TMPPATH("vflock-fork"));
    n_snprintf(other, sizeof(other), "%s", NTEST_TMPPATH("vflock-fork-child"));
    mkdir(dir, 0755);
    mkdir(other, 0755);
    fail_if(pipe(hook.report) != 0 || pipe(hook.opened) != 0 || pipe(hook.go) != 0 ||
            pipe(forking) != 0, "pipe: %s", strerror(errno));

    fail_if((lock = vf_lockdir(dir)) == NULL, "%s: no lock", dir);
    n_snprintf(lockpath, sizeof(lockpath), "%s", lock->path);
    vf_lock_release(lock);

    /* the thread stops in its open() of the file of the lock, with the
       list of the locks held, until the timer lets it go on */
    hook.open_path = lockpath;
    hook.thread_rc = pthread_create(&hook.thread, NULL, take_lock, dir);
    fail_if(hook.thread_rc != 0, "pthread_create: %s", strerror(hook.thread_rc));
    fail_if(!wrote_within(hook.opened[0], 10000), "the thread opened no lock");
    fail_if(pthread_atfork(fork_prepare, NULL, NULL) != 0, "pthread_atfork");
    fail_if(pthread_create(&timer, NULL, go_after_fork, NULL) != 0, "pthread_create");

    if ((child = fork()) == 0) {
        signal(SIGALRM, SIG_DFL);   /* check's handler would kill the test */
        alarm(2);
        _exit(vf_lockdir(other) == NULL);
    }
    fail_if(child < 0, "fork: %s", strerror(errno));
    fail_if(waitpid(child, &st, 0) != child, "waitpid: %s", strerror(errno));
    fail_if(WIFSIGNALED(st) && WTERMSIG(st) == SIGALRM,
            "the child hung taking a lock");
    fail_if(!WIFEXITED(st) || WEXITSTATUS(st) != 0, "the child got no lock");

    pthread_join(timer, NULL);
    pthread_join(hook.thread, &thread_lock);
    fail_if(thread_lock == NULL, "the thread got no lock");
    vf_lock_release(thread_lock);
    remove_lockdir(other);
    remove_lockdir(dir);
}
END_TEST
# define THREAD_TESTS , test_lock_unlink_error, \
    test_lock_inherited_released_as_thread_locks, \
    test_lock_fork_while_thread_locks
#else
# define THREAD_TESTS
#endif

/* an error of checking the file of the lock ends the attempt to take it */
START_TEST (test_lock_fstat_error) {
    char dir[PATH_MAX];
    struct vflock *lock;
    int verbose = -1;

    vfile_configure(VFILE_CONF_VERBOSE, &verbose);
    n_snprintf(dir, sizeof(dir), "%s", NTEST_TMPPATH("vflock-fstat"));
    mkdir(dir, 0755);
    hook.fstat_errno = EOVERFLOW;
    lock = vf_lockdir(dir);
    hook.fstat_errno = 0;
    fail_if(lock != NULL, "%s: a lock with fstat() failing", dir);
    remove_lockdir(dir);
}
END_TEST

/* a directory deeper than NAME_MAX is locked: the name of the lock's file
   held its whole path, ~250 bytes of it made it too long to create */
START_TEST (test_lock_long_path) {
    char dir[PATH_MAX], name[121], sub[PATH_MAX];
    struct vflock *lock;

    memset(name, 'd', sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    n_snprintf(dir, sizeof(dir), "%s", NTEST_TMPPATH("vflock-long"));
    mkdir(dir, 0755);
    n_snprintf(sub, sizeof(sub), "%s/%s", dir, name);
    mkdir(sub, 0755);
    n_snprintf(sub, sizeof(sub), "%s/%s/%s", dir, name, name);
    mkdir(sub, 0755);
    fail_if(strlen(sub) < 260, "%s: a path of %zu bytes", sub, strlen(sub));

    fail_if((lock = vf_lockdir(sub)) == NULL, "%s: no lock", sub);
    expect_int(nlockfiles(sub), 1);
    vf_lock_release(lock);
    expect_int(nlockfiles(sub), 0);

    rmdir(sub);
    n_snprintf(sub, sizeof(sub), "%s/%s", dir, name);
    rmdir(sub);
    rmdir(dir);
}
END_TEST

/* a path with no room for the name of the lock's file is refused, not an
   abort() */
START_TEST (test_lock_path_too_long) {
    char dir[PATH_MAX];
    int verbose = -1;

    vfile_configure(VFILE_CONF_VERBOSE, &verbose);
    memset(dir, 'd', sizeof(dir) - 5);
    dir[0] = '/';
    dir[sizeof(dir) - 5] = '\0';
    fail_if(vf_lockdir(dir) != NULL, "a lock of a %zu-byte path", strlen(dir));
}
END_TEST

/* what vfile logged other than errors */
static tn_buf *logged_info;

static void log_info_cb(int pri, const char *fmt, va_list ap)
{
    if ((pri & VFILE_LOG_ERR) == 0)
        n_buf_vprintf(logged_info, fmt, ap);
}

static const char *logged(const tn_buf *buf)
{
    return n_buf_size(buf) ? n_buf_ptr(buf) : "";
}

/* a file written uncompressed to be compressed at its close is not
   compressed when the close fails, as it may be cut short; the compressor
   is not installed in the tree, so its start shows whether it ran */
START_TEST (test_close_error_not_compressed) {
    char path[PATH_MAX], plain[PATH_MAX], compressing[PATH_MAX];
    struct vfile *vf;
    int v = 1;

    vfile_configure(VFILE_CONF_VERBOSE, &v);
    vfile_configure(VFILE_CONF_LOGCB, log_info_cb);
    logged_info = n_buf_new(0);
    n_snprintf(path, sizeof(path), "%s", NTEST_TMPPATH("close-compr.gz"));
    n_snprintf(plain, sizeof(plain), "%s", NTEST_TMPPATH("close-compr"));
    n_snprintf(compressing, sizeof(compressing), "Compressing %s...", n_basenam(plain));

    fail_if((vf = vfile_open(path, VFT_STDIO, VFM_RW | VFM_UNCOMPR)) == NULL);
    fail_unless(vfile_close(vf), "close: %s", strerror(errno));
    fail_if(strstr(logged(logged_info), compressing) == NULL,
            "not compressed after a good close: %s", logged(logged_info));

    n_buf_clean(logged_info);
    fail_if((vf = vfile_open(path, VFT_STDIO, VFM_RW | VFM_UNCOMPR)) == NULL);
    hook.fclose_error = vf->vf_stream;
    fail_if(vfile_close(vf), "a failed close succeeded");
    expect_int(errno, ENOSPC);
    fail_if(strstr(logged(logged_info), compressing) != NULL,
            "compressed after a failed close: %s", logged(logged_info));

    vfile_configure(VFILE_CONF_LOGCB, NULL);
    n_buf_free(logged_info);
    unlink(plain);
    unlink(path);
}
END_TEST

NTEST_RUNNER("vfile", test_vfile_append, test_valid_path,
             test_url_as_path_percent, test_url_hidepasswd,
             test_url_hidepasswd_short_buffer, test_fetcha_ext_multi_dirbn,
             test_url_type_scheme, test_open_errno,
             test_lock_mkdir_made_meanwhile, test_cachedir_prefix,
             test_cachedir_clean_opendir_error,
             test_lock_taken_at_release, test_lock_opened_before_release,
             test_lock_fstat_error, test_lock_taken_again,
             test_lock_released_in_child, test_lock_inherited_of_own_file,
             test_lock_long_path, test_lock_path_too_long,
             test_close_error_not_compressed
             THREAD_TESTS);
