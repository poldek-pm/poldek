#ifndef POLDEK_VFILE_VFREQ_H
#define POLDEK_VFILE_VFREQ_H

#define VF_PROGRESS_VIRGIN    0
#define VF_PROGRESS_DISABLED  1
#define VF_PROGRESS_RUNNING   2

struct vf_progress_bar {
    int     width;
    int     state;
    int     is_tty;
    int     prev_n;
    int     prev_perc;
};

void vfile_progress_init(struct vf_progress_bar *bar);
void vfile_progress(long total, long amount, void *data);




/* send login@host as FTP password */

#define VF_REQ_SYSUSER_AS_ANONPASSWD  (1 << 1) 
#define VF_REQ_INT_REDIRECTED         (1 << 12)

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
    FILE      *stream;
    long      stream_offset;
    
    struct vf_progress_bar *bar;
    
    int       req_errno;
};


struct vf_request *vf_request_new(const char *destpath, const char *url);
void vf_request_free(struct vf_request *req);

struct vf_request *vf_request_redirto(struct vf_request *req, const char *url);

#define vf_request_resetflags(req) req->flags &= ~VF_REQ_INT_REDIRECTED;

#endif
