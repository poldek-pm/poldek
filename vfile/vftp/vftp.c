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

#ifndef IPPORT_FTP
# define IPPORT_FTP 21
#endif


#include <trurl/nlist.h>

#include "ftp.h"
#include "i18n.h"
static void vftp_msg(const char *fmt, ...);

void (*vftp_msg_fn)(const char *fmt, ...) = vftp_msg;


static tn_list *vftp_cnl = NULL;               /* connections */
static char errmsg[256] = { '\0' };
static int vftp_errno = 0;
static int vftp_verbose = 0;

const char *vftp_errmsg(void) 
{
    if (vftp_errno)
        return errmsg;
    
    return ftp_errmsg();
}



int vftp_init(int verbose, 
              void (*progress_fn)(long total, long amount, void *data)) 
{
    if (progress_fn)
        ftp_progress_fn = progress_fn;

    vftp_verbose = verbose;
    ftp_verbose = &vftp_verbose;
    
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

int vftp_retr(FILE *stream, long offset, const char *url,
              void *progress_data) 
{
    tn_list_iterator   it;
    struct ftpcn       *cn;
    char               buf[PATH_MAX];
    char               *p, *q, *host, *path;
    int                port = 0, rc;
    char               *err_msg = _("%s: URL parse error");


    vftp_errno = 0;
    
    
    if ((rc = strncmp(url, "ftp://", sizeof("ftp://") - 1)) != 0) {
        snprintf(errmsg, sizeof(errmsg), err_msg, url);
        vftp_errno = 1;
        return 0;
    }
    
    snprintf(buf, sizeof(buf), "%s", url + sizeof("ftp://") - 1);
    host = buf;
    
    if ((q = strchr(buf, '/')) == NULL) {
        snprintf(errmsg, sizeof(errmsg), err_msg, url);
        vftp_errno = 1;
        return 0;
    }
    
    *q = '\0';
    path = q;

    if (port <= 0)
        port = IPPORT_FTP;
    
    if ((p = strrchr(host, ':'))) {
        if (sscanf(p + 1, "%d", &port) == 1) {
            snprintf(errmsg, sizeof(errmsg), err_msg, url);
            vftp_errno = 1;
            return 0;
        }
        *p = '\0';
    }
    
    
    vftp_vacuum();
    n_list_iterator_start(vftp_cnl, &it);
    while ((cn = n_list_iterator_get(&it))) {
        if (strcmp(cn->host, host) == 0 && cn->port == port &&
            ftpcn_is_alive(cn)) {
            
            if (vftp_verbose > 1)
                vftp_msg_fn("Reusing connection %s:%d\n", cn->host, cn->port);
            break;
        }
    }
    
    if (cn == NULL) {
        cn = ftpcn_new(host, port, "anonymous", "poldek@znienacka.net");
        if (cn)
            n_list_push(vftp_cnl, cn);
    }
    
    *q = '/';
    
    if (cn == NULL)
        return 0;
    
    return ftpcn_retr(cn, fileno(stream), offset, path, progress_data);
}

static void vftp_msg(const char *fmt, ...)
{
    va_list args;
    
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    fflush(stdout);
    va_end(args);
}
