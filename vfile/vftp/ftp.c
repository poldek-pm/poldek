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

#ifndef IPPORT_FTP
# define IPPORT_FTP 21
#endif

#include <trurl/nbuf.h>
#include <trurl/nassert.h>


#include "ftp.h"
#include "i18n.h"

#define CODE_BETWEEN(code, b1, b2) (code >= b1 && code <= b2)

static int verbose = 0;
static char errmsg[512] = { '\0' };


extern void (*vftp_msg_fn)(const char *fmt, ...);

void   (*ftp_progress_fn)(long total, long amount, void *data) = NULL;
int    *ftp_verbose = &verbose;

#define FTP_SUPPORTS_SIZE  (1 << 0)


#define ST_RESP_TIMEOUT    -2
#define ST_RESP_BAD        -1
#define ST_RESP_EMPTY       0
#define ST_RESP_NEWL        1
#define ST_RESP_CODE        2
#define ST_RESP_LASTLINE    3
#define ST_RESP_MARKLINE    4
#define ST_RESP_EOR         5  

struct ftp_response {
    tn_buf   *buf;
    int      last_i;
    int      state;

    int      code;
    int      code_len;
    char     *msg;
};


const char *ftp_errmsg(void) 
{
    if (*errmsg == '\0')
        return "Success";
    return errmsg;
}


static int do_ftp_cmd(int sock, char *fmt, va_list args)
{
    char     buf[1024], tmp_fmt[256];
    int      n;


    snprintf(tmp_fmt, sizeof(tmp_fmt), "%s\r\n", fmt);
    n = vsnprintf(buf, sizeof(buf), tmp_fmt, args);

    errno = 0;
    if (*ftp_verbose > 1)
        vftp_msg_fn("< %s", buf);
    
    if (write(sock, buf, n) != n) {
        if (errno == 0) 
            errno = EIO;
        snprintf(errmsg, sizeof(errmsg), _("write to socket %s: %m"), buf);
        return 0;
    }

    return 1;
}


static int ftpcn_cmd(struct ftpcn *cn, char *fmt, ...) 
{
    va_list  args;
    int rc;

    if (cn->state != FTPCN_ALIVE)
        return 0;

    va_start(args, fmt);
    rc = do_ftp_cmd(cn->sockfd, fmt, args);
    va_end(args);
    if (rc == 0)
        cn->state = FTPCN_DEAD;
        
    return rc;
}


void ftp_response_init(struct ftp_response *resp) 
{
    resp->buf = n_buf_new(2048);
    resp->last_i = 0;
    resp->state = ST_RESP_EMPTY;
    resp->code = 0;
    resp->msg = NULL;
    resp->code_len = 0;
}


void ftp_response_destroy(struct ftp_response *resp) 
{
    n_buf_free(resp->buf);
    memset(resp, 0, sizeof(*resp));
}


static
int response_complete(struct ftp_response *resp)
{
    const char *buf;
    char c;

    
    buf = n_buf_ptr(resp->buf);
    buf += resp->last_i;
    //printf("last_i = %d + %d\n", resp->last_i, n_buf_size(resp->buf));
    while (*buf) {
	c = *buf++;
        resp->last_i++;
        
	switch (resp->state) {
            case ST_RESP_BAD:
                return -1;
                break;

            case ST_RESP_EOR:
                return 1;
                break;
                
            case ST_RESP_NEWL:
	    case ST_RESP_EMPTY:
                if (isdigit(c)) {
                    resp->code_len++;
                    resp->state = ST_RESP_CODE;
                    
                } else {
                    snprintf(errmsg, sizeof(errmsg),
                             _("%s: response parse error at %s"),
                             (char*)n_buf_ptr(resp->buf), &c);
                    resp->state = ST_RESP_BAD;
                }
                break;

            case ST_RESP_CODE:
                if (isdigit(c)) {
                    resp->state = ST_RESP_CODE;
                    resp->code_len++;
                    
                } else if (c == ' ') {
                    if (resp->code_len != 3 ||
                        sscanf(buf - 4, "%d", &resp->code) != 1) {
                        resp->state = ST_RESP_BAD;
                        
                    } else {
                        resp->msg = (char*)buf;
                        resp->state = ST_RESP_LASTLINE;
                    }
                    
                } else {        /* no digit && no space */
                    resp->code_len = 0;
                    resp->state = ST_RESP_MARKLINE;
                }
                break;

	    case ST_RESP_MARKLINE:
                if (c == '\n')
                    resp->state = ST_RESP_NEWL;
                
                break;

            case ST_RESP_LASTLINE:
                if (c == '\n')
                    resp->state = ST_RESP_EOR; /* end of response */
                
                break;
                
            default:
                snprintf(errmsg, sizeof(errmsg),
                         _("%s: response parse error"),
                         (char*)n_buf_ptr(resp->buf));
        }
    }


    switch (resp->state) {
        case ST_RESP_EOR:
            return 1;
            
        case ST_RESP_BAD:
            snprintf(errmsg, sizeof(errmsg),
                     _("%s: response parse error"),
                     (char*)n_buf_ptr(resp->buf));
            return -1;
            
        default:
            return 0;
    }
    
    return 0;
}


static int readresp(int sockfd, struct ftp_response *resp, int readln) 
{
    int ttl, is_err = 0, buf_pos = 0;
    char buf[4096];
    
    errno = 0;
    ttl = 180;
    
    while (1) {
        struct timeval to = { 1, 0 };
        fd_set fdset;
        int rc;
        
        FD_ZERO(&fdset);
        FD_SET(sockfd, &fdset);
        if ((rc = select(sockfd + 1, &fdset, NULL, NULL, &to)) < 0) {
            if (errno == EAGAIN)
                continue;
            
            is_err = 1;
            break;
            
        } else if (rc == 0) {
            if (--ttl < 0) {
                is_err = 1;
                errno = ETIME;  /* I'm to lazy to define custom
                                   err categories */
                break;
            }
            
        } else if (rc > 0) {
            char c;
            int n;

            if (readln) 
                n = read(sockfd, &c, 1);
            else 
                n = read(sockfd, buf, sizeof(buf));
            
            if (n <= 0) {
                is_err = 1;
                break;
                
            } else if (n >= 1) {
                if (!readln) {
                    n_buf_addz(resp->buf, buf, n);
                    break;
                    
                } else {
                    buf[buf_pos++] = c;
                
                    if (buf_pos == sizeof(buf)) {
                        is_err = 1;
                        errno = EMSGSIZE;
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
        switch (errno) {
            case EMSGSIZE:
                snprintf(errmsg, sizeof(errmsg), _("response line too long"));
                break;
                
            case EINTR:
                snprintf(errmsg, sizeof(errmsg), _("unexpected EOF: %m"));
                break;
                
            case ETIME:
                snprintf(errmsg, sizeof(errmsg), "%s", 
                         _("timeout reached while waiting for server response"));
                break;
                
            default:
                snprintf(errmsg, sizeof(errmsg), "%s", _("unexpected EOF"));
        }
        
    }
    
    return is_err ? 0 : 1;
}

    


static int do_ftp_resp(int sock, int *resp_code, char **resp_msg, int readln)
{
    struct ftp_response resp;
    int is_err = 0;
    
    ftp_response_init(&resp);
    

    if (resp_msg)
        *resp_msg = NULL;

    if (resp_code)
        *resp_code = 0;
    
    while (1) {
        int n;
        
        if ((n = readresp(sock, &resp, readln)) > 0) {
            if (response_complete(&resp))
                break;
        } else {
            is_err = 1;
            break;
        }
    }

    
    if (is_err == 0) {
        int n = 0;
        
        n = strlen(resp.msg) - 1;
        if (n) {
            while (isspace(resp.msg[n]))
                resp.msg[n--] = '\0';
        }
        
        
        if (*ftp_verbose > 1)
            vftp_msg_fn("> %d %s\n", resp.code, resp.msg);
        
        if (resp_msg) 
            *resp_msg = strdup(resp.msg);
        
        if (resp_code) 
            *resp_code = resp.code;
    }

    ftp_response_destroy(&resp);
    return is_err == 0;
}


int ftpcn_resp_ext(struct ftpcn *cn, int readln) 
{
    int rc;

    if (cn->state != FTPCN_ALIVE)
        return 0;

    if (cn->last_respmsg) {
        free(cn->last_respmsg);
        cn->last_respmsg = NULL;
    }
    cn->last_respcode = 0;
    
    rc = do_ftp_resp(cn->sockfd, &cn->last_respcode, &cn->last_respmsg, readln);
    if (rc == 0)
        cn->state = FTPCN_DEAD;

    return rc;
}

#define ftpcn_resp(cn)           ftpcn_resp_ext(cn, 0)
#define ftpcn_resp_readln(cn)    ftpcn_resp_ext(cn, 1)

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


void uninstall_alarm(void)
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

    if ((n = getaddrinfo(host, service, &hints, &res)) != 0) {
        snprintf(errmsg, sizeof(errmsg),
                 _("unable to connect to %s:%s: %s"),
                 host, service, gai_strerror(n));
        return -1;
    }
    resp = res;

    
    do {
        sockfd = socket(resp->ai_family, resp->ai_socktype, resp->ai_protocol);
        if (sockfd < 0)
            continue;
        install_alarm(180);
        if (connect(sockfd, resp->ai_addr, resp->ai_addrlen) == 0)
            break; 
        uninstall_alarm();
        close(sockfd);
        sockfd = -1;
        
    } while ((resp = resp->ai_next) != NULL);

    if (sockfd == -1)
        snprintf(errmsg, sizeof(errmsg),
                 _("unable to connect to %s:%s: %m"), host, service);
    
    freeaddrinfo(res);
    return sockfd;
}


static int ftp_open(const char *host, int port)
{
    int sockfd, code;
    char portstr[64] = "ftp";

    snprintf(portstr, sizeof(portstr), "%d", port);
    if ((sockfd = to_connect(host, portstr)) < 0)
        return 0;

    if (!do_ftp_resp(sockfd, &code, NULL, 0) || code != 220) {
        close(sockfd);
        sockfd = -1;
    }
    
    return sockfd;
}


int ftp_login(struct ftpcn *cn, const char *login, const char *passwd)
{
    if (login && passwd) {
        cn->login = strdup(login);
        cn->passwd = strdup(passwd);
        
    } else {
	login = "anonymous";
        passwd = "ftp@";
    }
    
    
    if (!ftpcn_cmd(cn, "USER %s", login))
        goto l_err;
    

    if (!ftpcn_resp(cn)) 
        goto l_err;

    if (cn->last_respcode != 220 && !CODE_BETWEEN(cn->last_respcode, 300, 399))
        goto l_err;

    if (!ftpcn_cmd(cn, "PASS %s", passwd))
        goto l_err;

    if (!ftpcn_resp(cn) || cn->last_respcode > 500)
        goto l_err;
    
    if (!ftpcn_cmd(cn, "TYPE I"))
        goto l_err;
    
    if (!ftpcn_resp(cn) || cn->last_respcode != 200)
        goto l_err;
    
    return 1;
    
 l_err:
    snprintf(errmsg, sizeof(errmsg), _("login failed: %s"),
             cn->last_respmsg ? cn->last_respmsg : _("unknown error"));
    return 0;
}


struct ftpcn *ftpcn_malloc(void) 
{
    struct ftpcn *cn;

    cn = malloc(sizeof(*cn));
    memset(cn, 0,  sizeof(*cn));
    cn->host = cn->login = cn->passwd = cn->last_respmsg = NULL;
    cn->flags |= FTP_SUPPORTS_SIZE;
    return cn;
}


struct ftpcn *ftpcn_new(const char *host, int port,
                        const char *login, const char *pwd)
{
    struct ftpcn *cn = NULL;
    int sockfd;

    if (port <= 0)
        port = IPPORT_FTP;

    if (*ftp_verbose > 1)
        vftp_msg_fn(_("Connecting to %s:%d...\n"), host, port);
    
    if ((sockfd = ftp_open(host, port)) > 0) {
        cn = ftpcn_malloc();
        cn->sockfd = sockfd;
        cn->state = FTPCN_ALIVE;
        cn->host = strdup(host);
        cn->port = port;
        if (!ftp_login(cn, login, pwd)) {
            ftpcn_free(cn);
            cn = NULL;
        };
    }

    return cn;
}


static void ftpcn_close(struct ftpcn *cn)
{
    
    if (cn->state == FTPCN_CLOSED)
        return;
    
    if (cn->state == FTPCN_ALIVE) {
        if (ftpcn_cmd(cn, "QUIT"))
            ftpcn_resp(cn);
    }
    
    cn->state = FTPCN_CLOSED;
    close(cn->sockfd);
    cn->sockfd = -1;
}


void ftpcn_free(struct ftpcn *cn) 
{
    ftpcn_close(cn);
    if (cn->last_respmsg)
        free(cn->last_respmsg);
    memset(cn, 0, sizeof(*cn));
}


static int parse_pasv(const char *resp, char *addr, int addr_size, int *port)
{
    int p1, p2, a[4], is_err;
    const char *p;

    
    is_err = 0;
    
    p = resp;
    while (!isdigit(*p))
        p++;

    if (sscanf(p, "%d,%d,%d,%d,%d,%d", &a[0], &a[1], &a[2], &a[3],
               &p1, &p2) != 6) {
        
        snprintf(errmsg, sizeof(errmsg),
                 _("%s: PASV response parse error"), resp);
        is_err = 1;
    } else {
        int n;

        *port = p1*256+p2;

        n = snprintf(addr, addr_size, "%d.%d.%d.%d", a[0], a[1], a[2], a[3]);
        if (n == addr_size) {
            snprintf(errmsg, sizeof(errmsg), _("%s: address to long"), resp);
            is_err = 1;
        }
    }
    
    return is_err == 0;
}


static int ftpcn_pasv(struct ftpcn *cn) 
{
    char addr[256], *p;
    int port;
    
    
    if (!ftpcn_cmd(cn, "PASV"))
        return 0;
    
    if (!ftpcn_resp(cn) || cn->last_respcode != 227)
        return 0;
    
    if ((p = strchr(cn->last_respmsg, ' ')) == NULL)
        return 0;

    port = 0;
    *addr = '\0';
    if (parse_pasv(p, addr, sizeof(addr), &port)) {
        char service[64];
        snprintf(service, sizeof(service), "%d", port);
        return to_connect(addr, service);
    }
    
    return 0;
}


static long ftpcn_size(struct ftpcn *cn, const char *path) 
{ 
    long  size;

    if ((cn->flags & FTP_SUPPORTS_SIZE) == 0)
        return 0;

    if (!ftpcn_cmd(cn, "SIZE %s", path))
        return -1;
    
    if (!ftpcn_resp(cn))
        return -1;
    
    if (cn->last_respcode != 213) { /* SIZE not supported */
        cn->flags &= ~FTP_SUPPORTS_SIZE;
        return 0;
    }

    if (sscanf(cn->last_respmsg, "%ld", &size) != 1) {
        cn->flags &= ~FTP_SUPPORTS_SIZE;
        return 0;
    }

    return size;
}


int ftpcn_is_alive(struct ftpcn *cn) 
{
    if (!ftpcn_cmd(cn, "NOOP"))
        return 0;
    
    if (!ftpcn_resp(cn) || cn->last_respcode != 200) { /* serv must support NOOP */
        cn->state = FTPCN_DEAD;
        return 0;
    }

    return 1;
}


static
int rcvfile(int out_fd,  off_t out_fdoff, int in_fd, long total_size,
            void *progess_data) 
{
    struct  timeval tv;
    int     rc, is_err = 0;
    long    amount = 0;
    

    amount = out_fdoff;
    if (ftp_progress_fn) {
        ftp_progress_fn(total_size, 0, progess_data);
        if (amount) 
            ftp_progress_fn(total_size, amount, progess_data);
    }
    
    while (1) {
        fd_set fdset;
        
	FD_ZERO(&fdset);
	FD_SET(in_fd, &fdset);

	tv.tv_sec = 300;
	tv.tv_usec = 0;

        if ((rc = select(in_fd + 1, &fdset, NULL, NULL, &tv)) < 0) {
            if (errno == EAGAIN)
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
                if (ftp_progress_fn)
                    ftp_progress_fn(total_size, amount, progess_data);
                
            } else {
                errno = EIO;
                is_err = 1;
                break;
            } 
        }
    }
    
    if (is_err)
        if (errno == 0)
            errno = EIO;
    return is_err == 0;
}


static void progress(long total, long amount, void *data)
{
    printf("P %ld of total %ld\n", amount, total);
}


int ftpcn_retr(struct ftpcn *cn, int out_fd, off_t out_fdoff, 
               const char *path, void *progess_data)
{
    int   sockfd;
    long  total_size;
    int   l_errno = EIO;
    
    if ((lseek(out_fd, out_fdoff, SEEK_SET)) != 0) {
        snprintf(errmsg, sizeof(errmsg), "%s: lseek %ld: %m", path, out_fdoff);
        return 0;
    }

    if ((total_size = ftpcn_size(cn, path)) < 0)
        return 0;
    
    if ((sockfd = ftpcn_pasv(cn)) <= 0)
        return 0;

    if (out_fdoff < 0)
        out_fdoff = 0;
    
    if (!ftpcn_cmd(cn, "REST %ld", out_fdoff))
        return 0;
    
    if (!ftpcn_resp(cn))
        goto l_err;
    
    ftpcn_cmd(cn, "RETR %s", path);
    if (!ftpcn_resp_readln(cn))
        goto l_err;

    
    if (cn->last_respcode != 150) {
        snprintf(errmsg, sizeof(errmsg), _("%s: no such file (serv says: %s)"),
                 path, cn->last_respmsg);
        l_errno = ENOENT;
        goto l_err;
    }

    if (total_size == 0) {
        char *p;

        if ((p = strstr(cn->last_respmsg, " bytes")) && p != cn->last_respmsg) {
            p--;
            while (p != cn->last_respmsg && isdigit(*p)) 
                p--;
            p++;
            if (sscanf(p, "%ld ", &total_size) != 1)
                total_size = 0;
        }
    }
    
    
    if (!rcvfile(out_fd, out_fdoff, sockfd, total_size, progess_data)) {
        snprintf(errmsg, sizeof(errmsg), "download %s: %m", path);
        goto l_err;
    }
	
    
    close(sockfd);
    
    if (!ftpcn_resp(cn) || cn->last_respcode != 226)
        goto l_err;
    
    return 1;
    
 l_err:
    errno = l_errno;
    close(sockfd);
    return 0;
}

