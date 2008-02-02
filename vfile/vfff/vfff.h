/* $Id$ */
/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef VFFF_VCN_H
#define VFFF_VCN_H

#include <stdarg.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <netinet/in.h>
#include <time.h>

#ifndef IPPORT_HTTP
# define IPPORT_HTTP 80
#endif

#ifndef IPPORT_FTP
# define IPPORT_FTP 21
#endif

#define VFFF_TIMEOUT 30

extern int vfff_errno;
extern int *vfff_verbose;

extern void (*vfff_vlog_cb)(const char *fmt, va_list ap);

const char *vfff_errmsg(void);
void vfff_set_err(int err_no, const char *fmt, ...);
void vfff_log(const char *fmt, ...);
int vfff_sigint_reached(void);
int vfff_to_connect(const char *host, const char *service, int *af);


struct vfff_req;

/* state */
#define VCN_CLOSED  0           
#define VCN_ALIVE   1
#define VCN_DEAD    3

/* proto */
#define VCN_PROTO_FTP  0
#define VCN_PROTO_HTTP 1

/* flags */
#define VCN_SUPPORTS_SIZE  (1 << 0)
#define VCN_SUPPORTS_MDTM  (1 << 1)

struct vcn {
    int       proto;
    int       afamily;
    
    int       state;
    unsigned  flags;
    int       sockfd;
    char      *host;
    int       port;

    char      *login;
    char      *passwd;
    char      *auth_basic_str;
    
    char      *proxy_login;
    char      *proxy_passwd;
    char      *proxy_auth_basic_str;

    int       (*m_open)(struct vcn *cn);
    void      (*m_close)(struct vcn *cn);
    int       (*m_retr)(struct vcn *cn, struct vfff_req *req);
    int       (*m_stat)(struct vcn *cn, struct vfff_req *req);
    int       (*m_is_alive)(struct vcn *cn);

    void      (*m_free)(void *resp);
    void      *resp;

    time_t    ts_is_alive;
};

struct vcn *vcn_new(int proto, const char *host, int port,
                    const char *login, const char *passwd,
                    const char *proxy_login, const char *proxy_passwd);
void vcn_close(struct vcn *cn);
void vcn_free(struct vcn *cn);
int vcn_is_alive(struct vcn *cn);

struct vfff_req {
    const char   *uri;

    const char   *out_path;
    int          out_fd;
    off_t        out_fdoff;

    void         (*progress_fn)(void *data, long total, long amount);
    void         *progress_fn_data;
    
    char         redirected_to[PATH_MAX];

    off_t        st_remote_size;
    time_t       st_remote_mtime;
};

int vcn_retr(struct vcn *cn, struct vfff_req *req);
int vcn_stat(struct vcn *cn, struct vfff_req *req);

int vfff_transfer_file(struct vfff_req *vreq, int in_fd, long total_size);

#endif
