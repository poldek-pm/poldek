/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include <trurl/nassert.h>
#include <trurl/nlist.h>

#include "i18n.h"
#include "vfile.h"
#include "vfile_intern.h"
#include "vfff/vfff.h"

static int do_stat(struct vf_request *req);
static int do_retr(struct vf_request *req);
static int do_init(void);
static void do_destroy(void);

struct vf_module vf_mod_vfff = {
    "vfff",
    VFURL_HTTP | VFURL_FTP,
    do_init,
    do_destroy, 
    do_retr,
    do_stat, 
    0
};


static tn_list *vcn_pool = NULL;               /* connections */

static 
void do_vlog(const char *fmt, va_list ap)
{
    vf_vlog(VFILE_LOG_INFO, fmt, ap);
}


static
int do_init(void)
{
    vfff_vlog_cb = do_vlog;
    if (vcn_pool == NULL)
        vcn_pool = n_list_new(TN_LIST_UNIQ, (tn_fn_free)vcn_free, NULL);
    return vcn_pool != NULL;
}

static
void do_destroy(void) 
{
    n_list_free(vcn_pool);
    vcn_pool = NULL;
}


static int toremove_cn_fakecmp(const void *a, const void *b) 
{
    const struct vcn *cn = a;

    b = b;
    if (cn->state != VCN_ALIVE) 
        return 0;
    return -1;
}

void vcn_pool_vacuum(void) 
{
    n_list_remove_ex(vcn_pool, NULL, toremove_cn_fakecmp);
}

static struct vcn *vcn_pool_do_connect(struct vf_request *req)
{
    tn_list_iterator   it;
    struct vcn         *cn;
    char               *host, *login = NULL, *passwd = NULL;
    int                port, vcn_proto = 0;
    
    
    host = req->host;
    port = req->port;
    login = req->login;
    passwd = req->passwd;

    if (strcmp(req->proto, "http") == 0)
        vcn_proto = VCN_PROTO_HTTP;
    
    else if (req->proxy_proto && strcmp(req->proxy_proto, "http") == 0)
        vcn_proto = VCN_PROTO_HTTP;
    
    else if (strcmp(req->proto, "ftp") == 0)
        vcn_proto = VCN_PROTO_FTP;
    
    else
        n_assert(0);

    switch (vcn_proto) {
        case VCN_PROTO_HTTP:
            if (req->proxy_host) {
                host = req->proxy_host;
                port = req->proxy_port;
            }
            if (port <= 0)
                port = IPPORT_HTTP;
            break;
            
        case VCN_PROTO_FTP:
            if (login == NULL)
                login = "anonymous";
        
            if (passwd == NULL)
                passwd = vfile_conf.anon_passwd;

            n_assert(passwd);
        
            if (req->proxy_host) {
                int len;
                char *s;
                
                len = strlen(login) + 1 + strlen(req->host) + 1;
                s = alloca(len);
                n_snprintf(s, len, "%s@%s", login, req->host);
                
                login = s;
                host = req->proxy_host;
                port = req->proxy_port;
            }

            if (port <= 0)
                port = IPPORT_FTP;
            break;
            
        default:
            n_assert(0);
    }

    if (vcn_pool == NULL)
        do_init();
    
    vcn_pool_vacuum();
    n_list_iterator_start(vcn_pool, &it);
    while ((cn = n_list_iterator_get(&it))) {
        if (cn->proto != vcn_proto)
            continue;

        if (strcmp(cn->host, host) == 0 && cn->port == port) {
            if (cn->login) {
                if (login == NULL || strcmp(cn->login, login) != 0)
                    continue;
                
            } else if (login) {
                continue;
            }

            if (cn->passwd) {
                if (passwd == NULL || strcmp(cn->passwd, passwd) != 0)
                    continue;
                
            } else if (passwd) {
                continue;
            }

            if (!vcn_is_alive(cn))
                continue;
            
            if (*vfile_verbose > 1)
                vf_loginfo("Reusing connection %s%s%s:%d\n",
                            cn->login ? cn->login : "",
                            cn->login ? "@" : "",
                            cn->host, cn->port);
            break;
        }
    }
    
    if (cn == NULL) {
        cn = vcn_new(vcn_proto, host, port, login, passwd,
                     req->proxy_login, req->proxy_passwd);
        if (cn)
            n_list_push(vcn_pool, cn);
    }
    
    return cn;
}

#define DO_RETR 1
#define DO_STAT 2

struct do_fn {
    int type;
    int (*fn)(struct vcn *, struct vfff_req *);
};

static void set_err(struct vf_request *req, int err_no, const char *fmt, ...)
{
    va_list args;
    
    va_start(args, fmt);
    vf_vlog(VFILE_LOG_ERR, fmt, args);
    va_end(args);
    req->req_errno = err_no;
}

static
int do_vfn(const struct do_fn *dofn, struct vf_request *req,
           int recursion_deep)
{
    struct vcn        *cn;
    struct vfff_req   vreq;
    int                rc;
    
    vfff_verbose = vfile_verbose;
    req->req_errno = 0;

    if (recursion_deep > 32) {
        set_err(req, EINVAL, "too many (%d) redirects", recursion_deep);
        return 0;
    }

    if ((cn = vcn_pool_do_connect(req)) == NULL)
        return 0;

    memset(&vreq, 0, sizeof(vreq));
    vreq.uri = req->proxy_host ? req->url : req->uri;
    
    if (req->dest_fd > 0) {
        vreq.out_path = req->destpath;
        vreq.out_fd = req->dest_fd;
        vreq.out_fdoff = req->dest_fdoff;
        if (req->bar) {
            vreq.progress_fn_data = req->bar;
            vreq.progress_fn = vf_progress;
        }
    }
    
    *vreq.redirected_to = '\0';
    
    if ((rc = dofn->fn(cn, &vreq))) {
        req->st_remote_mtime = vreq.st_remote_mtime;
        req->st_remote_size = vreq.st_remote_size;
        
    } else if (*vreq.redirected_to) {
        char topath[PATH_MAX], *topathp = vreq.redirected_to;
        int  foreign_proto = 0;

        n_assert(cn->proto == VCN_PROTO_HTTP);
        
        if (*vreq.redirected_to == '/') {
            snprintf(topath, sizeof(topath), "http://%s%s", req->host,
                     vreq.redirected_to);
            topathp = topath;
            
        } else if (strncmp(vreq.redirected_to, "http://", 7) != 0) 
            foreign_proto = 1;
        
        if (topathp && vf_request_redirto(req, topathp)) {
            rc = 0;
            if (foreign_proto == 0)
                rc = do_vfn(dofn, req, ++recursion_deep);

        } else {
            set_err(req, EINVAL, "%s: invalid redirect URI", vreq.redirected_to);
            rc = 0;
        }
    }
    
    return rc;
}

static
int do_retr(struct vf_request *req)
{
    struct do_fn dofn;
    int rc;
    
    dofn.type = DO_RETR;
    dofn.fn = vcn_retr;
    
    if (!(rc = do_vfn(&dofn, req, 0))) {
        req->req_errno = vfff_errno;
        if ((req->flags & VF_REQ_INT_REDIRECTED) == 0)
            vf_logerr("%s: %s\n", vf_mod_vfff.vfmod_name, vfff_errmsg());
    }
    
    return rc;
}

static
int do_stat(struct vf_request *req)
{
    struct do_fn dofn;
    int rc;
    
    dofn.type = DO_STAT;
    dofn.fn = vcn_stat;

    if (!(rc = do_vfn(&dofn, req, 0))) {
        req->req_errno = vfff_errno;
        if ((req->flags & VF_REQ_INT_REDIRECTED) == 0)
            vf_logerr("%s: %s\n", vf_mod_vfff.vfmod_name, vfff_errmsg());
    }
    
    return rc;
}
