/* 
  Copyright (C) 2002 Pawel A. Gajda <mis@k2.net.pl>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* $Id$ */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#ifndef IPPORT_HTTP
# define IPPORT_HTTP 80
#endif

#define TIMEOUT     40

#include <trurl/nbuf.h>
#include <trurl/nassert.h>
#include <trurl/nhash.h>
#include <trurl/n_snprintf.h>
#include <trurl/nstr.h>
#include <trurl/nmalloc.h>

#include "http.h"
#include "i18n.h"

#ifndef VERSION
# define VERSION "3.1415926535897931"
#endif

#define HTTP_UA     "poldek-vhttp/" VERSION

/* HTTP/1.0 status codes from RFC1945, provided for reference.  */
/* Successful 2xx.  */
#define HTTP_STATUS_OK			200
#define HTTP_STATUS_CREATED		201
#define HTTP_STATUS_ACCEPTED		202
#define HTTP_STATUS_NO_CONTENT		204
#define HTTP_STATUS_PARTIAL_CONTENT	206

/* Redirection 3xx.  */
#define HTTP_STATUS_MULTIPLE_CHOICES	300
#define HTTP_STATUS_MOVED_PERMANENTLY	301
#define HTTP_STATUS_MOVED_TEMPORARILY	302
#define HTTP_STATUS_NOT_MODIFIED	304

#define HTTP_STATUS_IS_REDIR(code) (code == HTTP_STATUS_MOVED_TEMPORARILY || \
                                    code == HTTP_STATUS_MOVED_PERMANENTLY)

/* Client error 4xx.  */
#define HTTP_STATUS_BAD_REQUEST		400
#define HTTP_STATUS_UNAUTHORIZED	401
#define HTTP_STATUS_FORBIDDEN		403
#define HTTP_STATUS_NOT_FOUND		404
#define HTTP_STATUS_BAD_RANGE           416 

#define HTTP_STATUS_IS_CLIENT_ERROR(code) (code >= 400 && code % 400 < 100)

/* Server errors 5xx.  */
#define HTTP_STATUS_INTERNAL		500
#define HTTP_STATUS_NOT_IMPLEMENTED	501
#define HTTP_STATUS_BAD_GATEWAY		502
#define HTTP_STATUS_UNAVAILABLE		503

#define HTTP_STATUS_IS_SERVER_ERROR(code) (code >= 500 && code % 500 < 100)

#ifndef HAVE_ISBLANK
# define isblank(c) ((c) == ' ' || (c) == '\t')
#endif
#undef is_endl
#define is_endl(c) ((c) == '\n' || (c) == '\r')

static int verbose = 0;
static char errmsg[512] = { '\0' };

int vhttp_errno = 0;
int *vhttp_verbose = &verbose;


extern void (*vhttp_msg_fn)(const char *fmt, ...);
void   (*http_progress_fn)(long total, long amount, void *data) = NULL;


static volatile sig_atomic_t interrupted = 0;


#define HTTP_SUPPORTS_SIZE  (1 << 0)

#define ST_RESP_TIMEOUT         -2
#define ST_RESP_BAD             -1
#define ST_RESP_EMPTY            0
#define ST_RESP_NEWL             1
#define ST_RESP_PROTO            2 /* HTTP/1.x */
#define ST_RESP_PROTO_SPACE      3 /* blanks after HTTP/1.x  */
#define ST_RESP_STATUS_CODE      4 /* status code */
#define ST_RESP_STATUS_MSG       5 /* reason */
#define ST_RESP_LINE             6 
#define ST_RESP_EOR              10 

struct http_resp {
    tn_buf   *buf;
    int      last_i;
    int      state;

    int      code;
    int      code_len;
    char     *msg;              /* status message */
    tn_hash  *hdr;
    int      http_ver;
};


const char *vhttp_errmsg(void) 
{
    if (*errmsg == '\0')
        return NULL;
    return errmsg;
}

void vhttp_set_err(int err_no, const char *fmt, ...)
{
    va_list args;
    
    va_start(args, fmt);
    vsnprintf(errmsg, sizeof(errmsg), fmt, args);
    va_end(args);
    
    vhttp_errno = err_no;
}

static char *make_req_line(char *buf, int size, char *fmt, ...) 
{
    va_list  args;
    int n = 0;
    
    
    va_start(args, fmt);
    n = n_vsnprintf(buf, size, fmt, args);
    va_end(args);
    
    n += n_snprintf(&buf[n], size - n, " HTTP/1.1\r\n");
    
    return buf;
}


static
int httpcn_req(struct httpcn *cn, const char *req_line, char *fmt, ...) 
{
    char     req[4096];
    va_list  args;
    int      rc = 1, n = 0, nn = 0;
    
    if (cn->state != HTTPCN_ALIVE)
        return 0;

    n = 0;
    n += n_snprintf(&req[n], sizeof(req) - n, "%s", req_line);
    if (*vhttp_verbose > 1)
        vhttp_msg_fn("< %s", req);
    
    nn = n_snprintf(&req[n], sizeof(req) - n, "Host: %s\r\n", cn->host);
    if (*vhttp_verbose > 1)
        vhttp_msg_fn("<   %s", &req[n]);
    n += nn;

#if 0                           /* NFY */
    if (cn->login && cn->passwd) {
        
        nn = n_snprintf(&req[n], sizeof(req) - n,
                        "Authorization: Basic %s:", cn->login);
        if (*vhttp_verbose > 1)
            vhttp_msg_fn("<   %sxxx", &req[n]);
        n += nn;
#endif
        
    nn = n_snprintf(&req[n], sizeof(req) - n, "User-Agent: %s\r\n", HTTP_UA);
    if (*vhttp_verbose > 1)
        vhttp_msg_fn("<   %s", &req[n]);
    n += nn;
    
    nn = n_snprintf(&req[n], sizeof(req) - n, "Pragma: no-cache\r\n");
    if (*vhttp_verbose > 1)
        vhttp_msg_fn("<   %s", &req[n]);
    n += nn;
    
    nn = n_snprintf(&req[n], sizeof(req) - n, "Cache-Control: no-cache\r\n");
    if (*vhttp_verbose > 1)
        vhttp_msg_fn("<   %s", &req[n]);
    n += nn;
    
    if (fmt) {
        va_start(args, fmt);
        nn = n_vsnprintf(&req[n], sizeof(req) - n, fmt, args);
        va_end(args);
        
        if (*vhttp_verbose > 1)
            vhttp_msg_fn("<   %s", &req[n]);
        n += nn;
    }
    
    n += n_snprintf(&req[n], sizeof(req) - n, "\r\n");
    
    if (write(cn->sockfd, req, n) != n) {
        vhttp_set_err(errno, _("write to socket %s: %m"), req);
        cn->state = HTTPCN_DEAD;
        rc = 0;
    }
    
    return rc;
}


static struct http_resp *http_resp_new(void) 
{
    struct http_resp *resp;

    resp = n_malloc(sizeof(*resp));
    resp->buf = n_buf_new(2048);
    resp->last_i = 0;
    resp->state = ST_RESP_EMPTY;
    resp->code = 0;
    resp->code_len = 0;
    resp->hdr = n_hash_new(23, free);
    resp->msg = NULL;
    resp->http_ver = 1;
    return resp;
}


static void http_resp_free(struct http_resp *resp) 
{
    if (resp->buf)
        n_buf_free(resp->buf);

    if (resp->hdr)
        n_hash_free(resp->hdr);

    if (resp->msg)
        free(resp->msg);
    
    free(resp);
}

const char *http_resp_get_hdr(struct http_resp *resp, const char *name)
{
    return n_hash_get(resp->hdr, name);
}

static
int http_resp_get_hdr_long(struct http_resp *resp, const char *name, long *val)
{
    const char *s;
    int rc = 0;

    if ((s = n_hash_get(resp->hdr, name)))
        rc = (sscanf(s, "%ld", val) == 1);
    return rc;
}

static
int http_resp_conn_status(struct http_resp *resp)
{
    char *s;
    if ((s = n_hash_get(resp->hdr, "connection")) == NULL)
        return -1;

    if (strcmp(s, "keep-alive") == 0)
        return 1;

    return 0;
}

static
int http_resp_get_range(struct http_resp *resp,
                             long *from, long *to, long *total)
{
    const char *s, *p;
    int rc = 0;

    if ((s = n_hash_get(resp->hdr, "content-range")) == NULL)
        return 0;
    
    if ((p = strstr(s, "bytes")) == NULL)
        return 0;
    
    p += 5;
    while (isspace(*p))
        p++;

    if (resp->code != HTTP_STATUS_BAD_RANGE)
        rc = (sscanf(p, "%ld-%ld/%ld", from, to, total) == 3);
    else 
        rc = (sscanf(p, "*/%ld", total) == 1);
    
    return rc;
}

static
int response_complete(struct http_resp *resp)
{
    const char *buf;
    char c;

    
    buf = n_buf_ptr(resp->buf);
    buf += resp->last_i;
//    printf("last_i = %d + %d\n", resp->last_i, n_buf_size(resp->buf));
//    printf("buf %s\n", buf);
    while (*buf) {
	c = *buf++;
        //printf("c(%c) = %s\n", c, &c);
        resp->last_i++;
        
	switch (resp->state) {
            case ST_RESP_BAD:
                goto l_end;
                break;

            case ST_RESP_EOR:
                goto l_end;
                break;
                
	    case ST_RESP_EMPTY:
                resp->state = ST_RESP_PROTO;
                break;

            case ST_RESP_PROTO:
                if (is_endl(c)) {
                    resp->state = ST_RESP_BAD;
                    
                } else if (isblank(c)) {
                    resp->state = ST_RESP_PROTO_SPACE;
                }
                break;

            case ST_RESP_PROTO_SPACE:
                if (isdigit(c)) {
                    resp->code_len = 1;
                    resp->state = ST_RESP_STATUS_CODE;
                    
                } else if (!isblank(c)) {
//                    printf("BAD %c\n", c);
                    resp->state = ST_RESP_BAD;
                }
                break;
                
            case ST_RESP_STATUS_CODE:
                if (isdigit(c)) {
                    resp->code_len++;
                    
                } else if (isblank(c)) {
                    if (resp->code_len != 3 ||
                        sscanf(buf - 4, "%d", &resp->code) != 1) {
                        resp->state = ST_RESP_BAD;
                        
                    } else {
                        resp->state = ST_RESP_STATUS_MSG;
                    }
                    
                } else {        /* no digit && no space */
                    resp->code_len = 0;
                    resp->state = ST_RESP_BAD;
                }
                break;

            case ST_RESP_STATUS_MSG:
//                printf("st = ST_RESP_FIRST_LINE\n");
                if (c == '\n')
                    resp->state = ST_RESP_NEWL;
                break;
                
            case ST_RESP_NEWL:
//                printf("st = ST_RESP_NEWL [%c] %d\n", c, c);
                if (c == '\n') 
                    resp->state = ST_RESP_EOR;
                else if (c != '\r') 
                    resp->state = ST_RESP_LINE;
                break;
                
                
	    case ST_RESP_LINE:
//                printf("st = ST_RESP_LINE\n");
                if (c == '\n')
                    resp->state = ST_RESP_NEWL;
                
                break;

            default:
                resp->state = ST_RESP_BAD;
                break;
        }
    }

 l_end:
    
    switch (resp->state) {
        case ST_RESP_EOR:
            return 1;
            
        case ST_RESP_BAD:
            vhttp_set_err(EIO, _("%s: response parse error"),
                          (char*)n_buf_ptr(resp->buf));
            return -1;
            
        default:
            return 0;
    }
    
    return 0;
}


static int readresp(int sockfd, struct http_resp *resp, int readln) 
{
    int is_err = 0, buf_pos = 0;
    char buf[4096];

    vhttp_errno = 0;

    n_assert(readln);            /* todo: support for chunk-read */

    
    while (1) {
        struct timeval to = { TIMEOUT, 0 };
        fd_set fdset;
        int rc;
        
        FD_ZERO(&fdset);
        FD_SET(sockfd, &fdset);
        errno = 0;
        if ((rc = select(sockfd + 1, &fdset, NULL, NULL, &to)) < 0) {
            if (interrupted) {
                is_err = 1;
                errno = EINTR;
                break;
            }

            if (errno == EINTR)
                continue;
            
            is_err = 1;
            break;
            
        } else if (rc == 0) {
            errno = ETIMEDOUT;
            is_err = 1;
            break;
            
        } else if (rc > 0) {
            char c;
            int n;

            if (readln) 
                n = read(sockfd, &c, 1);
            else 
                n = read(sockfd, buf, sizeof(buf));

            if (n < 0 && errno == EINTR)
                continue;
            
            else if (n <= 0) {
                is_err = 1;
                if (n == 0 || errno == 0)
                    errno = ECONNRESET;
                break;
                
            } else if (n >= 1) {
                if (!readln) {
                    n_buf_addz(resp->buf, buf, n);
                    break;
                    
                } else {
                    buf[buf_pos++] = c;
                
                    if (buf_pos == sizeof(buf)) {
                        is_err = 1;
                        vhttp_errno = EMSGSIZE;
                        break;
                    }

                    if (c == '\n') {
                        n_buf_addz(resp->buf, buf, buf_pos);
                        break;
                    }
                }
            } else {
                n_assert(0);
            }
        }
    }
    
    if (is_err) {
        if (errno)
            vhttp_errno = errno;
        else
            vhttp_errno = EIO;
        
        switch (vhttp_errno) {
            case EMSGSIZE:
                vhttp_set_err(vhttp_errno, _("response line too long"));
                break;
                
            case ETIMEDOUT:
            case ECONNRESET:
                vhttp_set_err(vhttp_errno, "%m");
                break;
                
            case EINTR:
                if (interrupted) {
                    vhttp_set_err(vhttp_errno, _("connection canceled"));
                    break;
                }
                
            default:
                vhttp_set_err(vhttp_errno, "%s: %m", _("unexpected EOF"));
                
        }
        
    }
    
    return is_err ? 0 : 1;
}


static int http_resp_parse(struct http_resp *resp) 
{
    const char **tl, **tl_save, **status_tl, **status_tl_save;
    int is_err = 0, http_ver, i;

    n_hash_clean(resp->hdr);

    tl = tl_save = n_str_tokl(n_buf_ptr(resp->buf), "\r\n");

    if (*vhttp_verbose > 1)
        vhttp_msg_fn("> %s\n", *tl);

    status_tl = status_tl_save = n_str_tokl(*tl, " \r\n\t");

    i = 0;
    while (is_err == 0 && *status_tl) {
        if (i == 0) {
            if (sscanf(*status_tl, "HTTP/1.%d", &http_ver) != 1 ||
                http_ver > 1 || http_ver < 0)
                is_err = 1;
            else
                resp->http_ver = http_ver;
        }

        if (i == 2) {
            const char *p = strstr(*tl, *status_tl);
            if (p == NULL)      /* should not happen */
                p = *status_tl;
            
            resp->msg = n_strdup(p);
            break;
        }
        i++;
        status_tl++;
    }
    
    n_str_tokl_free(status_tl_save);
    
    tl++;                       /* skip status line */
    
    while (is_err == 0 && *tl) {
        char *p, *q, *nam, *val;
        
        if (*tl && **tl == '\0') {
            tl++;
            continue;
        }

        if (*vhttp_verbose > 1)
            vhttp_msg_fn(">   %s\n", *tl);

        if ((p = strchr(*tl, ':')) == NULL) {
            is_err = 1;
            break;
        }

        *p++ = '\0';
        
        nam = q = (char*)*tl;
        while (*q) {
            *q = tolower(*q);
            q++;
        }

        while (*p && isspace(*p))
            p++;
        
        if (*p == '\0') {
            is_err = 1;
            break;
        }
        
        val = q = p;
        while (*q) {
            *q = tolower(*q);
            q++;
        }
        
        //printf("add %s -> %s\n", nam, val);
        if (!n_hash_exists(resp->hdr, nam))
            n_hash_insert(resp->hdr, nam, n_strdup(val));
        tl++;
    }
    
    n_str_tokl_free(tl_save);
    
    return is_err == 0;
}


static
struct http_resp *do_http_read_resp(int sock)
{
    struct http_resp *resp;
    int is_err = 0;

    resp = http_resp_new();

    while (1) {
        int n;
        
        if ((n = readresp(sock, resp, 1)) > 0) {
            if (response_complete(resp))
                break;
        } else {
            is_err = 1;
            break;
        }
    }
    
    if (is_err == 0) {
        if (!http_resp_parse(resp)) {
            vhttp_set_err(EIO, _("%s: response parse error"),
                          (char*)n_buf_ptr(resp->buf));
            is_err = 1;
        }
    }
    
    if (is_err && resp) {
        http_resp_free(resp);
        resp = NULL;
    }
    
    return resp;
}

static int status_code_ok(int status_code, const char *msg, const char *path)
{
    int is_err = 1;

    if (path == NULL)
        path = "?";
    
    switch (status_code) {
        case HTTP_STATUS_OK:
        case HTTP_STATUS_PARTIAL_CONTENT:
            is_err = 0;
            break;
            
        case HTTP_STATUS_NOT_FOUND: 
            vhttp_set_err(ENOENT, _("%s: no such file"), path);
            break;
            
        case HTTP_STATUS_FORBIDDEN:
            vhttp_set_err(EPERM, _("%s: permission denied"), path);
            break;

        case HTTP_STATUS_BAD_RANGE:
            vhttp_set_err(EINVAL, _("%s: invalid range requested"), path);
            break;

        case HTTP_STATUS_MOVED_TEMPORARILY:
        case HTTP_STATUS_MOVED_PERMANENTLY:
            break;

        default:
            if (errno == 0)
                errno = EINVAL;
            vhttp_set_err(EINVAL, "%s: %m (%s)", path, msg);
            break;
    }

    return is_err == 0;
}

int httpcn_get_resp(struct httpcn *cn) 
{
    int rc = 1;

    if (cn->state != HTTPCN_ALIVE)
        return 0;

    if ((cn->resp = do_http_read_resp(cn->sockfd)) == NULL) {
        cn->state = HTTPCN_DEAD;
        rc = 0;
    }
    
    return rc;
}

static void vf_sigint_handler(int sig) 
{
    interrupted = 1;
    signal(sig, vf_sigint_handler);
}

static void *establish_sigint(void)
{
    void *vf_sigint_fn;

    interrupted = 0;
    vf_sigint_fn = signal(SIGINT, SIG_IGN);

    //printf("vf_sigint_fn %p, %d\n", vf_sigint_fn, *vhttp_verbose);
    if (vf_sigint_fn == NULL)      /* disable transfer interrupt */
        signal(SIGINT, SIG_DFL);
    else 
        signal(SIGINT, vf_sigint_handler);
    
    return vf_sigint_fn;
}

static void restore_sigint(void *vf_sigint_fn)
{
    signal(SIGINT, vf_sigint_fn);
}

static void sigalarmfunc(int unused)
{
    unused = unused;
    //printf("receive alarm");
}


static void install_alarm(int sec)
{
    struct sigaction act;
    
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


static int to_connect(const char *host, const char *service)
{
    struct addrinfo hints, *res, *resp;
    int sockfd, n;
    
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (*vhttp_verbose > 1)
        vhttp_msg_fn("Connecting to %s:%s...\n", host, service);

    if ((n = getaddrinfo(host, service, &hints, &res)) != 0) {
        vhttp_set_err(errno, _("unable to connect to %s:%s: %s"),
                     host, service, gai_strerror(n));
        return -1;
    }
    resp = res;

    vhttp_errno = 0;
    
    do {
        interrupted = 0;
        
        sockfd = socket(resp->ai_family, resp->ai_socktype, resp->ai_protocol);
        if (sockfd < 0)
            continue;
        install_alarm(TIMEOUT);

        if (connect(sockfd, resp->ai_addr, resp->ai_addrlen) == 0)
            break;
        
        if (errno == EINTR && interrupted == 0)
            vhttp_errno = errno = ETIMEDOUT;
        
        uninstall_alarm();
        close(sockfd);
        sockfd = -1;
        
    } while ((resp = resp->ai_next) != NULL);

    if (sockfd == -1)
        vhttp_set_err(errno, _("unable to connect to %s:%s: %m"), host, service);
    
    freeaddrinfo(res);
    return sockfd;
}


static int http_open(const char *host, int port)
{
    int sockfd;
    char portstr[64] = "http";

    errno = 0;
    snprintf(portstr, sizeof(portstr), "%d", port);
    if ((sockfd = to_connect(host, portstr)) < 0)
        return 0;

    
    return sockfd;
}


struct httpcn *httpcn_malloc(void) 
{
    struct httpcn *cn;

    cn = n_malloc(sizeof(*cn));
    memset(cn, 0,  sizeof(*cn));
    cn->host = cn->login = cn->passwd = NULL;
    cn->resp = NULL;
    cn->flags |= HTTP_SUPPORTS_SIZE;
    return cn;
}


struct httpcn *httpcn_new(const char *host, int port,
                          const char *login, const char *pwd)
{
    struct httpcn *cn = NULL;
    int sockfd;

    if (port <= 0)
        port = IPPORT_HTTP;

    if ((sockfd = http_open(host, port)) > 0) {
        cn = httpcn_malloc();
        cn->sockfd = sockfd;
        cn->state = HTTPCN_ALIVE;
        cn->host = n_strdup(host);
        cn->port = port;
        if (login && pwd) {
            cn->login = n_strdup(login);
            cn->passwd = n_strdup(pwd);
        }
    }

    return cn;
}


static void httpcn_close(struct httpcn *cn)
{
    
    if (cn->state == HTTPCN_CLOSED)
        return;
    
    cn->state = HTTPCN_CLOSED;
    close(cn->sockfd);
    cn->sockfd = -1;
}


void httpcn_free(struct httpcn *cn) 
{
    httpcn_close(cn);
    if (cn->resp)
        http_resp_free(cn->resp);
    memset(cn, 0, sizeof(*cn));
}

#if 0                           /* unused */
static long httpcn_size(struct httpcn *cn, const char *path) 
{ 
    long  size = -1;
    char req_line[PATH_MAX];
    
    
    if ((cn->flags & HTTP_SUPPORTS_SIZE) == 0)
        return -1;

    make_req_line(req_line, sizeof(req_line), "HEAD %s", path);
    if (!httpcn_req(cn, req_line, NULL))
        return -1;
    
    if (!httpcn_get_resp(cn))
        return -1;
    
    if (!status_code_ok(cn->resp->code, cn->resp->msg, path))
        return -1;
    
    if (!http_resp_get_hdr_long(cn->resp, "content-length", &size))
        size = -1;
    
    return size;
}
#endif

int httpcn_is_alive(struct httpcn *cn) 
{
    char req_line[256];

    if (cn->state != HTTPCN_ALIVE)
        return 0;
    
    make_req_line(req_line, sizeof(req_line), "HEAD /");
    
    if (!httpcn_req(cn, req_line, NULL))
        return 0;
    
    if (!httpcn_get_resp(cn)) {
        cn->state = HTTPCN_DEAD;
        return 0;
    }

    return 1;
}


static
int rcvfile(int out_fd, off_t out_fdoff, int in_fd, long total_size,
            void *progess_data) 
{
    struct  timeval tv;
    int     rc, is_err = 0;
    long    amount = 0;
    

    amount = out_fdoff;
    if (http_progress_fn) {
        http_progress_fn(total_size, 0, progess_data);
        if (amount) 
            http_progress_fn(total_size, amount, progess_data);
    }

    while (1) {
        fd_set fdset;

        if (total_size > 0 && amount == total_size)
            break;
        
	FD_ZERO(&fdset);
	FD_SET(in_fd, &fdset);

	tv.tv_sec = TIMEOUT;
	tv.tv_usec = 0;
        
        rc = select(in_fd + 1, &fdset, NULL, NULL, &tv);
        if (interrupted) {
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
                
                if ((nw = write(out_fd, buf, n)) != n) {
                    is_err = 1;
                    break;
                }
                amount += nw;
                if (http_progress_fn)
                    http_progress_fn(total_size, amount, progess_data);
                
            } else {
                is_err = 1;
                break;
            } 
        }
    }
    
    if (is_err) {
        vhttp_errno = errno;
        if (vhttp_errno == 0)
            vhttp_errno = errno = EIO;

        vhttp_set_err(errno, "%m");
    }

    if (http_progress_fn)
        http_progress_fn(total_size, -1, progess_data);
    
    return is_err == 0;
}

#if 0
static void progress(long total, long amount, void *data)
{
    printf("P %ld of total %ld\n", amount, total);
}
#endif

int httpcn_retr(struct httpcn *cn,
                int out_fd, off_t out_fdoff,
                const char *path, void *progess_data, 
                char *redirect_to, int size) 
{
    int   close_cn = 0, rc = 1;
    long  from = 0, to = 0, total = 0, amount = 0;
    void  *vf_sigint_fn;
    char  req_line[PATH_MAX];
    const char *trenc;
    
    vhttp_errno = 0;
    
    //http_progress_fn = progress;

    
    vf_sigint_fn = establish_sigint();
    if (redirect_to)
        *redirect_to = '\0';
    
    if ((lseek(out_fd, out_fdoff, SEEK_SET)) == (off_t)-1) {
        vhttp_set_err(errno, "%s: lseek %ld: %m", path, out_fdoff);
        goto l_err_end;
    }

    //if ((total_size = httpcn_size(cn, path)) < 0)
    //    goto l_err_end;
    
    if (out_fdoff < 0)
        out_fdoff = 0;

    make_req_line(req_line, sizeof(req_line), "GET %s", path);

    if (out_fdoff > 0) 
        httpcn_req(cn, req_line, "Range: bytes=%ld-\r\n", out_fdoff);
    else 
        httpcn_req(cn, req_line, NULL);

    
    if (!httpcn_get_resp(cn))
        goto l_err_end;

    close_cn = 0;
    switch (http_resp_conn_status(cn->resp)) {
        case -1:                /* no Connection header */
            if (cn->resp->http_ver > 0) /* HTTP > 1.0 */
                break;
                                 /* no break */
        case 0:                  /* Connection: close    */
            close_cn = 1;
            break;

        case 1:                  /* Connection: keep-alive  */
            close_cn = 0;
            break;

        default:
            n_assert(0);
            break;
    }

    /* poor HTTP client doesn't supports Trasfer-Encodings */
    if ((trenc = http_resp_get_hdr(cn->resp, "transfer-encoding"))) {
        if (*vhttp_verbose > 1)
            vhttp_msg_fn("Closing connection cause unimplemented HTTP "
                         "transfer encodins\n");
        close_cn = 1;
    }
    

    if (HTTP_STATUS_IS_REDIR(cn->resp->code)) {
        const char *redirto = http_resp_get_hdr(cn->resp, "location");
        if (redirto && redirect_to && strlen(redirto)) {
            snprintf(redirect_to, size, redirto);
            rc = 0;             /* treat redirects as errors, caller should
                                   check redirect_to */
            goto l_end;
        }
    }

    if (!status_code_ok(cn->resp->code, cn->resp->msg, path) &&
        cn->resp->code != HTTP_STATUS_BAD_RANGE)
        goto l_err_end;
    

    if (!http_resp_get_hdr_long(cn->resp, "content-length", &amount)) {
        vhttp_set_err(EINVAL, _("%s: Content-Length parse error (%s)"),
                      path, http_resp_get_hdr(cn->resp, "content-length"));
        goto l_err_end;
    }
    
    if (out_fdoff == 0)
        total = amount;
    
    else {
        if (!http_resp_get_range(cn->resp, &from, &to, &total)) {
            vhttp_set_err(EINVAL, _("%s: Content-Range parse error (%s)"),
                          path, http_resp_get_hdr(cn->resp, "content-range"));
            goto l_err_end;
        }

        if (cn->resp->code == HTTP_STATUS_BAD_RANGE) {
            if (out_fdoff != total) {
                goto l_err_end;
                
            } else {
                if (*vhttp_verbose > 1)
                    vhttp_msg_fn(_("%s: already downloaded\n"), path);
                goto l_end;         /* downloaded */
            } 
        }
        
        if (from != out_fdoff) {
            vhttp_set_err(EINVAL, _("%s: invalid Content-Range reached"), path);
            goto l_err_end;
        }
    }
    
    if (*vhttp_verbose > 1) {
        long a = from ? total - from : total;
        vhttp_msg_fn("Total file size %ld, %ld to download\n", total, a);
    }

    
    
    errno = 0;
    if (!rcvfile(out_fd, out_fdoff, cn->sockfd, total, progess_data))
        goto l_err_end;

    
 l_end:
    restore_sigint(vf_sigint_fn);
    if (close_cn)
        httpcn_close(cn);
    
    return rc;
    
 l_err_end:
    rc = 0;
    close_cn = 1;
    
    if (vhttp_errno == 0)
        vhttp_errno = EIO;

    goto l_end;

}

