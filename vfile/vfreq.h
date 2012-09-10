/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_VFILE_VFREQ_H
#define POLDEK_VFILE_VFREQ_H

#include <time.h>

void *vf_progress_new(const char *label);
void vf_progress_reset(void *bar);
void vf_progress(void *bar, long total, long amount);
void vf_progress_free(void *bar);


#define VF_REQ_INT_REDIRECTED         (1 << 0)

struct vf_request {
    unsigned  flags;
    
    char      *url;
    char      *proto;
    char      *host;
    int       port;
    char      *uri;
    char      *login;
    char      *passwd;

    char      *proxy_url;
    char      *proxy_proto;
    char      *proxy_host;
    int       proxy_port;
    char      *proxy_login;
    char      *proxy_passwd;

    /* destination  */
    char      *destpath;
    int       dest_fd;
    int       dest_fdoff;
    
    void      *bar;             /* progress bar */

    /* filled by module's stat()s */
    time_t    st_remote_mtime;
    off_t     st_remote_size;

    time_t    st_local_mtime;
    off_t     st_local_size;
    
    int       req_errno;
};


struct vf_request *vf_request_new(const char *url, const char *destpath);
void vf_request_free(struct vf_request *req);

int vf_request_open_destpath(struct vf_request *req);
void vf_request_close_destpath(struct vf_request *req);

struct vf_request *vf_request_redirto(struct vf_request *req, const char *url);

#define vf_request_resetflags(req) req->flags &= ~VF_REQ_INT_REDIRECTED;

#endif
