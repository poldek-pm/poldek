/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_STRSIGNAL
# define _GNU_SOURCE 1  /* for strsignal */
#endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <argp.h>

#include <openssl/evp.h>
#include <trurl/nassert.h>

#include <vfile/p_open.h>
#include <vfile/vfile.h>

#include "i18n.h"
#include "log.h"
#include "misc.h"


static
int valid_dir(const char *envname, const char *dir);


void translate_argp_options(struct argp_option *arr) 
{
    int i = 0;

    while (arr[i].doc) {
        arr[i].doc = _(arr[i].doc);
        if (arr[i].arg)
            arr[i].arg = _(arr[i].arg);
        i++;
    }
}

int bin2hex(char *hex, int hex_size, const unsigned char *bin, int bin_size)
{
    int i, n = 0, nn = 0;

    n_assert(hex_size > 2 * bin_size); /* with end '\0' */
    
    for (i=0; i < bin_size; i++) {
        n = snprintf(hex + nn, hex_size - nn, "%02x", bin[i]);
        nn += n;
        if (nn >= hex_size)
            break;
    }
    return nn;
}


int mhexdigest(FILE *stream, unsigned char *mdhex, int *mdhex_size, int digest_type)
{
    unsigned char md[128];
    int  md_size = sizeof(md);

    
    if (mdigest(stream, md, &md_size, digest_type)) {
        int i, n = 0, nn = 0;
        
        for (i=0; i < md_size; i++) {
            n = snprintf(mdhex + nn, *mdhex_size - nn, "%02x", md[i]);
            nn += n;
        }
        *mdhex_size = nn;
        
    } else {
        *mdhex = '\0';
        *mdhex_size = 0;
    }

    return *mdhex_size;
}


int mdigest(FILE *stream, unsigned char *md, int *md_size, int digest_type)
{
    unsigned char buf[8*1024];
    EVP_MD_CTX ctx;
    int n, nn = 0;


    n_assert(md_size && *md_size);

    if (digest_type == DIGEST_MD5) 
        EVP_DigestInit(&ctx, EVP_md5());
    else
        EVP_DigestInit(&ctx, EVP_sha1());

    while ((n = fread(buf, 1, sizeof(buf), stream)) > 0) {
        EVP_DigestUpdate(&ctx, buf, n);
        nn += n; 
    }
    
    EVP_DigestFinal(&ctx, buf, &n);

    if (n > *md_size) {
        *md = '\0';
        *md_size = 0;
    } else {
        memcpy(md, buf, n);
        *md_size = n;
    }
    
    return *md_size;
}

const char *setup_cachedir(void) 
{
    struct passwd *pw;
    char *dir;

    if ((dir = getenv("TMPDIR")) && valid_dir("$TMPDIR", dir))
        return strdup(dir);
    
    if ((pw = getpwuid(getuid())) == NULL)
        return strdup(tmpdir());

    if (!is_rwxdir(pw->pw_dir))
        return strdup(tmpdir());
    
    if (valid_dir("HOME", pw->pw_dir) && mk_dir(pw->pw_dir, ".poldek")) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", pw->pw_dir, ".poldek");
        return strdup(path);
    }

    return strdup(tmpdir());
}

const char *tmpdir(void) 
{
    struct stat st;
    static char *tmpdir = NULL;
    char *dir;

    if (tmpdir != NULL)
        return tmpdir;
    
    if ((dir = getenv("TMPDIR")) == NULL)
        tmpdir = "/tmp";
    
    else if (*dir != '/' || strlen(dir) < 4)
        dir = "/tmp";
    
    else {
        char *p;
            
        p = dir + 1;
        while (*p) {
            if (!isalnum(*p) && *p != '/' && *p != '-') {
                tmpdir = "/tmp";
                logn(LOGWARN,
                     _("$TMPDIR (%s) contains non alnum characters, using /tmp"), dir);
                break;
            }
            p++;
        }

        if (tmpdir == NULL) {
            if (stat(dir, &st) != 0) {
                logn(LOGERR, _("$TMPDIR (%s): %m, using /tmp"), dir);
                tmpdir = "/tmp";
                
            } else if (!S_ISDIR(st.st_mode)) {
                logn(LOGERR, _("$TMPDIR (%s): not a directory, using /tmp"), dir);
                tmpdir = "/tmp";
            }
        }
    }

    if (tmpdir == NULL)
        tmpdir = dir;

    return tmpdir;
}

static
int valid_dir(const char *envname, const char *dir) 
{
    struct stat st;
    const char *p;
    int rc = 1;

    
    p = dir + 1;
    while (*p) {
        if (!isalnum(*p) && *p != '/' && *p != '-') {
            logn(LOGWARN,
                 _("%s (%s) contains non alphanumeric characters"),
                 envname, dir);
            rc = 0;
            break;
        }
        p++;
    }
    
    if (rc) {
        rc = 0;
        if (stat(dir, &st) != 0)
            logn(LOGERR, _("%s (%s): %m, using /tmp"), envname, dir);
            
        else if (!S_ISDIR(st.st_mode))
            logn(LOGERR, _("%s (%s): not a directory"), envname, dir);
            
        else if (!(st.st_mode & S_IRWXU))
            logn(LOGERR, _("%s (%s): permission denied"), envname, dir);
            
        else 
            rc = 1;
    }

    return rc;
}


char *trimslash(char *path) 
{
    if (path) {
        char *p = strchr(path, '\0');
    
        if (p) {
            p--;
            if (*p == '/')
                *p = '\0';
        }
    }
    return path;
}


char *next_token(char **str, char delim, int *toklen) 
{
    char *p, *token;

    if (*str == NULL)
        return NULL;
    
    
    if ((p = strchr(*str, delim)) == NULL) {
        token = *str;
        if (toklen)
            *toklen = strlen(*str);
        *str = NULL;
        
    } else {
        *p = '\0';
        
        if (toklen)
            *toklen = p - *str;
        p++;
        while(isspace(*p))
            p++;
        token = *str;
        *str = p;
    }
    
    return token;
}

int is_rwxdir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 &&
        S_ISDIR(st.st_mode) && (st.st_mode & S_IRWXU);
}

int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}


void die(void) 
{
    printf("Something wrong, something not quite right, die\n");
    abort();
}

void process_cmd_output(struct p_open_st *st, const char *prefix) 
{
    int c, endl = 1, cnt = 0;
    
    if (prefix == NULL)
        prefix = st->cmd;

    setvbuf(st->stream, NULL, _IONBF, 0);
    while ((c = fgetc(st->stream)) != EOF) {
        
        if (endl) {
            msg(1, "_%s: ", prefix);
            endl = 0;
        }

        msg(1, "_%c", c);
        if (c == '\n' && cnt > 0)
            endl = 1;
        
        cnt++;
    }
}

int lockfile(const char *lockfile) 
{
    struct flock fl;
    int    fd;
    
    
    if ((fd = open(lockfile, O_RDWR | O_CREAT, 0644)) < 0) {
        logn(LOGERR, "open %s: %m", lockfile);
        return -1;
    }

    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    
    if (fcntl(fd, F_SETLK, &fl) == -1) {
        if (errno == EAGAIN || errno == EACCES)
            fd = 0;
        else
            logn(LOGERR, "fcntl %s: %m", lockfile);
        
    } else {
        char buf[64];
        
        ftruncate(fd, 0);
        snprintf(buf, sizeof(buf), "%d", getpid());
        write(fd, buf, strlen(buf));
    }
    
    return fd;
}

pid_t readlockfile(const char *lockfile) 
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



int mk_dir(const char *path, const char *dn) 
{
    struct stat st;
    char fpath[PATH_MAX];

    if (!vfile_valid_path(path))
        return 0;
    
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        logn(LOGERR, _("%s: no such directory"), path);
        return 0;
    }

    if (!(st.st_mode & S_IRWXU)) {
        logn(LOGERR, _("%s: mkdir: permission denied"), path);
        return 0;
    }
    
    snprintf(fpath, sizeof(fpath), "%s/%s", path, dn);
    if (!is_dir(fpath)) {
        if (mkdir(fpath, 0755) != 0) {
            logn(LOGERR, "%s: mkdir: %m", fpath);
            return 0;
        }
    }
    
    return 1;
}


