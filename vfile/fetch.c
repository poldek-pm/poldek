/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>

#include "vfile.h"
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
    uint16_t  urltypes;
    int16_t   is_multi;
    tn_array  *args;
    char      path[0];
};


#define MAX_FETCHERS  64
static struct ffetcher *ffetchers[MAX_FETCHERS];
static int nffetchers = 0;

int vfile_configured_handlers(void)
{
    return nffetchers;
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


static
struct ffetcher *ffetcher_new(unsigned urltypes, char *fmt)
{
    char *token, *path, *bn;
    struct fetcharg *arg;
    tn_array *args;
    struct ffetcher *ftch;
    int has_p_arg = 0, has_d_arg = 0, is_multi = 0;

    n_assert(fmt);
    if (fmt == NULL)
        return NULL;
    
    
    if ((path = next_token(&fmt, ' ', NULL)) == NULL) 
        return NULL;
    
    if (*path != '/') {
        vfile_err_fn("%s: cmd must be precedenced by '/'\n", path);
        return NULL;
    }

    if (access(path, X_OK) != 0) {
        vfile_err_fn("%s: %m\n", path);
        return NULL;
    }
    

    args = n_array_new(8, free, NULL);
    bn = n_basenam(path);
    arg = malloc(sizeof(*arg) + strlen(bn) + 1);
    arg->type = FETCHFMT_ARG;
    strcpy(arg->arg, bn);
    n_array_push(args, arg);
    
    while ((token = next_token(&fmt, ' ', NULL))) {
        if (*token != '%') {
            arg = malloc(sizeof(*arg) + strlen(token) + 1);
            arg->type = FETCHFMT_ARG;
            arg->flags = 0;
            strcpy(arg->arg, token);
            n_array_push(args, arg);
            
        } else if (strlen(token) > 3) {
            vfile_err_fn("%s: invalid format specified\n", fmt);
            goto l_err_end;
            
        } else {
            char c;
            arg = malloc(sizeof(*arg));

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
                        vfile_err_fn("%s: invalid format specified\n", fmt);
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
                        vfile_err_fn("%s: invalid format specified\n", fmt);
                        goto l_err_end;
                    } 
                        
                    break;
                    
                case 'd':
                    arg->type = FETCHFMT_DIR;
                    has_d_arg++;
                    if (c != '\0') {
                        vfile_err_fn("%s: invalid format specified\n", fmt);
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
                        vfile_err_fn("%s: %c invalid format specified\n",
                                     fmt, c);
                        goto l_err_end;
                    }
                    
                    break;

                default:
                    vfile_err_fn("%s: invalid format specified\n", fmt);
                    goto l_err_end;
            }
            n_array_push(args, arg);
        }
    }
    
    
    if (n_array_size(args) > 2 && has_d_arg && has_p_arg) {
        ftch = malloc(sizeof(*ftch) + strlen(path) + 1);
        ftch->args = args;
        ftch->is_multi = is_multi;
        ftch->urltypes = urltypes;
        strcpy(ftch->path, path);
    } else
        goto l_err_end;
    
    return ftch;

 l_err_end:
    if (args) 
        n_array_free(args);
    return NULL;
}

static 
int process_output(struct p_open_st *st, const char *prefix) 
{
    int c, endl = 1, cnt = 0;
    
    if (prefix == NULL)
        prefix = st->cmd;

    setvbuf(st->stream, NULL, _IONBF, 0);
    while ((c = fgetc(st->stream)) != EOF) {
        if (*vfile_verbose == 0)
            continue;
        
        if (endl) {
            vfile_msg_fn("%s: ", prefix);
            endl = 0;
        }

        vfile_msg_fn("_%c", c);
        if (c == '\n' && cnt > 0)
            endl = 1;
        
        cnt++;
    }
    
    return 1;
}


static
int ffetch_file(struct ffetcher *fftch, const char *destdir,
                const char *url /* or */, tn_array *urls)
{
    char              *bn = NULL, **argv;
    struct p_open_st  pst;
    int               i, n, ec;
    unsigned          p_open_flags = 0;

    
    mkdir(destdir, 0755);

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
                
            case FETCHFMT_DIRBN: 
                argv[n] = alloca(strlen(destdir) + strlen(bn) + 2);
                sprintf(argv[n], "%s/%s", destdir, bn ? bn : "ERROR");
                n++;
                break;

            case FETCHFMT_DIR:
                argv[n++] = (char*)destdir;
                break;

            case FETCHFMT_BN:
                if (url) {
                    argv[n++] = bn;
                } else {
                    int i;
                    for (i=0; i<n_array_size(urls); i++) 
                        argv[n++] = n_basenam(n_array_nth(urls, i));
                }
                break;

            case FETCHFMT_FN:
                if (url) {
                    argv[n++] = (char*)url;
                } else {
                    int i;
                    for (i=0; i<n_array_size(urls); i++) 
                        argv[n++] = n_array_nth(urls, i);
                }
                break;

            default:
                vfile_err_fn("vfile_fetch*: internal error");
                n_assert(0);
                return 0;
        }
    }
    argv[n++] = NULL;

    if (*vfile_verbose) {
        int i, len = 0;
        char *s, *p;


        for (i=0; i < n-1; i++)
            len += strlen(argv[i]) + 1;
        len++;
        
        p = s = alloca(len);
        *s = '\0';
        
        for (i=0; i < n-1; i++) {
            p = n_strncpy(p, argv[i], len);
            len -= strlen(argv[i]);
            p = n_strncpy(p, " ", len);
            len--;
        }
        vfile_msg_fn("Running %s\n", s);
    }
    
    p_st_init(&pst);

    if (fftch->urltypes & VFURL_CDROM)
        p_open_flags |= P_OPEN_KEEPSTDIN;
    
    if (p_open(&pst, p_open_flags, fftch->path, argv) == NULL) {
        vfile_err_fn("p_open: %s\n", pst.errmsg);
        return 0;
    }
    
    process_output(&pst,
                   ((struct fetcharg*) n_array_nth(fftch->args, 0))->arg);
        
    if ((ec = p_close(&pst)) != 0)
        vfile_err_fn("%s", pst.errmsg);

    p_st_destroy(&pst);
    
    return ec == 0;
}


int vfile_register_ext_handler(unsigned urltypes, const char *fmt) 
{
    struct ffetcher *ftch;
    char *s;
    int len;
    
    if (nffetchers == MAX_FETCHERS)
        return 0;
    
    len = strlen(fmt) + 1;
    s = alloca(len);
    memcpy(s, fmt, len);
    
    if ((ftch = ffetcher_new(urltypes, s)) == NULL) {
        vfile_err_fn("External downloader '%s' not registered\n", fmt);
        
    } else {
        ffetchers[nffetchers++] = ftch;
        return nffetchers;
    }
    return 0;
}


static
struct ffetcher *find_fetcher(int urltype, int multi) 
{
    int i;
    
    for (i=0; i<nffetchers; i++) {
        if (urltype & ffetchers[i]->urltypes) {
            if (!multi)
                return ffetchers[i];
            else if (ffetchers[i]->is_multi)
                return ffetchers[i];
        }
    }
    return NULL;
}


int vfile_fetch_ext(const char *destdir, const char *url, int urltype) 
{
    struct ffetcher *ftch;

    n_assert(urltype > 0);
    if (urltype == VFURL_UNKNOWN)
        urltype = vfile_url_type(url);
    
    if (nffetchers == 0) {
        vfile_err_fn("vfile_fetch: %s: no handlers configured\n", url);
        return 0;
    }
    
    if ((ftch = find_fetcher(urltype, 0)) == NULL) {
        vfile_err_fn("vfile_fetch: %s: no handler for this URL\n", url);
        return 0;
    }

    return ffetch_file(ftch, destdir, url, NULL);
}


int vfile_fetcha_ext(const char *destdir, tn_array *urls, int urltype) 
{
    struct ffetcher *ftch;
    int rc = 1;

    n_assert(urltype > 0);
    if (urltype == VFURL_UNKNOWN) 
        urltype = vfile_url_type(n_array_nth(urls, 0));
    
    if ((ftch = find_fetcher(urltype, 1))) {
        rc = ffetch_file(ftch, destdir, NULL, urls);
        
    } else if ((ftch = find_fetcher(urltype, 0))) {
        int i;
        int nerrs = 0;
        
        for (i=0; i<n_array_size(urls); i++) 
            if (!ffetch_file(ftch, destdir, n_array_nth(urls, i), NULL))
                nerrs++;
        rc = nerrs == 0;
        
    } else {
        vfile_err_fn("URL %s not supported\n", n_array_nth(urls, 0));
        rc = 0;
    }

    return rc;
}


static 
char *url_to_path(char *buf, size_t size, const char *url, int isdir) 
{
    char *sl, *p = buf;
    size_t len = strlen(url) + 1;

    memcpy(buf, url, len > size - 1 ? size - 1 : len);
    buf[size - 1] = '\0';

    if (isdir)
        sl = strchr(buf, '\0');
    else
        sl = strrchr(buf, '/');
    
    while (*p && p != sl) {
        if (!isalnum(*p))
            *p = '_';
        p++;
    }
    
    return buf;
}


char *vfile_url_as_dirpath(char *buf, size_t size, const char *url) 
{
    return url_to_path(buf, size, url, 1);
}


char *vfile_url_as_path(char *buf, size_t size, const char *url) 
{
    return url_to_path(buf, size, url, 0);
}


int vfile_url_type(const char *url)  
{
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

    return VFURL_PATH;
}
