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

#ifndef IPPORT_FTP
# define IPPORT_FTP 21
#endif

#include <trurl/nmalloc.h>
#include <trurl/nlist.h>

#include "ftp.h"
#include "vftp.h"
#include "i18n.h"
static void vftp_msg(const char *fmt, ...);

void (*vftp_msg_fn)(const char *fmt, ...) = vftp_msg;

static tn_list *vftp_cnl = NULL;               /* connections */
const char *vftp_anonpasswd = "vftp@mis";

int vftp_init(int *verbose, 
              void (*progress_fn)(long total, long amount, void *data)) 
{
    if (progress_fn)
        ftp_progress_fn = progress_fn;

    vftp_verbose = verbose;
    
    vftp_cnl = n_list_new(TN_LIST_UNIQ, (t_fn_free)ftpcn_free, NULL);
    return vftp_cnl != NULL;
}

void vftp_destroy(void) 
{
    n_list_free(vftp_cnl);
    vftp_cnl = NULL;
}


static int toremove_cn_fakecmp(const void *a, const void *b) 
{
    const struct ftpcn *cn = a;

    b = b;
    if (cn->state == FTPCN_DEAD) 
        return 0;
    return -1;
}

void vftp_vacuum(void) 
{
    n_list_remove_ex(vftp_cnl, NULL, toremove_cn_fakecmp);
}

int vftp_retr(struct vf_request *req)
{
    tn_list_iterator   it;
    struct ftpcn       *cn;
    const char         *passwd;
    char               *login, *host;
    int                port;

    
    login = req->login;
    host = req->host;
    port = req->port;

    if (login == NULL)
        login = "anonymous";
        
    if (req->proxy_host) {
        int len;
        char *s;
        
        len = strlen(login) + 1 + strlen(req->host) + 1;
        s = alloca(len);
        snprintf(s, len, "%s@%s", login, req->host);
        
        login = s;
        host = req->proxy_host;
        port = req->proxy_port;
    }
    
    if (port <= 0)
        port = IPPORT_FTP;
    
    vftp_set_err(0, "");

    passwd = req->passwd;
    if (passwd == NULL && (passwd = vftp_anonpasswd) == NULL)
        passwd  = "vftp@mis";
    
    vftp_vacuum();
    n_list_iterator_start(vftp_cnl, &it);
    while ((cn = n_list_iterator_get(&it))) {
        if (strcmp(cn->login, login) == 0 && strcmp(cn->passwd, passwd) == 0 &&
            strcmp(cn->host, host) == 0 && cn->port == port &&
            ftpcn_is_alive(cn)) {
            
            if (*vftp_verbose > 1)
                vftp_msg_fn("Reusing connection %s@%s:%d\n", login, host, port);
            break;
        }
    }
    
    if (cn == NULL) {
        cn = ftpcn_new(host, port, login, passwd);
        if (cn)
            n_list_push(vftp_cnl, cn);
    }

    if (cn == NULL)
        return 0;
    
    return ftpcn_retr(cn, fileno(req->stream), req->stream_offset, req->uri, req->bar);
}

static void vftp_msg(const char *fmt, ...)
{
    va_list args;
    
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    fflush(stdout);
    va_end(args);
}
