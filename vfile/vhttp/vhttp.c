/* 
  Copyright (C) 2002 Pawel A. Gajda <mis@k2.net.pl>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* $Id$ */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>

#include <utmp.h>
#include <pwd.h>

#ifndef IPPORT_HTTP
# define IPPORT_HTTP 80
#endif


#include <trurl/nlist.h>

#include "http.h"
#include "vhttp.h"
#include "i18n.h"
static void vhttp_msg(const char *fmt, ...);

void (*vhttp_msg_fn)(const char *fmt, ...) = vhttp_msg;

static tn_list *vhttp_cnl = NULL;               /* connections */

int vhttp_init(int *verbose, 
              void (*progress_fn)(long total, long amount, void *data)) 
{
    if (progress_fn)
        http_progress_fn = progress_fn;

    vhttp_verbose = verbose;
    
    vhttp_cnl = n_list_new(TN_LIST_UNIQ, (t_fn_free)httpcn_free, NULL);
    return vhttp_cnl != NULL;
}

void vhttp_destroy(void) 
{
    n_list_free(vhttp_cnl);
    vhttp_cnl = NULL;
}


static int toremove_cn_fakecmp(const void *a, const void *b) 
{
    const struct httpcn *cn = a;

    b = b;
    if (cn->state == HTTPCN_DEAD) 
        return 0;
    return -1;
}

void vhttp_vacuum(void) 
{
    n_list_remove_ex(vhttp_cnl, NULL, toremove_cn_fakecmp);
}


static
int do_vhttp_retr(FILE *stream, long offset, const char *url,
                  void *progress_data, int recursion_level) 
{
    tn_list_iterator   it;
    struct httpcn       *cn;
    char               buf[PATH_MAX], redirect_to[PATH_MAX];
    char               *p, *q, *host, *path;
    const char         *login = NULL, *passwd = NULL;
    int                port = 0, rc;
    char               *err_msg = _("%s: URL parse error");

    
    vhttp_set_err(0, "");

    if (recursion_level > 10) {
        vhttp_set_err(EINVAL, "redir to %s: too many redirects", url);
        return 0;
    }
    
    if ((rc = strncmp(url, "http://", sizeof("http://") - 1)) != 0) {
        vhttp_set_err(EINVAL, err_msg, url);
        return 0;
    }
    
    snprintf(buf, sizeof(buf), "%s", url + sizeof("http://") - 1);
    host = buf;
    
    if ((q = strchr(buf, '/')) == NULL) {
        vhttp_set_err(EINVAL, err_msg, url);
        return 0;
    }
    
    *q = '\0';
    path = q;

    /* extract loginname from hostpart */
    if ((p = strrchr(host, '@')) != NULL) {
        *p = '\0';
        login = host;
        host = p + 1;

        if ((p = strchr(login, ':')) == NULL) {
            vhttp_set_err(EINVAL, err_msg, url);
            return 0;
        }
        *p = '\0';
        passwd = p + 1;
    }

    if (port <= 0)
        port = IPPORT_HTTP;
    
    if ((p = strrchr(host, ':'))) {
        if (sscanf(p + 1, "%d", &port) != 1) {
            vhttp_set_err(EINVAL, err_msg, url);
            return 0;
        }
        *p = '\0';
    }
    
    
    vhttp_vacuum();
    n_list_iterator_start(vhttp_cnl, &it);
    while ((cn = n_list_iterator_get(&it))) {
        if (strcmp(cn->host, host) == 0 && cn->port == port &&
            httpcn_is_alive(cn)) {

            if (cn->login && login && strcmp(cn->login, login) != 0 && 
                cn->passwd && passwd && strcmp(cn->passwd, passwd) != 0)
                continue;
            
            if (*vhttp_verbose > 1)
                vhttp_msg_fn("Reusing connection %s%s%s:%d\n",
                             cn->login ? cn->login : "",
                             cn->login ? "@" : "",
                             cn->host, cn->port);
            break;
        }
    }
    
    if (cn == NULL) {
        cn = httpcn_new(host, port, login, passwd);
        if (cn)
            n_list_push(vhttp_cnl, cn);
    }
    
    *q = '/';
    
    if (cn == NULL)
        return 0;
    
    rc = httpcn_retr(cn, fileno(stream), offset, path, progress_data,
                     redirect_to, sizeof(redirect_to));


    if (rc == 0 && *redirect_to) {
        char topath[PATH_MAX], *topathp = topath;

        *q = '\0';
        if (*redirect_to == '/') 
            snprintf(topath, sizeof(topath), "http://%s%s", host, redirect_to);
        
        else if (strncmp(redirect_to, "http://", 7) == 0)
            topathp = redirect_to;

        else {
            vhttp_set_err(EINVAL, "%s: invalid redirect URI", redirect_to);
            topathp = NULL;
        }
        
        if (topathp) 
            rc = do_vhttp_retr(stream, offset, topathp, progress_data,
                               ++recursion_level);
    }
    
    return rc;
}


int vhttp_retr(FILE *stream, long offset, const char *url, void *progress_data)
{
    return do_vhttp_retr(stream, offset, url, progress_data, 0);
}


static void vhttp_msg(const char *fmt, ...)
{
    va_list args;
    
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    fflush(stdout);
    va_end(args);
}
