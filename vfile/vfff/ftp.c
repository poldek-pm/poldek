/* 
  Copyright (C) 2002 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <time.h>

#include <trurl/nbuf.h>
#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
#include <trurl/n_snprintf.h>

#include "sigint/sigint.h"
#include "vfff.h"
#include "i18n.h"

#define CODE_BETWEEN(code, b1, b2) (code >= b1 && code <= b2)

#define ST_RESP_TIMEOUT    -2
#define ST_RESP_BAD        -1
#define ST_RESP_EMPTY       0
#define ST_RESP_NEWL        1
#define ST_RESP_CODE        2
#define ST_RESP_LASTLINE    3
#define ST_RESP_MARKLINE    4
#define ST_RESP_EOR         5  

struct ftp_resp {
    tn_buf   *buf;
    int      last_i;
    int      state;

    int      code;
    int      code_len;
    char     *msg;
};

#define resp_code(cn) ((struct ftp_resp *)(cn)->resp)->code
#define resp_msg(cn) ((struct ftp_resp *)(cn)->resp)->msg

#if 0
static inline int resp_code(struct vcn *cn)
{
    struct ftp_resp *resp;
    
    resp = cn->resp;
    return resp->code;
}
#endif

static int do_ftp_cmd(int sock, char *fmt, va_list args)
{
    char     buf[1024], tmp_fmt[256];
    int      n;

    if (vfff_sigint_reached())
        return 0;

    snprintf(tmp_fmt, sizeof(tmp_fmt), "%s\r\n", fmt);
    n = n_vsnprintf(buf, sizeof(buf), tmp_fmt, args);

    vfff_errno = 0;
    if (*vfff_verbose > 1)
        vfff_log("< %s", buf);
    
    if (write(sock, buf, n) != n) {
        vfff_set_err(errno, _("write to socket %s: %m"), buf);
        return 0;
    }

    return 1;
}


static int vftpcn_cmd(struct vcn *cn, char *fmt, ...) 
{
    va_list  args;
    int rc;

    if (cn->state != VCN_ALIVE)
        return 0;

    va_start(args, fmt);
    rc = do_ftp_cmd(cn->sockfd, fmt, args);
    va_end(args);
    if (rc == 0)
        cn->state = VCN_DEAD;
        
    return rc;
}


struct ftp_resp *ftp_resp_new(void) 
{
    struct ftp_resp *resp;
    
    resp = n_malloc(sizeof(*resp));
    resp->buf = n_buf_new(2048);
    resp->last_i = 0;
    resp->state = ST_RESP_EMPTY;
    resp->code = 0;
    resp->msg = NULL;
    resp->code_len = 0;
    return resp;
}


void ftp_resp_free(struct ftp_resp *resp) 
{
    n_buf_free(resp->buf);
    memset(resp, 0, sizeof(*resp));
    free(resp);
}


static
int response_complete(struct ftp_resp *resp)
{
    const char *buf;
    char c;

    
    buf = n_buf_ptr(resp->buf);
    buf += resp->last_i;
    //printf("last_i = %d + %d\n", resp->last_i, n_buf_size(resp->buf));
    while (*buf) {
        c = *buf++;
        //printf("c(%c) = %s\n", c, &c);
        resp->last_i++;
        
        switch (resp->state) {
            case ST_RESP_BAD:
                return -1;
                break;

            case ST_RESP_EOR:
                return 1;
                break;
                
            case ST_RESP_EMPTY:
                if (isdigit(c)) {
                    resp->code_len++;
                    resp->state = ST_RESP_CODE;
                    
                } else {
                    vfff_set_err(EIO, _("response parse error: %s"),
                                 (char*)n_buf_ptr(resp->buf));
                    resp->state = ST_RESP_BAD;
                }
                break;

            case ST_RESP_NEWL:
                if (isdigit(c)) {
                    resp->code_len++;
                    resp->state = ST_RESP_CODE;
                    
                } else if (isspace(c)) {
                    resp->code_len = 0;
                    resp->state = ST_RESP_MARKLINE;
                    
                } else {
                    vfff_set_err(EIO, _("response parse error: %s"),
                                 (char*)n_buf_ptr(resp->buf));
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
                vfff_set_err(EIO, _("%s: response parse error"),
                             (char*)n_buf_ptr(resp->buf));
        }
    }


    switch (resp->state) {
        case ST_RESP_EOR:
            return 1;
            
        case ST_RESP_BAD:
            vfff_set_err(EIO, _("%s: response parse error"),
                         (char*)n_buf_ptr(resp->buf));
            return -1;
            
        default:
            return 0;
    }
    
    return 0;
}


static int readresp(int sockfd, struct ftp_resp *resp, int readln) 
{
    int is_err = 0, buf_pos = 0, ttl;
    char buf[4096];

    vfff_errno = 0;
    ttl = VFFF_TIMEOUT;
    
    while (1) {
        struct timeval to = { 1, 0 };
        fd_set fdset;
        int rc;

        FD_ZERO(&fdset);
        FD_SET(sockfd, &fdset);
        errno = 0;
        if ((rc = select(sockfd + 1, &fdset, NULL, NULL, &to)) < 0) {
            if (vfff_sigint_reached()) {
                is_err = 1;
                errno = EINTR;
                break;
            }

            if (errno == EINTR)
                continue;
            
            is_err = 1;
            break;
            
        } else if (rc == 0 && ttl-- == 0) {
            errno = ETIMEDOUT;
            is_err = 1;
            break;
            
        } else if (rc > 0) {
            char c;
            int n;

            ttl = VFFF_TIMEOUT;
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
                        vfff_errno = EMSGSIZE;
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
            vfff_errno = errno;
        else
            vfff_errno = EIO;
        
        switch (vfff_errno) {
            case EMSGSIZE:
                vfff_set_err(vfff_errno, _("response line too long"));
                break;
                
            case ETIMEDOUT:
            case ECONNRESET:
                vfff_set_err(vfff_errno, "%m");
                break;
                
            case EINTR:
                if (vfff_sigint_reached()) 
                    break;
                
            default:
                vfff_set_err(vfff_errno, "%s: %m", _("unexpected EOF"));
                
        }
        
    }
    
    return is_err ? 0 : 1;
}

    
static int do_ftp_resp(int sock, struct ftp_resp *resp, int readln)
{
    int is_err = 0;

    if (vfff_sigint_reached())
        return 0;
    
    while (1) {
        int n;
        
        if ((n = readresp(sock, resp, readln)) > 0) {
            if (response_complete(resp))
                break;

            if (vfff_sigint_reached()) {
                is_err = 1;
                break;
            }
            
        } else {
            is_err = 1;
            break;
        }
    }

    
    if (is_err == 0) {
        if (resp->msg) {
            int n = 0;
            n = strlen(resp->msg) - 1;
            if (n) {
                while (isspace(resp->msg[n]))
                    resp->msg[n--] = '\0';
            }
            if (*vfff_verbose > 1)
                vfff_log("> %d %s\n", resp->code, resp->msg);
        }
    }

    return is_err == 0;
}


int vftpcn_resp_ext(struct vcn *cn, int readln) 
{
    int rc;
    
    if (cn->state != VCN_ALIVE)
        return 0;

    if (cn->resp)
        ftp_resp_free(cn->resp);
    
    cn->resp = ftp_resp_new();
    
    rc = do_ftp_resp(cn->sockfd, cn->resp, readln);
    if (rc == 0)
        cn->state = VCN_DEAD;

    return rc;
}

#define vftpcn_resp(cn)           vftpcn_resp_ext(cn, 0)
#define vftpcn_resp_readln(cn)    vftpcn_resp_ext(cn, 1)


static
int ftp_login(struct vcn *cn)
{
    int code;
    
    vfff_errno = 0;
    
    if (!vftpcn_cmd(cn, "USER %s", cn->login))
        goto l_err;

    
    if (!vftpcn_resp(cn)) 
        goto l_err;

    code = resp_code(cn);
    if (code != 230) { /* logged in without passwd */
        if (code != 220 && !CODE_BETWEEN(code, 300, 399))
            goto l_err;
        
        if (!vftpcn_cmd(cn, "PASS %s", cn->passwd))
            goto l_err;
    
        if (!vftpcn_resp(cn) || resp_code(cn) > 500)
            goto l_err;
    }
    
    if (!vftpcn_cmd(cn, "TYPE I"))
        goto l_err;
    
    if (!vftpcn_resp(cn) || resp_code(cn) != 200)
        goto l_err;
    
    return 1;
    
 l_err:
    if (vfff_errno == 0)
        vfff_set_err(EIO, _("login failed: %s"),
                     resp_msg(cn) ? resp_msg(cn) : _("unknown error"));
    else
        vfff_set_err(vfff_errno, "%m");
    
    return 0;
}

static int ftp_open(struct vcn *cn)
{
    if (!vftpcn_resp(cn) || resp_code(cn) != 220)
        return 0;

    if (!ftp_login(cn))
        return 0;

    return 1;
}

static void ftp_close(struct vcn *cn)
{
    if (cn->state == VCN_CLOSED)
        return;
    
    if (cn->state == VCN_ALIVE) {
        if (vftpcn_cmd(cn, "QUIT"))
            vftpcn_resp(cn);
    }
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
        vfff_set_err(EIO, _("%s: PASV response parse error"), resp);
        is_err = 1;
    } else {
        int n;

        *port = p1*256+p2;

        n = n_snprintf(addr, addr_size, "%d.%d.%d.%d", a[0], a[1], a[2], a[3]);
        if (n == addr_size) {
            vfff_set_err(EIO, _("%s: address too long"), resp);
            is_err = 1;
        }
    }
    
    return is_err == 0;
}

static int parse_epasv(const char *resp, int *port)
{
    const char *p;
    int is_err = 0;
    
    p = resp;
    if ((p = strchr(p, '(')) == NULL)
        is_err = 1;
        
    else {
        p++;
        if (sscanf(p, "|||%d|", port) != 1)
            is_err = 1;
    }

    if (is_err)
        vfff_set_err(EIO, _("%s: PASV response parse error"), resp);
    
    return is_err == 0;
}

static int vftpcn_pasv(struct vcn *cn) 
{
    char addrbuf[256], *addr, *p, *cmd = "PASV";
    int port, v6 = 0, req_code = 227, isok = 0;
    
    if (cn->afamily == AF_INET6) {
        cmd = "EPSV";
        v6 = 1;
        req_code = 229;
    }
    	
    if (!vftpcn_cmd(cn, cmd))
        return 0;
    
    if (!vftpcn_resp(cn) || resp_code(cn) != req_code) {
        vfff_set_err(EIO, resp_msg(cn));
        return 0;
    }

    if ((p = strchr(resp_msg(cn), ' ')) == NULL)
        return 0;

    port = 0;
    addrbuf[0] = '\0';
    
    if (v6 == 0) {
        isok = parse_pasv(p, addrbuf, sizeof(addrbuf), &port);
        addr = addrbuf;
        
    } else {
        isok = parse_epasv(p, &port);
        addr = cn->host;
    }
    
    if (isok) {
        char service[64];
        snprintf(service, sizeof(service), "%d", port);
        isok = vfff_to_connect(addr, service, NULL);
    }
    
    return isok;
}


static long vftpcn_size(struct vcn *cn, const char *path) 
{ 
    long  size;

    if ((cn->flags & VCN_SUPPORTS_SIZE) == 0)
        return 0;

    if (!vftpcn_cmd(cn, "SIZE %s", path))
        return -1;
    
    if (!vftpcn_resp(cn))
        return -1;
    
    if (resp_code(cn) != 213) { /* SIZE not supported */
        cn->flags &= ~VCN_SUPPORTS_SIZE;
        return 0;
    }

    if (sscanf(resp_msg(cn), "%ld", &size) != 1) {
        cn->flags &= ~VCN_SUPPORTS_SIZE;
        return 0;
    }

    return size;
}

static time_t vftpcn_mtime(struct vcn *cn, const char *path) 
{ 
    struct tm  tm;
    time_t     ts = -1;
    
    if ((cn->flags & VCN_SUPPORTS_MDTM) == 0)
        return 0;

    if (!vftpcn_cmd(cn, "MDTM %s", path))
        return -1;
    
    if (!vftpcn_resp(cn))
        return -1;
    
    if (resp_code(cn) != 213) { /* SIZE not supported */
        cn->flags &= ~VCN_SUPPORTS_MDTM;
        return 0;
    }

    //printf("msg = (%s), %d\n", resp_msg(cn),
    //       sscanf(resp_msg(cn), "%4d%2d%2d%2d%2d%2d",
    //              &tm.tm_year, &tm.tm_mon, &tm.tm_mday, 
    //              &tm.tm_hour, &tm.tm_min, &tm.tm_sec));
    if (sscanf(resp_msg(cn), "%4d%2d%2d%2d%2d%2d",
               &tm.tm_year, &tm.tm_mon, &tm.tm_mday, 
               &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) {
        
        cn->flags &= ~VCN_SUPPORTS_MDTM;
        return 0;
    }

    
    if (CODE_BETWEEN(tm.tm_year, 1900, 3000) &&
        CODE_BETWEEN(tm.tm_mon, 1, 12) &&
        CODE_BETWEEN(tm.tm_mday, 0, 31) &&
        CODE_BETWEEN(tm.tm_hour, 0, 23) &&
        CODE_BETWEEN(tm.tm_min, 0, 59) &&
        CODE_BETWEEN(tm.tm_sec, 0, 59)) {

        tm.tm_year -= 1900;
        tm.tm_mon -=  1;
        ts = mktime(&tm);
    }
    return ts;
}

static
int vftpcn_is_alive(struct vcn *cn) 
{
    if (!vftpcn_cmd(cn, "NOOP"))
        return 0;
    
    if (!vftpcn_resp(cn) || resp_code(cn) != 200) { /* serv must support NOOP */
        cn->state = VCN_DEAD;
        return 0;
    }

    return 1;
}

static
int vftpcn_retr(struct vcn *cn, struct vfff_req *req)
{
    int   sockfd = -1;
    long  total_size;
    

    vfff_errno = 0;
    
    if ((lseek(req->out_fd, req->out_fdoff, SEEK_SET)) == (off_t)-1) {
        vfff_set_err(errno, "%s: lseek %ld: %m", req->uri, req->out_fdoff);
        goto l_err;
    }

    if ((total_size = vftpcn_size(cn, req->uri)) < 0)
        goto l_err;

    if (vfff_sigint_reached())
        goto l_err;

    if ((req->st_remote_mtime = vftpcn_mtime(cn, req->uri)) < 0)
        goto l_err;

    if (vfff_sigint_reached())
        goto l_err;
    
    if ((sockfd = vftpcn_pasv(cn)) <= 0)
        goto l_err;

    if (vfff_sigint_reached())
        goto l_err;

    if (req->out_fdoff < 0)
        req->out_fdoff = 0;
    
    if (!vftpcn_cmd(cn, "REST %ld", req->out_fdoff))
        goto l_err;

    if (!vftpcn_resp(cn))
        goto l_err;

    vftpcn_cmd(cn, "RETR %s", req->uri);
    if (!vftpcn_resp_readln(cn))
        goto l_err;

    if (resp_code(cn) != 150) {
        vfff_set_err(ENOENT, _("%s: no such file (serv said: %s)"),
                     req->uri, resp_msg(cn));
        goto l_err;
    }

    if (total_size == 0) {
        char *p;

        if ((p = strstr(resp_msg(cn), " bytes")) && p != resp_msg(cn)) {
            p--;
            while (p != resp_msg(cn) && isdigit(*p)) 
                p--;
            p++;
            if (sscanf(p, "%ld ", &total_size) != 1)
                total_size = 0;
        }
    }

    req->st_remote_size = total_size;
    
    errno = 0;
    if (!vfff_transfer_file(req, sockfd, total_size))
        goto l_err;
    
    close(sockfd);
    
    if (!vftpcn_resp(cn) || resp_code(cn) != 226)
        goto l_err;

    return 1;
    
 l_err:
    if (vfff_errno == 0)
        vfff_errno = EIO;
    
    if (sockfd > 0)
        close(sockfd);
    return 0;
}


static
int vftpcn_stat(struct vcn *cn, struct vfff_req *req)
{
    vfff_errno = 0;

    req->st_remote_size = vftpcn_size(cn, req->uri);
    
    if (!vfff_sigint_reached())
        req->st_remote_mtime = vftpcn_mtime(cn, req->uri);
    
    if (req->st_remote_mtime < 0 && req->st_remote_size < 0)
        return 0;
    
    return 1;
}


void vftp_vcn_init(struct vcn *cn)
{
    cn->m_open = ftp_open;
    cn->m_close = ftp_close;
    cn->m_free = (void (*)(void*)) ftp_resp_free;
    cn->m_is_alive = vftpcn_is_alive;
    cn->m_retr = vftpcn_retr;
    cn->m_stat = vftpcn_stat;
}
