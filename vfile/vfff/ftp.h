/* $Id$ */
#ifndef VFTP_FTP_H
#define VFTP_FTP_H

#define VFTPCN_CLOSED  0
#define VFTPCN_ALIVE   1
#define VFTPCN_DEAD    3

#include <sys/types.h>
#include <errno.h>

#include <netdb.h>

const char *vftp_errmsg(void);
void vftp_set_err(int err_no, const char *fmt, ...);

struct vftpcn {
    int       state;
    unsigned  flags;
    int       sockfd;
    char      *host;
    int       port;

    struct    addrinfo addr;

    char      *login;
    char      *passwd;
    
    char      *last_respmsg;
    int       last_respcode;
};

struct vftpcn *vftpcn_new(const char *host, int port,
                        const char *login, const char *pwd);
void vftpcn_free(struct vftpcn *cn);

int vftpcn_is_alive(struct vftpcn *cn);


struct vftp_req {
    const  char  *uri;

    int          out_fd;
    off_t        out_fdoff;
    void         *progress_fn_data;

    off_t        st_size;
    time_t       st_mtime;
};

int vftpcn_retr(struct vftpcn *cn, struct vftp_req *req);
int vftpcn_stat(struct vftpcn *cn, struct vftp_req *req);

#endif
