/* 
  Copyright (C) 2002 - 2005 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* $Id$ */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <trurl/nbuf.h>
#include <trurl/nassert.h>
#include <trurl/nhash.h>
#include <trurl/n_snprintf.h>
#include <trurl/nstr.h>
#include <trurl/nmalloc.h>

#include "i18n.h"
#include "vfff.h"
#include "vfile/vfile_intern.h"

#define  VCN_ALIVE_TTL  10

extern void vhttp_vcn_init(struct vcn *cn);
extern void vftp_vcn_init(struct vcn *cn);

static char errmsg[512] = { '\0' };
static int verbose = 0;

int vfff_errno = 0;
int *vfff_verbose = &verbose;
void (*vfff_vlog_cb)(const char *fmt, va_list ap) = NULL;

const char *vfff_errmsg(void) 
{
    if (*errmsg == '\0')
        return "unknown error";
    return errmsg;
}

void vfff_set_err(int err_no, const char *fmt, ...)
{
    va_list args;
    
    va_start(args, fmt);
    vsnprintf(errmsg, sizeof(errmsg), fmt, args);
    va_end(args);
    
    vfff_errno = err_no;
}

int vfff_sigint_reached(void)
{
    int v;
    if ((v = vfile_sigint_reached(0)) && vfff_errno == 0)
        vfff_set_err(EINTR, _("connection cancelled"));

    return v;
}

void vfff_log(const char *fmt, ...)
{
    va_list args;
    
    va_start(args, fmt);
    if (vfff_vlog_cb)
        vfff_vlog_cb(fmt, args);
    else {
        vfprintf(stdout, fmt, args);
        fflush(stdout);
    }
    va_end(args);
}

static sig_atomic_t alarm_reached = 0;

static void sigalarmfunc(int unused)
{
    unused = unused;
    alarm_reached = 1;
    //printf("receive alarm");
}


static void install_alarm(int sec)
{
    struct sigaction act;

    alarm_reached = 0;
    sigaction(SIGALRM, NULL, &act);
    act.sa_flags &=  ~SA_RESTART;
    act.sa_handler =  sigalarmfunc;
    sigaction(SIGALRM, &act, NULL);
    alarm(sec);
};


static void uninstall_alarm(void)
{
    alarm(0);
};


int vfff_to_connect(const char *host, const char *service, int *af)
{
    struct addrinfo hints, *res, *resp;
    int sockfd, n;

    if (af)
        *af = 0;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (*vfff_verbose > 1)
        vfff_log("Connecting to %s:%s...\n", host, service);

    if ((n = getaddrinfo(host, service, &hints, &res)) != 0) {
        vfff_set_err(errno, _("unable to connect to %s:%s: %s"),
                     host, service, gai_strerror(n));
        return -1;
    }
    resp = res;

    vfff_errno = 0;
    
    do {
        sockfd = socket(resp->ai_family, resp->ai_socktype, resp->ai_protocol);
        if (sockfd < 0)
            continue;
        install_alarm(VFFF_TIMEOUT);

        if (connect(sockfd, resp->ai_addr, resp->ai_addrlen) == 0)
            break;

        if (alarm_reached)
            vfff_errno = errno = ETIMEDOUT;

        else if (vfff_sigint_reached() && errno == EINTR)
            break;

        uninstall_alarm();
        close(sockfd);
        sockfd = -1;
        
    } while ((resp = resp->ai_next) != NULL);

    if (sockfd == -1)
        vfff_set_err(errno, _("unable to connect to %s:%s: %m"), host, service);
    
    else if (af)
        *af = resp->ai_family;

    //DBGF("sigint reached %d, errno %m\n", vfff_sigint_reached());
    
    uninstall_alarm();
    freeaddrinfo(res);
    return sockfd;
}


static int cn_open(const char *host, int port, int *af)
{
    int sockfd;
    char portstr[64];

    errno = 0;
    snprintf(portstr, sizeof(portstr), "%d", port);
    if ((sockfd = vfff_to_connect(host, portstr, af)) < 0)
        return 0;

    return sockfd;
}

static
struct vcn *cn_connect(const char *host, int port,
                       const char *login, const char *passwd,
                       const char *proxy_login, const char *proxy_passwd)
{
    struct vcn *cn = NULL;
    int        sockfd, af;

    if ((sockfd = cn_open(host, port, &af)) > 0) {
        cn = n_malloc(sizeof(*cn));
        memset(cn, 0,  sizeof(*cn));

        cn->flags = VCN_SUPPORTS_SIZE | VCN_SUPPORTS_MDTM;
        cn->afamily = af;
        cn->sockfd = sockfd;
        cn->state = VCN_ALIVE;
        cn->host = n_strdup(host);
        cn->port = port;
        cn->login = cn->passwd = cn->proxy_login = cn->proxy_passwd = NULL;
        cn->auth_basic_str = cn->proxy_auth_basic_str = NULL;
        
        if (login && passwd) {
            cn->login = n_strdup(login);
            cn->passwd = n_strdup(passwd);
        } 

        if (proxy_login && proxy_passwd) {
            cn->proxy_login = n_strdup(proxy_login);
            cn->proxy_passwd = n_strdup(proxy_passwd);
        }
        
        cn->resp = NULL;
    }

    return cn;
}


struct vcn *vcn_new(int proto, const char *host, int port,
                   const char *login, const char *passwd,
                   const char *proxy_login, const char *proxy_passwd)
{
    struct vcn *cn;
    int default_port = 0;

    
    switch (proto) {
        case VCN_PROTO_FTP:
            default_port = IPPORT_FTP;
            break;

        case VCN_PROTO_HTTP:
            default_port = IPPORT_HTTP;
            break;

        default:
            n_assert(0);
            break;
    }

    if (port <= 0)
        port = default_port;

    cn = cn_connect(host, port, login, passwd, proxy_login, proxy_passwd);
    if (cn == NULL)
        return NULL;

    cn->proto = proto;
    switch (proto) {
        case VCN_PROTO_FTP:
            vftp_vcn_init(cn);
            break;

        case VCN_PROTO_HTTP:
            vhttp_vcn_init(cn);
            break;

        default:
            n_assert(0);
            break;
    }

    if (cn->m_open && !cn->m_open(cn)) {
        vcn_free(cn);
        cn = NULL;
    }

    return cn;
}

void vcn_close(struct vcn *cn)
{
    if (cn->state == VCN_CLOSED)
        return;

    if (cn->m_close)
        cn->m_close(cn);
    
    cn->state = VCN_CLOSED;
    close(cn->sockfd);
    cn->sockfd = -1;
}


void vcn_free(struct vcn *cn) 
{
    vcn_close(cn);

    n_cfree(&cn->host);
    
    n_cfree(&cn->login);
    n_cfree(&cn->passwd);

    n_cfree(&cn->proxy_login);
    n_cfree(&cn->proxy_passwd);

    n_cfree(&cn->auth_basic_str);
    n_cfree(&cn->proxy_auth_basic_str);
    
    if (cn->resp)
        cn->m_free(cn->resp);
    
    memset(cn, 0, sizeof(*cn));
}

int vcn_is_alive(struct vcn *cn) 
{
    vfff_errno = 0;
    
    if (cn->ts_is_alive > 0) {
        time_t ts = time(0);
    
        if (ts - cn->ts_is_alive < VCN_ALIVE_TTL)
            return 1;
    }

    cn->ts_is_alive = time(0);
    return cn->m_is_alive(cn);
}

int vcn_retr(struct vcn *cn, struct vfff_req *req) 
{
    vfff_errno = 0;
    return cn->m_retr(cn, req);
}

int vcn_stat(struct vcn *cn, struct vfff_req *req) 
{
    vfff_errno = 0;
    return cn->m_stat(cn, req);
}

int vfff_transfer_file(struct vfff_req *vreq, int in_fd, long total_size)
{
    struct  timeval tv;
    int     rc, is_err = 0;
    long    amount = 0;
    

    amount = vreq->out_fdoff;
    
    if (vreq->progress_fn) {
        vreq->progress_fn(vreq->progress_fn_data, total_size, 0);
        if (amount) 
            vreq->progress_fn(vreq->progress_fn_data, total_size, amount);
    }

    while (1) {
        fd_set fdset;

        if (total_size > 0 && amount == total_size)
            break;
        
        FD_ZERO(&fdset);
        FD_SET(in_fd, &fdset);
        
        tv.tv_sec = VFFF_TIMEOUT;
        tv.tv_usec = 0;
        
        rc = select(in_fd + 1, &fdset, NULL, NULL, &tv);
        if (vfff_sigint_reached()) {
            is_err = 1;
            errno = EINTR;
            break;
        }

        if (rc == 0) {
            errno = ETIMEDOUT;
            is_err = 1;
            break;
            
        } else if (rc < 0) {
            if (errno == EINTR)
                continue;
            
            is_err = 1;
            break;
            
        } else if (rc > 0) {
            char buf[8192];
            int n;
            
            if ((n = read(in_fd, buf, sizeof(buf))) == 0) 
                break;
            
            if (n > 0) {
                int nw;
                
                if ((nw = write(vreq->out_fd, buf, n)) != n) {
                    is_err = 1;
                    break;
                }
                amount += nw;
                if (vreq->progress_fn)
                    vreq->progress_fn(vreq->progress_fn_data, total_size, amount);
                
            } else {
                is_err = 1;
                break;
            } 
        }
    }
    
    if (is_err) {
        vfff_errno = errno;
        if (vfff_errno == 0)
            vfff_errno = errno = EIO;

        vfff_set_err(errno, "%m");
    }

    if (vreq->progress_fn)
        vreq->progress_fn(vreq->progress_fn_data, total_size, -1);
    
    return is_err == 0;
}


