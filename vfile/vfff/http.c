/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <trurl/nbuf.h>
#include <trurl/nassert.h>
#include <trurl/nhash.h>
#include <trurl/n_snprintf.h>
#include <trurl/nstr.h>
#include <trurl/nmalloc.h>

#include <sigint/sigint.h>

#include "vfff.h"
#include "../vfile_intern.h" // for verbose level
#include "i18n.h"
#include "sigint/sigint.h"

extern char *vfff_uri_escape(const char *path);

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
#define HTTP_STATUS_NOT_MODIFIED	    304

#define HTTP_STATUS_IS_REDIR(code) (code == HTTP_STATUS_MOVED_TEMPORARILY || \
                                    code == HTTP_STATUS_MOVED_PERMANENTLY)

/* Client error 4xx.  */
#define HTTP_STATUS_BAD_REQUEST		400
#define HTTP_STATUS_UNAUTHORIZED	401
#define HTTP_STATUS_FORBIDDEN		403
#define HTTP_STATUS_NOT_FOUND		404
#define HTTP_STATUS_BAD_RANGE       416

#define HTTP_STATUS_IS_CLIENT_ERROR(code) (code >= 400 && code % 400 < 100)

/* Server errors 5xx.  */
#define HTTP_STATUS_INTERNAL		500
#define HTTP_STATUS_NOT_IMPLEMENTED	501
#define HTTP_STATUS_BAD_GATEWAY		502
#define HTTP_STATUS_UNAVAILABLE		503

#define HTTP_STATUS_IS_SERVER_ERROR(code) (code >= 500 && code % 500 < 100)

#if !defined(HAVE_ISBLANK) && !defined(isblank)
# define isblank(c) ((c) == ' ' || (c) == '\t')
#endif

#undef is_endl
#define is_endl(c) ((c) == '\n' || (c) == '\r')

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

extern int vhttp_misc_base64(char *b64, int size, const char *bin);

static
char *make_req_line(char *buf, int size, const char *method, const char *uri)
{
    n_snprintf(buf, size, "%s %s HTTP/1.1\r\n", method, uri);
    return buf;
}

static
int mk_auth(char *auth, int size, const char *login, const char *passwd)
{
    char buf[512];

    n_snprintf(buf, sizeof(buf), "%s:%s", login, passwd);
    return vhttp_misc_base64(auth, size, buf);
}

static
int httpcn_req(struct vcn *cn, const char *req_line, char *fmt, ...)
{
    char     req[4096];
    va_list  args;
    int      rc = 1, n = 0, nn = 0;

    if (cn->state != VCN_ALIVE)
        return 0;

    n = 0;
    n += n_snprintf(&req[n], sizeof(req) - n, "%s", req_line);
    if (*vfff_verbose > 1)
        vfff_log("< %s", req);


    if (cn->login && cn->passwd && cn->auth_basic_str == NULL) {
        char auth[512];

        mk_auth(auth, sizeof(auth), cn->login, cn->passwd);
        cn->auth_basic_str = n_strdup(auth);
    }

    if (cn->auth_basic_str)
        n += n_snprintf(&req[n], sizeof(req) - n,
                        "Authorization: Basic %s\r\n", cn->auth_basic_str);

    if (cn->proxy_login && cn->proxy_passwd &&
        cn->proxy_auth_basic_str == NULL) {
        char auth[512];

        mk_auth(auth, sizeof(auth), cn->proxy_login, cn->proxy_passwd);
        cn->proxy_auth_basic_str = n_strdup(auth);
    }

    if (cn->proxy_auth_basic_str)
        n += n_snprintf(&req[n], sizeof(req) - n,
                        "Proxy-Authorization: Basic %s\r\n",
                        cn->proxy_auth_basic_str);

    nn = n_snprintf(&req[n], sizeof(req) - n, "Host: %s\r\n", cn->host);
    if (*vfff_verbose > 1)
        vfff_log("<   %s", &req[n]);
    n += nn;

    nn = n_snprintf(&req[n], sizeof(req) - n, "User-Agent: %s\r\n", HTTP_UA);
    if (*vfff_verbose > 1)
        vfff_log("<   %s", &req[n]);
    n += nn;

#if 0
    nn = n_snprintf(&req[n], sizeof(req) - n, "Pragma: no-cache\r\n");
    if (*vfff_verbose > 1)
        vfff_log("<   %s", &req[n]);
    n += nn;

    nn = n_snprintf(&req[n], sizeof(req) - n, "Cache-Control: no-cache\r\n");
    if (*vfff_verbose > 1)
        vfff_log("<   %s", &req[n]);
    n += nn;
#endif

    if (fmt) {
        va_start(args, fmt);
        nn = n_vsnprintf(&req[n], sizeof(req) - n, fmt, args);
        va_end(args);

        if (*vfff_verbose > 1)
            vfff_log("<   %s", &req[n]);
        n += nn;
    }

    n += n_snprintf(&req[n], sizeof(req) - n, "\r\n");

    if (write(cn->sockfd, req, n) != n) {
        vfff_set_err(errno, _("write to socket %s: %m"), req);
        cn->state = VCN_DEAD;
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

static
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
    if ((s = n_hash_get(resp->hdr, "connection")) == NULL &&
        (s = n_hash_get(resp->hdr, "proxy-connection")) == NULL)
        return -1;

    if (strcasecmp(s, "keep-alive") == 0)
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
            vfff_set_err(EIO, _("%s: response parse error"),
                         (char*)n_buf_ptr(resp->buf));
            return -1;

        default:
            return 0;
    }

    return 0;
}


static int readresp(int sockfd, struct http_resp *resp, int readln)
{
    int is_err = 0, buf_pos = 0, ttl = VFFF_TIMEOUT;
    char buf[4096];

    vfff_errno = 0;

    n_assert(readln);            /* todo: support for chunk-read */


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
                /* fallthru */
            default:
                vfff_set_err(vfff_errno, "%s: %m", _("unexpected EOF"));

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

    if (*vfff_verbose > 1)
        vfff_log("> %s\n", *tl);

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

        if (*vfff_verbose > 1)
            vfff_log(">   %s\n", *tl);

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

        if (vfff_sigint_reached()) {
            is_err = 1;
            break;
        }

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
            vfff_set_err(EIO, _("%s: response parse error"),
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
            if (*vfile_conf.verbose > 0) // kill error if verbose = 0
                vfff_set_err(ENOENT, _("%s: no such file"), path);
            else
                is_err = 0;
            break;

        case HTTP_STATUS_FORBIDDEN:
            vfff_set_err(EPERM, _("%s: permission denied"), path);
            break;

        case HTTP_STATUS_BAD_RANGE:
            vfff_set_err(EINVAL, _("%s: invalid range requested"), path);
            break;

        case HTTP_STATUS_MOVED_TEMPORARILY:
        case HTTP_STATUS_MOVED_PERMANENTLY:
            break;

        default:
            if (errno == 0)
                errno = EINVAL;
            vfff_set_err(EINVAL, "%s: %m (%s)", path, msg);
            break;
    }

    return is_err == 0;
}

int httpcn_get_resp(struct vcn *cn)
{
    int rc = 1;

    if (cn->state != VCN_ALIVE)
        return 0;

    if (cn->resp)
        http_resp_free(cn->resp);

    if ((cn->resp = do_http_read_resp(cn->sockfd)) == NULL) {
        cn->state = VCN_DEAD;
        rc = 0;
    }

    return rc;
}

static int decode_wday(const char *s)
{
    int i;
    char *weekdays[] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat"};

    i = 0;
    while (i < 7) {
        if (strncasecmp(weekdays[i], s, 3) == 0)
            return i;
        i++;
    }

    return -1;
}

static int decode_month(const char *s)
{
    int i;
    char *months[] = { "jan", "feb", "mar", "apr", "may", "jun",
                       "jul", "aug", "sep", "oct", "nov", "dec" };

    i = 0;
    while (i < 12) {
        if (strncasecmp(months[i], s, 3) == 0)
            return i;
        i++;
    }

    return -1;
}

static time_t parse_date(const char *dt)
{
    char weekday[32], month[32];
    struct tm tm;
    time_t ts = -1;
    int is_ok = 1;

    /* Sun, 06 Nov 1994 08:49:37 GMT  (RFC 822, updated by RFC 1123) */
    if (sscanf(dt, "%16[a-zA-Z], %d %16[a-zA-Z] %d %d:%d:%d GMT",
               weekday, &tm.tm_mday, month, &tm.tm_year,
               &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 7) {

        /* Sunday, 06-Nov-94 08:49:37 GMT (RFC 850, obsoleted by RFC 1036) */
        if (sscanf(dt, "%16[a-zA-Z], %2d-%16[a-zA-Z]-%2d %d:%d:%d GMT",
                   weekday, &tm.tm_mday, month, &tm.tm_year,
                   &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 7) {

            tm.tm_year += 2000;

        /* Sun Nov  6 08:49:37 1994  (ANSI C's asctime() format)  */
        } else if (sscanf(dt, "%16[a-zA-Z] %16[a-zA-Z] %2d %d:%d:%d %d",
                          weekday, month, &tm.tm_mday, &tm.tm_hour, &tm.tm_min,
                          &tm.tm_sec, &tm.tm_year) != 7) {
            is_ok = 0;
        }
    }

    if (is_ok) {
        tm.tm_wday = decode_wday(weekday);
        tm.tm_mon = decode_month(month);
        if (tm.tm_mon >= 0 && tm.tm_wday >= 0) {
            tm.tm_year -= 1900;
            ts = mktime(&tm);
        }
    }

    return ts;
}

static
int vhttp_vcn_is_alive(struct vcn *cn)
{
    char req_line[256];

    if (cn->state != VCN_ALIVE)
        return 0;

    make_req_line(req_line, sizeof(req_line), "HEAD", "/");

    if (!httpcn_req(cn, req_line, NULL))
        return 0;

    if (!httpcn_get_resp(cn)) {
        cn->state = VCN_DEAD;
        return 0;
    }

    return 1;
}

static int is_closing_connection_status(struct http_resp *resp)
{
    int close_cn = 0;

    switch (http_resp_conn_status(resp)) {
        case -1:                /* no Connection header */
            if (resp->http_ver > 0) /* HTTP > 1.0 */
                break;
            /* fallthru */
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

    return close_cn;
}

static
int is_redirected_connection(struct http_resp *resp, struct vfff_req *rreq)
{
    int is_redirected = 0;

    if (HTTP_STATUS_IS_REDIR(resp->code)) {
        const char *redirto = http_resp_get_hdr(resp, "location");
        if (redirto && *redirto != '\0')
            snprintf(rreq->redirected_to, sizeof(rreq->redirected_to), "%s", redirto);
        else
            vfff_set_err(ENOENT, "wrong or empty redirect location");

        is_redirected = 1;
    }

    return is_redirected;
}

static
off_t http_resp_get_content_length(struct http_resp *resp)
{
    long int size;

    if (!http_resp_get_hdr_long(resp, "content-length", &size)) {
        vfff_set_err(EINVAL, _("Content-Length parse error (%s)"),
                      http_resp_get_hdr(resp, "content-length"));
        size = -1;
    }

    return (off_t)size;
}

static
int vhttp_vcn_stat(struct vcn *cn, struct vfff_req *rreq)
{
    char req_line[PATH_MAX];
    const char *s;
    int close_cn = 0, rc = 1;
    struct http_resp *resp;

    vfff_errno = 0;
    *rreq->redirected_to = '\0';

    make_req_line(req_line, sizeof(req_line), "HEAD", rreq->uri);
    if (!httpcn_req(cn, req_line, NULL))
        return 0;

    if (!httpcn_get_resp(cn))
        return 0;

    resp = cn->resp;
    close_cn = is_closing_connection_status(resp);

    if (is_redirected_connection(resp, rreq)) {
        rc = 0;                 /* see the comment in httpcn_retr() */
        goto l_end;
    }

    if (!status_code_ok(resp->code, resp->msg, rreq->uri)) {
        rc = 0;
        goto l_end;
    }

    rreq->st_remote_size = http_resp_get_content_length(resp);

    if ((s = http_resp_get_hdr(resp, "last-modified")) != NULL)
        rreq->st_remote_mtime = parse_date(s);

    if (rreq->st_remote_size == -1 && rreq->st_remote_mtime == -1)
        rc = 0;

 l_end:
    if (close_cn)
        vcn_close(cn);

    return rc;
}


static
int vhttp_vcn_retr(struct vcn *cn, struct vfff_req *rreq)
{
    int    close_cn = 0, rc = 1;
    long   from = 0, to = 0, total = 0, amount = 0;
    char   req_line[PATH_MAX];
    const  char *trenc;
    struct http_resp *resp;
    struct stat st;

    vfff_errno = 0;
    *rreq->redirected_to = '\0';
    n_assert(rreq->out_fd > 0);

    if ((lseek(rreq->out_fd, rreq->out_fdoff, SEEK_SET)) == (off_t)-1) {
        vfff_set_err(errno, "%s[%d]: lseek %ld: %m", n_basenam(rreq->uri),
                     rreq->out_fd, rreq->out_fdoff);
        goto l_err_end;
    }

    if ((fstat(rreq->out_fd, &st)) != 0) {
        vfff_set_err(errno, "%s: stat: %m", rreq->out_path);
        goto l_err_end;
    }


    if (rreq->out_fdoff < 0)
        rreq->out_fdoff = 0;

    make_req_line(req_line, sizeof(req_line), "GET", rreq->uri);

    if (rreq->out_fdoff > 0)
        httpcn_req(cn, req_line, "Range: bytes=%ld-\r\n", rreq->out_fdoff);
    else
        httpcn_req(cn, req_line, NULL);

    if (!httpcn_get_resp(cn))
        goto l_err_end;

    resp = cn->resp;

    close_cn = is_closing_connection_status(resp);

    if (is_redirected_connection(resp, rreq)) {
        rc = 0;             /* treat redirects as errors, caller should
                               check rreq's redirected_to  */
        goto l_end;
    }

    /* poor HTTP client doesn't support Trasfer-Encodings */
    if (!status_code_ok(resp->code, resp->msg, rreq->uri) &&
        resp->code != HTTP_STATUS_BAD_RANGE)
        goto l_err_end;

    if ((trenc = http_resp_get_hdr(resp, "transfer-encoding"))) {
        if (*vfff_verbose > 1)
            vfff_log("Trasfer-Encoding is an unimplemented tag, give up\n");
        vfff_set_err(ENOENT, "%s: unimplemented HTTP "
                      "transfer encoding", trenc);
        close_cn = 1;
        goto l_err_end;
    }

    if ((amount = http_resp_get_content_length(resp)) < 0)
        goto l_err_end;

    if ((trenc = http_resp_get_hdr(resp, "last-modified")) != NULL)
        rreq->st_remote_mtime = parse_date(trenc);

    if (rreq->out_fdoff == 0)
        total = amount;

    else {
        if (!http_resp_get_range(resp, &from, &to, &total)) {
            vfff_set_err(EINVAL, _("%s: Content-Range parse error (%s)"),
                         rreq->uri, http_resp_get_hdr(resp, "content-range"));
            goto l_err_end;
        }

        if (resp->code == HTTP_STATUS_BAD_RANGE) {
            if (rreq->out_fdoff != total) {
                if (*vfff_verbose > 1)
                    vfff_log(_("%s: invalid Content-Range, truncate %s\n"),
                             rreq->uri, rreq->out_path);

                if ((ftruncate(rreq->out_fd, 0) == 0))
                    rreq->out_fdoff = 0;

                goto l_err_end;

            } else {
                if (*vfff_verbose > 1)
                    vfff_log(_("%s: already downloaded; mtime %s\n"),
                             rreq->uri, ctime(&rreq->st_remote_mtime));
                goto l_end;         /* downloaded */
            }
        }

        if (from != rreq->out_fdoff) {
            vfff_set_err(EINVAL, _("%s: invalid Content-Range reached"),
                         rreq->uri);
            goto l_err_end;
        }
    }

    rreq->st_remote_size = total;


    if (*vfff_verbose > 1) {
        long a = from ? total - from : total;
        vfff_log("Total file size %ld, %ld to download, mtime %s\n",
                 total, a, ctime(&rreq->st_remote_mtime));
    }

    errno = 0;
    if (!vfff_transfer_file(rreq, cn->sockfd, total))
        goto l_err_end;


 l_end:
    if (close_cn)
        vcn_close(cn);


    return rc;

 l_err_end:
    rc = 0;
    close_cn = 1;

    if (vfff_errno == 0)
        vfff_errno = EIO;

    goto l_end;

}

void vhttp_vcn_init(struct vcn *cn)
{
    cn->m_open = NULL;
    cn->m_close = NULL;
    cn->m_free = (void (*)(void*))http_resp_free;
    cn->m_is_alive = vhttp_vcn_is_alive;
    cn->m_retr = vhttp_vcn_retr;
    cn->m_stat = vhttp_vcn_stat;
}
