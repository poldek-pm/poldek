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
#include <fnmatch.h>
#include <errno.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include <fcntl.h>
#include <time.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nhash.h>
#include <trurl/narray.h>
#include <trurl/nmalloc.h>

#include "i18n.h"

#include "vfile.h"
#include "vfile_intern.h"

char *vfff_uri_escape(const char *path);

static int extr_port(char *host) 
{
    char *p;
    int port = 0;
    
    if ((p = strrchr(host, ':'))) {
        if (sscanf(p + 1, "%d", &port) != 1)
            return -1;
        
        *p = '\0';
    }

    return port;
}

static
int vf_parse_url(char *url, struct vf_request *req) 
{
    char               *p, *q;

    if ((p = strstr(url, "://")) == NULL) 
        return 0;
    
    req->proto = url;
    *p = '\0';
    p += 3;
    req->host = p;
    
    if ((q = strchr(p, '/')) != NULL) {
        *q = '\0';
        req->uri = q+1;
    }

    /* extract loginname from hostpart */
    if ((p = strrchr(req->host, '@')) != NULL) {
        *p = '\0';
        req->login = req->host;
        req->host = p + 1;

        if ((p = strchr(req->login, ':')) == NULL) 
            return 0;

        *p = '\0';
        req->passwd = p + 1;
    }

    if ((req->port = extr_port(req->host)) < 0) 
        return 0;

    n_assert(req->proto && req->host);
    return 1;
}

static tn_array *get_noproxy(void)
{
    const char **tl, **tl_save, *v;
    tn_array *noproxy;
    
    if ((v = getenv("no_proxy")) == NULL || *v == 0)
        return NULL;

    tl = tl_save = n_str_tokl(v, ", \t");
    
    noproxy = n_array_new(16, free, NULL);
    while (*tl) {
        n_array_push(noproxy, n_strdup(*tl));
        tl++;
    }
    
    n_str_tokl_free(tl_save);
    return noproxy;
}

static int is_proxable(struct vf_request *req) 
{
    tn_array *noproxy = vfile_conf.noproxy;
    int i;

    
    if (n_array_size(vfile_conf.noproxy) == 0) {
        noproxy = get_noproxy();
        if (noproxy == NULL)
            return 1;
    }
    
    for (i=0; i < n_array_size(noproxy); i++) {
        const char *mask = n_array_nth(noproxy, i);
        if (*mask == '.') {
            int  size = strlen(mask) + 2;
            char *tmp = alloca(size);
            n_snprintf(tmp, size, "*%s", mask);
            mask = tmp;
        }

        if (fnmatch(mask, req->host, 0) == 0) {
            if (*vfile_verbose > 1)
                vf_loginfo("Non proxable %s (match %s)\n", req->host, mask);
            return 0;
        }
    }
    
    /* no configured noproxy, env is set */
    if (noproxy && noproxy != vfile_conf.noproxy) { 
        while (n_array_size(noproxy))
            n_array_push(vfile_conf.noproxy, n_array_shift(noproxy));
        
        n_array_free(noproxy);
    }
    
    return 1;
}


static const char *get_proxy(struct vf_request *req)
{
    char *url = NULL, buf[256];

    if (!is_proxable(req))
        return NULL;

    if ((url = n_hash_get(vfile_conf.proxies_ht, req->proto)))
        return url;
    
    snprintf(buf, sizeof(buf), "%s_proxy", req->proto);

    if ((url = getenv(buf)) == NULL || *url == 0) {
        char *p;

        p = buf;
        while (*p) {
            *p = toupper(*p);
            p++;
        }

        url = getenv(buf);
    }

    if (url) {                  /* save it in proxies_ht */
        if (*url == '\0')
            url = NULL;

        else {
            url = n_strdup(url);
            n_hash_insert(vfile_conf.proxies_ht, req->proto, url);
        }
    } 

    return url;
}

int vf_request_open_destpath(struct vf_request *req)
{
    int          fd;
    struct stat  st;

    
    n_assert(req->dest_fd <= 0);
    n_assert(req->destpath);
    
    if ((fd = open(req->destpath, O_WRONLY|O_APPEND|O_CREAT, 0644)) < 0) {
        vf_logerr("open %s: %m\n", req->destpath);
        return 0;
    }
    
    if (fstat(fd, &st) != 0) {
        vf_logerr("fstat %s: %m\n", req->destpath);
        close(fd);
        return 0;
    }
    
    req->dest_fd = fd;
    req->dest_fdoff = st.st_size;
    if (st.st_size > 0) {      /* existing file */
        req->st_local_mtime = st.st_mtime;
        req->st_local_size = st.st_size;
    }
    return 1;
}

struct vf_request *vf_request_new(const char *url, const char *destpath)
{
    char               buf[PATH_MAX], pbuf[PATH_MAX], tmp[PATH_MAX], *escaped_uri;
    const char         *proxy = NULL;
    char               *err_msg = _("%s: URL parse error\n");
    struct vf_request  *req, rreq, preq;
    int                len;

    memset(&rreq, 0, sizeof(rreq));

    //printf("**request new %s\n", url);
    snprintf(buf, sizeof(buf), "%s", url);
    
    if (!vf_parse_url(buf, &rreq) || rreq.uri == NULL) {
        vf_logerr(err_msg, CL_URL(url));
        return NULL;
    }
    
    if ((proxy = get_proxy(&rreq))) {

        snprintf(pbuf, sizeof(pbuf), "%s", proxy);
        memset(&preq, 0, sizeof(preq));
        
        if (!vf_parse_url(pbuf, &preq)) {
            vf_logerr(err_msg, CL_URL(proxy));
            return NULL;
        }
    }
    
    if (destpath) {
        rreq.destpath = (char*)destpath;
        if (!vf_request_open_destpath(&rreq))
            return NULL;
    }
    
    req = n_malloc(sizeof(*req));
    memset(req, 0, sizeof(*req));
    
    if (destpath)
        req->destpath = n_strdup(destpath);
    else
        req->destpath = NULL;
    
    req->dest_fd = rreq.dest_fd;
    req->dest_fdoff = rreq.dest_fdoff;
    req->st_local_size = rreq.st_local_size;
    req->st_local_mtime = rreq.st_local_mtime;
    
    req->proto = n_strdup(rreq.proto);
    req->host = n_strdup(rreq.host);

    len = n_snprintf(tmp, sizeof(tmp), "/%s", rreq.uri);

    /* escape URI if HTTP */
    if (n_str_eq(req->proto, "http") && (escaped_uri = vfff_uri_escape(tmp)))
        req->uri = escaped_uri;
    else 
        req->uri = n_strdupl(tmp, len);

    len = n_snprintf(tmp, sizeof(tmp), "%s://%s%s", rreq.proto, rreq.host,
                     req->uri);
    req->url = n_strdupl(tmp, len);
    req->port = rreq.port;

    
    if (rreq.login)
        req->login = n_strdup(rreq.login);

    if (rreq.passwd)
        req->passwd = n_strdup(rreq.passwd);

    if (proxy && *proxy) {
        len = n_snprintf(tmp, sizeof(tmp), "%s://%s", preq.proto, preq.host);
        req->proxy_url = n_strdupl(tmp, len);
        
        req->proxy_proto = n_strdup(preq.proto);
        req->proxy_host = n_strdup(preq.host);
        if (preq.login)
            req->proxy_login = n_strdup(preq.login);

        if (preq.passwd)
            req->proxy_passwd = n_strdup(preq.passwd);
        req->proxy_port = preq.port;
    }
    
    return req;
}

void vf_request_close_destpath(struct vf_request *req)
{
    if (req->dest_fd > 0) {
        struct utimbuf ut;

        n_assert(req->destpath);
        if (*vfile_verbose > 1) {
            char timbuf[64] = { '\0' };

            if (req->st_remote_mtime > 0) 
                strftime(timbuf, sizeof(timbuf), "(mtime %Y-%m-%d %H:%M:%S)",
                     gmtime(&req->st_remote_mtime));
            
            vf_loginfo("Closing %s %s\n", req->destpath, timbuf);
        }
        
        if (req->st_remote_mtime > 0) {
            ut.actime = ut.modtime = req->st_remote_mtime;
            utime(req->destpath, &ut);
        }

        close(req->dest_fd);
        req->dest_fd = -1;
        req->st_local_size = 0;
        req->st_local_mtime = 0;
    }
}


void vf_request_free(struct vf_request *req)
{
    n_cfree(&req->url);
    n_cfree(&req->proto);
    n_cfree(&req->host);
    n_cfree(&req->uri);

    n_cfree(&req->login);
    n_cfree(&req->passwd);

    n_cfree(&req->proxy_url);
    n_cfree(&req->proxy_proto);
    n_cfree(&req->proxy_host);
    n_cfree(&req->proxy_login);
    n_cfree(&req->proxy_passwd);

    vf_request_close_destpath(req);
    n_cfree(&req->destpath);
    free(req);
}

#define xx_replace(p, q) n_cfree(&p); p = q; q = NULL; 

struct vf_request *vf_request_redirto(struct vf_request *req, const char *url)
{
    struct vf_request *tmpreq;

    tmpreq = vf_request_new(url, NULL);
    if (tmpreq == NULL)
        return NULL;
    
    if (*vfile_verbose > 1)
        vf_loginfo("Redirected to %s\n", PR_URL(tmpreq->url));
    
    xx_replace(req->url, tmpreq->url);

    if (strcmp(req->proto, tmpreq->proto) != 0)
        req->flags |= VF_REQ_INT_REDIRECTED;

    if ((req->flags & VF_REQ_INT_REDIRECTED) ||
        strcmp(req->host, tmpreq->host) != 0) {
        
        xx_replace(req->proto, tmpreq->proto);
        xx_replace(req->host, tmpreq->host);
        n_cfree(&req->login);
        n_cfree(&req->passwd);
    }
    
    xx_replace(req->uri, tmpreq->uri);
    req->port = tmpreq->port;
    xx_replace(req->proxy_proto, tmpreq->proxy_proto);
    xx_replace(req->proxy_host, tmpreq->proxy_host);
    xx_replace(req->proxy_login, tmpreq->proxy_login);
    xx_replace(req->proxy_passwd, tmpreq->proxy_passwd);

    vf_request_free(tmpreq);
    return req;
}

    
