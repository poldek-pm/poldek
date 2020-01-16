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
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/nstr.h>

#include "compiler.h"
#include "i18n.h"
#include "vfile.h"
#include "vfile_intern.h"
#include "p_open.h"


/*
   %p[n] - package basename
   %d - cache dir
   %D - cache dir/package basename
   %P[n] - package full path

   "/usr/bin/wget -N --dot-style=binary -P %d %Pn"
   "/usr/bin/snarf %P %D"
   "/usr/bin/curl %P -o %D"
*/

#define FETCHFMT_ARG    0
#define FETCHFMT_DIR    1
#define FETCHFMT_DIRBN  2
#define FETCHFMT_BN     3
#define FETCHFMT_FN     4

#define FETCHFMT_MULTI  (1 << 0)

struct fetcharg {
    int8_t type;
    int8_t flags;
    char arg[0];
};

struct ffetcher {
    char      *name;
    tn_array  *protocols;
    uint16_t  urltypes;
    int16_t   is_multi;   /* is able to download more than one file at once? */
    tn_array  *args;
    char      path[0];
};

extern tn_hash *vfile_default_clients_ht;

static tn_hash *ffetchers = NULL;
static tn_hash *ffetchers_proto_idx = NULL;

static
struct ffetcher *find_fetcher(const char *proto, int multi);


int vfile_is_configured_ext_handler(const char *url)
{
    char proto[64];

    vf_url_proto(proto, sizeof(proto), url);
    return find_fetcher(proto, 0) != NULL;
}

static
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

static unsigned protocols_to_urltypes(const tn_array *protocols)
{
    char proto[64];
    int i;
    unsigned urltypes = 0, type;

    for (i=0; i<n_array_size(protocols); i++) {
        char *p = n_array_nth(protocols, i);
        n_snprintf(proto, sizeof(proto), "%s://", p);
        if ((type = vf_url_type(proto)) != VFURL_UNKNOWN)
            urltypes |= type;

    }

    return urltypes;
}


static
struct ffetcher *ffetcher_new(const char *name, tn_array *protocols,
                              const char *cmd)
{
    char              *token, *path, *bn, *fmt;
    struct fetcharg   *arg;
    tn_array          *args;
    struct ffetcher   *ftch;
    int               has_p_arg = 0, has_d_arg = 0, is_multi = 0;
    int               len;
    const char        *invalid_fmt_msg = "%s: invalid format\n";

    len = strlen(cmd) + 1;
    fmt = alloca(len);
    memcpy(fmt, cmd, len);

    if ((path = next_token(&fmt, ' ', NULL)) == NULL)
        return NULL;

    if (*path != '/') {
        char *p = alloca(PATH_MAX);
        if (vf_find_external_command(p, PATH_MAX, path, NULL)) {
            path = p;
        } else {
            if (*vfile_verbose > 1)
                vf_logerr("%s: command not found\n", path);
            return NULL;
        }
    }

    args = n_array_new(8, free, NULL);
    bn = n_basenam(path);

    arg = n_malloc(sizeof(*arg) + strlen(bn) + 1);
    arg->type = FETCHFMT_ARG;
    strcpy(arg->arg, bn);
    n_array_push(args, arg);

    while ((token = next_token(&fmt, ' ', NULL))) {
        if (*token != '%') {
            arg = n_malloc(sizeof(*arg) + strlen(token) + 1);
            arg->type = FETCHFMT_ARG;
            arg->flags = 0;
            strcpy(arg->arg, token);
            n_array_push(args, arg);

        } else if (strlen(token) > 3) {
            vf_logerr(invalid_fmt_msg, cmd);
            goto l_err_end;

        } else {
            char c;
            arg = n_malloc(sizeof(*arg));

            c = *(token + 2);
            switch(*(token + 1)) {
                case 'p':
                    arg->type = FETCHFMT_BN;
                    arg->flags = 0;
                    has_p_arg++;
                    if (c == 'n') {
                        arg->flags = FETCHFMT_MULTI;
                        is_multi = 1;
                    } else if (c != '\0') {
                        vf_logerr(invalid_fmt_msg, cmd);
                        goto l_err_end;
                    }
                    break;

                case 'P':
                    arg->type = FETCHFMT_FN;
                    arg->flags = 0;
                    has_p_arg++;
                    if (c == 'n') {
                        arg->flags = FETCHFMT_MULTI;
                        is_multi = 1;
                    } else if (c != '\0') {
                        vf_logerr(invalid_fmt_msg, cmd);
                        goto l_err_end;
                    }

                    break;

                case 'd':
                    arg->type = FETCHFMT_DIR;
                    has_d_arg++;
                    if (c != '\0') {
                        vf_logerr(invalid_fmt_msg, cmd);
                        goto l_err_end;
                    }
                    break;

                case 'D':
                    arg->type = FETCHFMT_DIRBN;
                    arg->flags = 0;
                    has_d_arg++;


                    if (c == 'n') {
                        arg->flags = FETCHFMT_MULTI;
                    } else if (c != '\0') {
                        vf_logerr(invalid_fmt_msg, cmd);
                        goto l_err_end;
                    }

                    break;

                default:
                    vf_logerr(invalid_fmt_msg, cmd);
                    goto l_err_end;
            }
            n_array_push(args, arg);
        }
    }


    if (n_array_size(args) > 2 && has_d_arg && has_p_arg) {
        int path_len, name_len;

        path_len = strlen(path) + 1;
        name_len = strlen(name) + 1;

        ftch = n_malloc(sizeof(*ftch) + path_len + name_len);
        memset(ftch, 0, sizeof(*ftch));

        ftch->protocols = n_array_dup(protocols, (tn_fn_dup)n_strdup);
        ftch->args     = args;
        ftch->is_multi = is_multi;
        ftch->urltypes = protocols_to_urltypes(protocols);
        memcpy(ftch->path, path, path_len);
        memcpy(&ftch->path[path_len], name, name_len);
        ftch->name = &ftch->path[path_len];

    } else
        goto l_err_end;

    return ftch;

 l_err_end:
    if (args)
        n_array_free(args);
    return NULL;
}

static void ffetcher_free(struct ffetcher *ftch)
{
    n_array_free(ftch->protocols);
    n_array_free(ftch->args);
    free(ftch);
}


static void process_output(struct p_open_st *st, const char *prefix)
{
    int endl = 1, cnt = 0;


    if (prefix == NULL)
        prefix = st->cmd;

    while (1) {
        struct timeval to = { 1, 0 };
        fd_set fdset;
        int rc;

        FD_ZERO(&fdset);
        FD_SET(st->fd, &fdset);
        if ((rc = select(st->fd + 1, &fdset, NULL, NULL, &to)) < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;

            break;

        } else if (rc > 0) {
            char  buf[2048];
            int   n, i;

            if ((n = read(st->fd, buf, sizeof(buf) - 1)) <= 0)
                break;

            if (*vfile_verbose == 0)
                continue;

            // fix for aria - print in one line speed
            if (buf[0] == '\n' && (buf[1] == '[' || endl == 1))
                buf[0] = '\r';
            if ( n >= 2 && buf[n-1] == '\n' && buf[n-2] == ']')
                buf[n-1] = '\r';

            buf[n] = '\0';
            for (i=0; i < n; i++) {
                int c = buf[i];

                if (endl) {
                    vf_loginfo("%s: ", prefix);
                    endl = 0;
                }

                if (c == '\n')
                    vf_loginfo("_\n"); /* is_endlined in log.c */
                else
                    vf_loginfo("_%c", c);

                if (c == '\n' || c == '\r')
                    endl = 1;

                cnt++;
            }
        }
    }
}

static
int ffetch_file(struct ffetcher *fftch, const char *destdir,
                const char *url /* or */, tn_array *urls)
{
    char              *bn = NULL, **argv;
    struct vflock     *vflock = NULL;
    struct p_open_st  pst;
    int               i, n, ec, verbose;
    unsigned          p_open_flags = 0;


    if ((vflock = vf_lock_mkdir(destdir)) == NULL)
        return 0;

    if (url)
        n_assert(urls == NULL);

    if (urls)
        n_assert(url == NULL && fftch->is_multi);

    if (url)
        bn = n_basenam(url);

    n = n_array_size(fftch->args) + 1;

    if (urls)
        n += n_array_size(urls);
    else
        n += 1;

    argv = alloca(sizeof(*argv) * n);

    n = 0;
    for (i=0; i<n_array_size(fftch->args); i++) {
        struct fetcharg *arg = n_array_nth(fftch->args, i);
        switch (arg->type) {
            case FETCHFMT_ARG:
                argv[n++] = arg->arg;
                break;

            case FETCHFMT_DIRBN: {
                int len = strlen(destdir) + strlen(bn) + 2;
                argv[n] = alloca(len);
                snprintf(argv[n], len, "%s/%s", destdir, bn ? bn : "ERROR");
                n++;
                break;
            }

            case FETCHFMT_DIR:
                argv[n++] = (char*)destdir;
                break;

            case FETCHFMT_BN:
                if (url) {
                    argv[n++] = bn;
                } else {
                    for (int ii = 0; ii < n_array_size(urls); ii++)
                        argv[n++] = n_basenam(n_array_nth(urls, ii));
                }
                break;

            case FETCHFMT_FN:
                if (url) {
                    argv[n++] = (char*)url;
                } else {
                    for (int ii = 0; ii < n_array_size(urls); ii++)
                        argv[n++] = n_array_nth(urls, ii);
                }
                break;

            default:
                vf_logerr("vf_fetch*: internal error\n");
                n_assert(0);
                return 0;
        }
    }
    argv[n++] = NULL;

    if (*vfile_verbose > 1) {
        int len = 0;
        char *s, *p;


        for (int ii=0; ii < n-1; ii++)
            len += strlen(argv[ii]) + 1;
        len++;

        p = s = alloca(len);
        *s = '\0';

        for (i=0; i < n-1; i++) {
            p = n_strncpy(p, CL_URL(argv[i]), len);
            len -= strlen(argv[i]);
            p = n_strncpy(p, " ", len);
            len--;
        }
        vf_loginfo(_("Running %s\n"), s);
    }

    verbose = *vfile_verbose;
    if (fftch->urltypes & VFURL_CDROM) {
        p_open_flags |= P_OPEN_KEEPSTDIN;
        if (*vfile_verbose < 1)
            *vfile_verbose = 1;
    }

    p_st_init(&pst);

    if (p_open(&pst, p_open_flags, fftch->path, argv) == NULL) {
        vf_logerr("p_open: %s\n", pst.errmsg);
        ec = -1;

    } else {
        process_output(&pst,
                       ((struct fetcharg*) n_array_nth(fftch->args, 0))->arg);

        if ((ec = p_close(&pst)) != 0)
            vf_logerr("%s\n", pst.errmsg ? pst.errmsg :
                      _("program exited with non-zero value"));

        p_st_destroy(&pst);
    }

    *vfile_verbose = verbose;
    if (vflock)
        vf_lock_release(vflock);
    return ec == 0;
}


int vfile_register_ext_handler(const char *name, tn_array *protocols,
                               const char *cmd)
{
    struct ffetcher *ftch;
    int i;

    if (ffetchers && n_hash_exists(ffetchers, name)) {
        vf_log(VFILE_LOG_WARN, "%s: fetcher already exists, not overwritten\n", name);
        return 0;
    }

    if ((ftch = ffetcher_new(name, protocols, cmd)) == NULL) {
        if (*vfile_verbose > 1)
            vf_logerr("External downloader '%s': registration failed\n", cmd);

    } else {
        if (ffetchers == NULL) {
            ffetchers = n_hash_new(21, (tn_fn_free)ffetcher_free);
            ffetchers_proto_idx = n_hash_new(21, (tn_fn_free)n_array_free);
        }

        n_hash_insert(ffetchers, name, ftch);

        for (i=0; i < n_array_size(protocols); i++) {
            const char *proto = n_array_nth(protocols, i);
            tn_array *arr = NULL;

            if ((arr = n_hash_get(ffetchers_proto_idx, proto)) == NULL) {
                arr = n_array_new(2, NULL, NULL);
                n_hash_insert(ffetchers_proto_idx, proto, arr);
            }
            n_array_push(arr, ftch);
        }

        return 1;
    }
    return 0;
}


static
struct ffetcher *find_fetcher(const char *proto, int multi)
{
    struct ffetcher  *ftch = NULL;
    tn_array         *arr;
    int              i;
    const char       *clname;

    n_assert(vfile_conf.default_clients_ht);



    if ((clname = n_hash_get(vfile_conf.default_clients_ht, proto))) {

        if (ffetchers)
            ftch = n_hash_get(ffetchers, clname);

        if (ftch == NULL) {
            vf_logerr("vfile: %s: no such external fetcher found\n", clname);
            return NULL;
        }

        if (multi && !ftch->is_multi)
            return NULL;
        else
            return ftch;
    }

    if (ffetchers == NULL)
        return NULL;

    if ((arr = n_hash_get(ffetchers_proto_idx, proto)) == NULL)
        return NULL;

    for (i=0; i < n_array_size(arr); i++) {
        ftch = n_array_nth(arr, i);

        if (!multi || ftch->is_multi)
            return ftch;
    }

    return NULL;
}


int vf_fetch_ext(const char *url, const char *destdir)
{
    struct ffetcher *ftch;
    char proto[64];


    vf_url_proto(proto, sizeof(proto), url);

    if ((ftch = find_fetcher(proto, 0)) == NULL) {
        vf_logerr("vfile: %s://...: no external fetcher for this type "
                   "of url found\n", proto);
        return 0;
    }

    return ffetch_file(ftch, destdir, url, NULL);
}


int vf_fetcha_ext(tn_array *urls, const char *destdir)
{
    struct ffetcher *ftch;
    char proto[64];
    int rc = 1;

    vf_url_proto(proto, sizeof(proto), n_array_nth(urls, 0));

    if ((ftch = find_fetcher(proto, 1))) {
        rc = ffetch_file(ftch, destdir, NULL, urls);

    } else if ((ftch = find_fetcher(proto, 0))) {
        int i;
        int nerrs = 0;

        for (i=0; i < n_array_size(urls); i++)
            if (!ffetch_file(ftch, destdir, n_array_nth(urls, i), NULL))
                nerrs++;
        rc = nerrs == 0;

    } else {
        vf_logerr("vfile: %s://...: no external fetcher "
                   "for this type of url found\n", proto);
        rc = 0;
    }

    return rc;
}


static
int url_to_path(char *buf, int size, const char *url, int isdir)
{
    char *sl, *p, *bufp, url_buf[PATH_MAX];
    int n;

    *buf = '\0';
    n = 0;

    url = vf_url_hidepasswd(url_buf, sizeof(url_buf), url);

    if ((p = strstr(url, "://")) == NULL) {
        n = 0;
        p = (char*)url;

        if (*p == '/')          /* skip leading '/' ('_' in local path) */
            p++;

    } else {
        int nn = p - url;

        if (size <= nn)
            return 0;

        strncpy(buf, url, nn)[nn] = '\0';
        n = strlen(buf);
        n += n_snprintf(&buf[n], size - n, "_");
        p += 3;
    }

    bufp = &buf[n];
    n += n_snprintf(&buf[n], size - n, p);

    if (isdir)
        sl = strchr(buf, '\0');
    else
        sl = strrchr(buf, '/');

    p = bufp;

    if (*p == '/')
        *p++ = '_';

    while (*p && p != sl) {
        if (!isalnum(*p) && strchr("-+", *p) == NULL)
            *p = '.';
        p++;
    }

    //DBGF("%s[%d] => %s(%s)\n", url, isdir, buf, sl);
    return n;
}


int vf_url_as_dirpath(char *buf, size_t size, const char *url)
{
    return url_to_path(buf, size, url, 1);
}


int vf_url_as_path(char *buf, size_t size, const char *url)
{
    return url_to_path(buf, size, url, 0);
}

char *vf_url_proto(char *proto, int size, const char *url)
{
    char *p;

    n_assert(size > 2);
    *proto = '\0';

    if (*url == '/')
        n_snprintf(proto, size, "file");

    else if ((p = strstr(url, "://"))) {
        int len = p - url;

        if (len > size - 1)
            len = size - 1;

        memcpy(proto, url, len);
        proto[len] = '\0';
    }

    return *proto ? proto : NULL;
}


int vf_url_type(const char *url)
{
    char *p;

    if (*url == '/')
        return VFURL_PATH;

    if (strncmp(url, "ftp://", 6) == 0)
        return VFURL_FTP;

    if (strncmp(url, "http://", 7) == 0)
        return VFURL_HTTP;

    if (strncmp(url, "https://", 7) == 0)
        return VFURL_HTTPS;

    if (strncmp(url, "rsync://", 8) == 0)
        return VFURL_RSYNC;

    if (strncmp(url, "cdrom://", 8) == 0)
        return VFURL_CDROM;

    if ((p = strstr(url, "://"))) {
        int is_url = 1;

        while (url != p) {
            if (!isalpha(*url)) {
                is_url = 1;
                break;
            }
            url++;
        }

        if (is_url)
            return VFURL_UNKNOWN;
    }


    return VFURL_PATH;
}


const char *vf_url_hidepasswd(char *buf, int size, const char *url)
{
    char *p, *u, *q = NULL;
    int i;


    *buf = '\0';

    if (*url == '/' || (u = strstr(url, "://")) == NULL)
        return url;

    u += 3;

    if ((p = strrchr(u, '@')) != NULL && (q = strchr(u, ':')) != NULL && q < p) {
        i = q - url;
        strncpy(buf, url, size)[size - 1] = '\0';
        n_assert(buf[i] == ':');
        p = &buf[i + 1];
        while (*p && *p != '@')
            *p++ = 'x';

        url = buf;
    }

    return url;
}


const char *vf_url_hidepasswd_s(const char *url)
{
    static char buf[PATH_MAX];
    return vf_url_hidepasswd(buf, sizeof(buf), url);
}


const char *vf_url_slim(char *buf, int size, const char *url, int maxl)
{
    int len;
    char *p = NULL, *bn;
    int  bn_len;

    *buf = '\0';
    url = vf_url_hidepasswd(buf, size, url);

    if ((len = strlen(url)) < maxl + 8) /* +8 => +sizeof("/[...]/    */
        return url;

    if (len > size - 1)
        return url;

    //printf("URL %s\n", url);
    if (*buf == '\0') {         /* vf_url_hidepasswd doesn't fill it */
        strncpy(buf, url, size)[size - 1] = '\0';
        url = buf;
    }

    bn = n_basenam(buf);
    bn_len = strlen(bn);
    maxl -= bn_len;
    maxl -= sizeof("/[...]/");

    //printf("bn = %s, %d, %d\n", bn, strlen(bn), maxl);
    if ((p = strchr(buf, '/')) == NULL || p - buf >= maxl) {
        url = bn;

    } else {
        p = bn - 1;
        n_assert(*p == '/');
        *p = '\0';

        while (p && p > buf && p - buf > maxl) {
            //printf("p = %s, %s\n", p, buf);
            if ((p = strrchr(buf, '/'))) {
                //printf("p  = %s, %s\n", p, buf);
                *p = '\0';
            }
        }
        n_assert(p);
        //printf("buf = %s, bn = %s, %d, %d\n", buf, bn,
        //(p - buf + 2), strlen(buf));
        len = n_snprintf(p, size - (p - buf), "/[...]/");
        memmove(p + len, bn, bn_len + 1);
        //printf("buf[%d] = %s\n", len, buf);
    }

    return url;
}

const char *vf_url_slim_s(const char *url, int maxl)
{
    static char buf[PATH_MAX];
    return vf_url_slim(buf, sizeof(buf), url, maxl > 50 ? maxl : 60);
}

char *vf_url_unescape(const char *url)
{
    char *unescaped = NULL;
    int unesclen = 0;

    if (!url)
	return NULL;

    unescaped = n_malloc(strlen(url) + 1);

    while (*url) {
        char ch = *url;

        if (*url == '%' && isxdigit(url[1]) && isxdigit(url[2])) {
            char str[3];

            str[0] = url[1];
            str[1] = url[2];
            str[2] = '\0';

            ch = (char)strtol(str, NULL, 16);

            url += 2;
        }

        unescaped[unesclen++] = ch;
        url++;
    }

    unescaped[unesclen] = '\0';

    return unescaped;
}

int vf_find_external_command(char *cmdpath, int size, const char *cmd,
                             const char *PATH)
{
    const char **tl, **tl_save;
    char buf[PATH_MAX];
    int  found = 0;

    if (PATH == NULL) {
        if ((PATH = getenv("PATH")) == NULL)
            PATH = "/bin:/usr/bin:/usr/local/bin";
    }
    n_snprintf(buf, sizeof(buf), "%s", PATH);
    tl = tl_save = n_str_tokl(buf, ":");
    while (*tl) {
        snprintf(cmdpath, size, "%s/%s", *tl, cmd);
        if (access(cmdpath, R_OK | X_OK) == 0) {
            found = 1;
            break;
        }
        tl++;
    }
    n_str_tokl_free(tl_save);
    return found;
}
