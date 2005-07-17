#ifndef POLDEK_VFILE_VFREQ_H
#define POLDEK_VFILE_VFREQ_H

#include <time.h>

#define VF_PROGRESS_VIRGIN    0
#define VF_PROGRESS_DISABLED  1
#define VF_PROGRESS_RUNNING   2

struct vf_progress_bar {
    int     width;
    int     state;
    int     is_tty;
    int     prev_n;
    int     prev_perc;
    time_t     time_base;
    time_t     time_last;
    float      transfer_rate;
    float      eta; /* estimatet time of arrival */
    int        maxlen;
    int        freq;
};

void vf_progress_init(struct vf_progress_bar *bar);
void vf_progress(long total, long amount, void *data);

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
    
    //FILE      *stream;
    //long      stream_offset;
    
    struct vf_progress_bar *bar;

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
