/* $Id$ */
#ifndef VHTTP_HTTP_H
#define VHTTP_HTTP_H

#define HTTPCN_CLOSED  0
#define HTTPCN_ALIVE   1
#define HTTPCN_DEAD    3

#include <sys/types.h>
#include <errno.h>

const char *vhttp_errmsg(void);
void vhttp_set_err(int err_no, const char *fmt, ...);

int vhttp_misc_base64(char *b64, int size, const char *bin);

extern void (*http_progress_fn)(long total, long amount, void *data);

struct http_resp;

struct httpcn {
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
    
    struct http_resp *resp; /* last response  */
};

struct httpcn *httpcn_new(const char *host, int port,
                          const char *login, const char *passwd,
                          const char *proxy_login, const char *proxy_passwd);

void httpcn_free(struct httpcn *cn);

int httpcn_is_alive(struct httpcn *cn);

int httpcn_retr(struct httpcn *cn,
                int out_fd, off_t out_fdoff,
                const char *path, void *progess_data, 
                char *redirect_to, int size);

const char *http_errmsg(void);

#endif
