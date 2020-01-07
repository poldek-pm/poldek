/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <trurl/trurl.h>
#include <sigint/sigint.h>

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "pkgmisc.h"
#include "misc.h"
#include "pkgset-req.h"
#include "poldek.h"
#include "poldek_intern.h"
#include "pm/pm.h"

#include "ictx.h"

struct i3err {
    struct pkg        *pkg;
    unsigned          code;
    char              *message;
    char              _message[0];
};

static void *i3err_new(unsigned errcode, struct pkg *pkg,
                       const char *fmt, va_list args)
{
    struct i3err *e;
    char message[1024];
    int n = 0;

    if (fmt)
        n = n_vsnprintf(message, sizeof(message), fmt, args);

    e = n_malloc(sizeof(*e) + n + 1);
    e->pkg = pkg_link(pkg);
    e->code = errcode;
    e->message = NULL;

    if (fmt) {
        memcpy(e->_message, message, n + 1);
        e->message = e->_message;
    }

    return e;
}

static void i3err_free(struct i3err *e)
{
    pkg_free(e->pkg);
    free(e);
}

void i3_error(struct i3ctx *ictx, struct pkg *pkg,
              unsigned errcode, const char *fmt, ...)
{
    struct i3err *e;
    tn_array *errors;

    va_list args;

    va_start(args, fmt);
    e = i3err_new(errcode, pkg, fmt, args);
    va_end(args);

    if ((errors = n_hash_get(ictx->errors, pkg_id(pkg))) == NULL) {
        errors = n_array_new(2, (tn_fn_free)i3err_free, NULL);
        n_hash_insert(ictx->errors, pkg_id(pkg), errors);
    }

    n_array_push(errors, e);
    if (e->message)
        logn(LOGERR, "%s", e->message);
}

void i3_forget_error(struct i3ctx *ictx, const struct pkg *pkg)
{
    n_hash_remove(ictx->errors, pkg_id(pkg));
}

int i3_get_nerrors(struct i3ctx *ictx, unsigned errcodeclass)
{
    tn_array *keys;
    int i, j, n = 0;

    keys = n_hash_keys(ictx->errors);
    for (i=0; i < n_array_size(keys); i++) {
        tn_array *errors = n_hash_get(ictx->errors, n_array_nth(keys, i));

        for (j=0; j < n_array_size(errors); j++) {
            struct i3err *e = n_array_nth(errors, j);
            if (e->code & errcodeclass)
                n++;
        }
    }

    return n;
}

#if 0                           /* unused */
static int i3pkg_cmp(struct i3pkg *n1, struct i3pkg *n2)
{
    return strcmp(pkg_id(n1->pkg), pkg_id(n2->pkg));
}
#endif


struct i3pkg *i3pkg_new(struct pkg *pkg, unsigned flags,
                        struct pkg *bypkg, const struct capreq *byreq,
                        enum i3_byflag byflag)
{
    struct i3pkg *i3pkg = n_calloc(sizeof(*i3pkg), 1);

    i3pkg->flags = flags;
    i3pkg->pkg = pkg_link(pkg);

    i3pkg->markedby = n_array_new(2, (tn_fn_free)i3pkg_free, NULL);
    i3pkg->obsoletedby = pkgs_array_new_ex(2, pkg_cmp_recno);

    if (bypkg) {
        i3pkg->bypkg = pkg_link(bypkg);
        i3pkg->byreq = byreq;
        i3pkg->byflag = byflag;
    }

    i3pkg->_refcnt = 0;
    return i3pkg;
}

struct i3pkg *i3pkg_link(struct i3pkg *i3pkg)
{
    i3pkg->_refcnt++;
    return i3pkg;
}

void i3pkg_free(struct i3pkg *i3pkg)
{
    if (i3pkg->_refcnt > 0) {
        i3pkg->_refcnt--;
        return;
    }


    pkg_free(i3pkg->pkg);

    n_array_cfree(&i3pkg->markedby);
    n_array_cfree(&i3pkg->obsoletedby);

    if (i3pkg->bypkg)
        pkg_free(i3pkg->bypkg);

    //if (i3pkg->candidates)
    //    n_hash_free(i3pkg->candidates);
    free(i3pkg);
}

#if 0                            /* unused */
void i3pkg_register_candidates(struct i3pkg *i3pkg, const struct capreq *req,
                               tn_array *candidates)
{
    char *strreq = capreq_stra(req);

    if (i3pkg->candidates == NULL)
        i3pkg->candidates = n_hash_new(8, (tn_fn_free)n_array_free);

    n_hash_insert(i3pkg->candidates, strreq, candidates);
}
#endif


void i3ctx_init(struct i3ctx *ictx, struct poldek_ts *ts)
{
    ictx->i3pkg_stack = n_array_new(32, (tn_fn_free)i3pkg_free, NULL);

    ictx->inset = iset_new();
    ictx->unset = iset_new();

    ictx->ma_flags = 0;
    if (ts->getop(ts, POLDEK_OP_VRFYMERCY))
        ictx->ma_flags = POLDEK_MA_PROMOTE_VERSION;

    ictx->ts = ts;
    ictx->ps = ts->ctx->ps;

    ictx->processed = pkgmark_set_new(0, PKGMARK_SET_IDPTR);

    ictx->multi_obsoleted = n_hash_new(8, (tn_fn_free)n_array_free);
    ictx->errors = n_hash_new(8, (tn_fn_free)n_array_free);
    ictx->abort = 0;
}

void i3ctx_destroy(struct i3ctx *ictx)
{
    n_array_cfree(&ictx->i3pkg_stack);
    iset_free(ictx->inset);
    iset_free(ictx->unset);

    ictx->ts = NULL;
    ictx->ps = NULL;
    pkgmark_set_free(ictx->processed);

    n_hash_free(ictx->multi_obsoleted);
    n_hash_free(ictx->errors);
    memset(ictx, 0, sizeof(*ictx));
}

void i3ctx_reset(struct i3ctx *ictx)
{
    n_array_clean(ictx->i3pkg_stack);

    iset_free(ictx->inset);
    ictx->inset = iset_new();

    iset_free(ictx->unset);
    ictx->unset = iset_new();

    pkgmark_set_free(ictx->processed);
    ictx->processed = pkgmark_set_new(0, PKGMARK_SET_IDPTR);

    n_hash_clean(ictx->multi_obsoleted);
    n_hash_clean(ictx->errors);
    ictx->abort = 0;
}

int i3_stop_processing(struct i3ctx *ictx, int stop)
{
    ictx->abort = stop;
    return ictx->abort;
}
