/* $Id$ */
#ifndef VFTP_FTP_H
#define VFTP_FTP_H

#define FTPCN_CLOSED  0
#define FTPCN_ALIVE   1
#define FTPCN_DEAD    3

#include <sys/types.h>
#include <errno.h>

const char *vftp_errmsg(void);
void vftp_set_err(int err_no, const char *fmt, ...);
extern void (*ftp_progress_fn)(long total, long amount, void *data);

struct ftpcn {
    int       state;
    unsigned  flags;
    int       sockfd;
    char      *host;
    int       port;

    char      *login;
    char      *passwd;
    
    char      *last_respmsg;
    int       last_respcode;
};

struct ftpcn *ftpcn_new(const char *host, int port,
                        const char *login, const char *pwd);
void ftpcn_free(struct ftpcn *cn);

int ftpcn_is_alive(struct ftpcn *cn);

int ftpcn_retr(struct ftpcn *cn, int out_fd, off_t out_fdoff, const char *path,
               void *progess_data);

const char *ftp_errmsg(void);

#endif
