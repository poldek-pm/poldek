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

#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>

//#include <trurl/nbuf.h>
#include <trurl/nassert.h>
#include <trurl/nmalloc.h>

#include "vfff.h"
#include "../vfile.h"           /* for VFILE_CONF_SSL_VERIFY_NONE */
#include "../vfile_intern.h"    /* for vfile_conf */

struct sslmod {
    SSL_CTX *ctx;
    SSL *ssl;
};

static int raw_read(struct vcn *cn, void *buf, size_t n)
{
    return read(cn->sockfd, buf, n);
}

static int raw_write(struct vcn *cn, void *buf, size_t n)
{
    /* a connection the peer closed gives EPIPE, not a fatal SIGPIPE */
    return send(cn->sockfd, buf, n, MSG_NOSIGNAL);
}

static int raw_select(struct vcn *cn, unsigned timeout)
{
    struct timeval to = { timeout, 0 };
    fd_set fdset;

    FD_ZERO(&fdset);
    FD_SET(cn->sockfd, &fdset);

    return select(cn->sockfd + 1, &fdset, NULL, NULL, &to);
}

/* OpenSSL writes to the socket on its own, even in SSL_read() (an alert after
   a bare EOF): hold back the SIGPIPE a closed connection raises meanwhile */
static void sigpipe_hold(sigset_t *oldmask)
{
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &set, oldmask);
}

/* drop a SIGPIPE held back by sigpipe_hold(), restore the signal mask */
static void sigpipe_release(const sigset_t *oldmask)
{
    int saved_errno = errno;
    sigset_t set, pending;

    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);

    if (!sigismember(oldmask, SIGPIPE) && sigpending(&pending) == 0 &&
        sigismember(&pending, SIGPIPE)) {
        const struct timespec nowait = { 0, 0 };

        sigtimedwait(&set, NULL, &nowait);
    }

    pthread_sigmask(SIG_SETMASK, oldmask, NULL);
    errno = saved_errno;
}

static int ssl_read(struct vcn *cn, void *buf, size_t n)
{
    struct sslmod *mod = cn->iomod;
    sigset_t mask;
    int rc;

    n_assert(mod);
    sigpipe_hold(&mask);
    rc = SSL_read(mod->ssl, buf, n);
    sigpipe_release(&mask);

    return rc;
}

static int ssl_write(struct vcn *cn, void *buf, size_t n)
{
    struct sslmod *mod = cn->iomod;
    sigset_t mask;
    int rc;

    n_assert(mod);
    sigpipe_hold(&mask);
    rc = SSL_write(mod->ssl, buf, n);
    sigpipe_release(&mask);

    return rc;
}

static int ssl_select(struct vcn *cn, unsigned timeout)
{
    struct sslmod *mod = cn->iomod;

    n_assert(mod);

    int pending = SSL_pending(mod->ssl);
    if (pending > 0)
        return pending;

    return raw_select(cn, timeout);
}

static void set_ssl_errors(void)
{
    unsigned long e;
    char buf[1024];
    int n = 0;

    buf[0] = '\0';
    while ((e = ERR_get_error())) {
        if (n > 0 && n < (int)sizeof(buf) - 2) {
            buf[n++] = ';';
            buf[n++] = ' ';
        }
        ERR_error_string_n(e, &buf[n], sizeof(buf) - n);
        n = strlen(buf);
    }

    if (*buf)
        vfff_set_err(EPROTO, "openssl: %s", buf);
}


static struct sslmod *init_ssl(const struct vcn *cn)
{
    const SSL_METHOD *method = NULL;
    SSL_CTX *ctx = NULL;
    SSL *ssl = NULL;
    sigset_t mask;
    int rc;

    method = TLS_client_method();
    ctx = SSL_CTX_new(method);
    if (ctx == NULL)
        goto l_err;

    /* TLS >= 1.2 */
    if (!SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION)) {
        vfff_set_err(EPROTO, "openssl: failed to set TLS >= 1.2");
        goto l_err;
    }

    if (vfile_conf.flags & VFILE_CONF_SSL_VERIFY_NONE) {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    } else {
        /* load system default CA certificates for server verification */
        if (!SSL_CTX_set_default_verify_paths(ctx)) {
            vfff_set_err(EPROTO, "openssl: failed to load CA certificates");
            goto l_err;
        }

        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    }

    ssl = SSL_new(ctx);
    if (ssl == NULL)
        goto l_err;

    if ((vfile_conf.flags & VFILE_CONF_SSL_VERIFY_NONE) == 0) {
        /* enable hostname verification against the server cert */
        SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        if (!SSL_set1_host(ssl, cn->host)) {
            vfff_set_err(EPROTO, "openssl: failed to set verification hostname");
            goto l_err;
        }
    }

    SSL_set_tlsext_host_name(ssl, cn->host);
    SSL_set_fd(ssl, cn->sockfd);

    sigpipe_hold(&mask);
    rc = SSL_connect(ssl);
    sigpipe_release(&mask);
    if (rc != 1)
        goto l_err;

    struct sslmod *mod = n_malloc(sizeof(*mod));
    mod->ctx = ctx;
    mod->ssl = ssl;

    return mod;

 l_err:
    set_ssl_errors();

    if (ssl)
        SSL_free(ssl);

    if (ctx)
        SSL_CTX_free(ctx);

    return NULL;
}

int vfff_io_init(struct vcn *cn)
{
    int ok = 1;

    if (cn->proto == VCN_PROTO_HTTPS) {
        cn->iomod = init_ssl(cn);
        cn->io_read = ssl_read;
        cn->io_write = ssl_write;
        cn->io_select = ssl_select;
        if (cn->iomod == NULL)
            ok = 0;

    } else {
        cn->io_read = raw_read;
        cn->io_write = raw_write;
        cn->io_select = raw_select;
    }

    return ok;
}

void vfff_io_destroy(struct vcn *cn)
{
    struct sslmod *mod = cn->iomod;
    if (mod) {
        SSL_free(mod->ssl);
        SSL_CTX_free(mod->ctx);
        free(mod);
        cn->iomod = NULL;
    }

    cn->io_read = NULL;
    cn->io_write = NULL;
    cn->io_select = NULL;
}
