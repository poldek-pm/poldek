#include <dlfcn.h>
#include <unistd.h>

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


NTEST_RUNNER("vfile", test_vfile_append, test_valid_path,
             test_url_as_path_percent, test_url_hidepasswd,
             test_url_hidepasswd_short_buffer, test_fetcha_ext_multi_dirbn,
             test_url_type_scheme, test_open_errno,
             test_lock_mkdir_made_meanwhile, test_cachedir_prefix);
