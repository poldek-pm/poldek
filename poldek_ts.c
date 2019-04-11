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

#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/stat.h>
#include <sys/types.h>


#include <trurl/nmalloc.h>
#include <trurl/nstr.h>

#include "compiler.h"
#include "vfile/vfile.h"
#include "pkgdir/source.h"
#include "pm/pm.h"
#include "poldek.h"
#include "poldek_intern.h"
#include "poldek_term.h"
#include "pkgset.h"
#include "pkgset-req.h"
#include "pkgmisc.h"
#include "pkgset-req.h"
#include "arg_packages.h"
#include "misc.h"
#include "log.h"
#include "i18n.h"
#include "fileindex.h"

extern int poldek_conf_PROMOTE_EPOCH;
extern int poldek_conf_MULTILIB;

#define bitvect_slot_itype  uint32_t
#define bitvect_slot_size   sizeof(bitvect_slot_itype) * CHAR_BIT
#define bitvect_mask(b)     (1 << ((b) % (bitvect_slot_size)))
#define bitvect_slot(b)     ((b) / (bitvect_slot_size))
#define bitvect_set(a, b)   ((a)[bitvect_slot(b)] |= bitvect_mask(b))
#define bitvect_clr(a, b)   ((a)[bitvect_slot(b)] &= ~(bitvect_mask(b)))
#define bitvect_isset(a, b) ((a)[bitvect_slot(b)] & bitvect_mask(b))

extern int do_poldek_ts_install_dist(struct poldek_ts *ts);
extern int do_poldek_ts_upgrade_dist(struct poldek_ts *ts);
extern int do_poldek_ts_uninstall(struct poldek_ts *ts);

static int ts_run_install(struct poldek_ts *ts);
static int ts_run_uninstall(struct poldek_ts *ts);
static int ts_run_verify(struct poldek_ts *ts);

typedef int (*ts_run_fn)(struct poldek_ts *);

struct ts_run {
    int        type;
    ts_run_fn  run;
    unsigned   flags;
};

#define TS_RUN_NEEDAVSET (1 << 0)
#define TS_RUN_NOPRERUN  (1 << 3)
struct ts_run ts_run_tbl[] = {
    { POLDEK_TS_VERIFY, (ts_run_fn)ts_run_verify, TS_RUN_NOPRERUN | TS_RUN_NEEDAVSET },
    { POLDEK_TS_INSTALL, (ts_run_fn)ts_run_install, TS_RUN_NEEDAVSET },
    { POLDEK_TS_UNINSTALL, (ts_run_fn)ts_run_uninstall, 0 },
    { 0, 0, 0 },
};


#define TS_CONFIG_LATER (1 << 0)

static int poldek_ts_init(struct poldek_ts *ts, struct poldek_ctx *ctx);
static void poldek_ts_destroy(struct poldek_ts *ts);


struct poldek_ts *poldek_ts_new(struct poldek_ctx *ctx, unsigned flags)
{
    struct poldek_ts *ts;

    ts = n_malloc(sizeof(*ts));
    poldek_ts_init(ts, ctx);
    poldek_ts_setf(ts, flags);

    return ts;
}

void poldek_ts_free(struct poldek_ts *ts)
{
    poldek_ts_destroy(ts);
    free(ts);
    MEMINF("");
}


void poldek_ts_xsetop(struct poldek_ts *ts, int optv, int on, int touch)
{
    n_assert(bitvect_slot(optv) < sizeof(ts->_opvect)/sizeof(bitvect_slot_itype));

    //n_assert(optv != POLDEK_OP_VRFY_DEPS);

    switch (optv) {
        case POLDEK_OP_PROMOTEPOCH:
            poldek_conf_PROMOTE_EPOCH = on;
            DBGF("set %p (%p) %d %d\n", ts, ts->ctx, optv, on);
            /* propagate it to ctx too, it is (unfortunately) global variable */
            if (ts->ctx)
                poldek_configure(ts->ctx, POLDEK_CONF_OPT, optv, on);
            break;

        case POLDEK_OP_MULTILIB:
            poldek_conf_MULTILIB = on;
            DBGF("set %p (%p) %d %d\n", ts, ts->ctx, optv, on);
            /* propagate it to ctx too, it is (unfortunately) global variable */
            if (ts->ctx)
                poldek_configure(ts->ctx, POLDEK_CONF_OPT, optv, on);
            break;

        case POLDEK_OP_VRFY_FILECNFLS:
        case POLDEK_OP_VRFY_FILEORPHANS:
        case POLDEK_OP_VRFY_FILEMISSDEPS:
            /* propagate it to ctx too - pkgset_load() must know that
               to load whole file database */
            if (ts->ctx) {
                poldek_configure(ts->ctx, POLDEK_CONF_OPT, optv, on);
                poldek_configure(ts->ctx, POLDEK_CONF_OPT,
                                 POLDEK_OP_LDFULLFILELIST, on);
            }

            break;

        case POLDEK_OP_GREEDY:
            DBGF("set (touch=%d) greedy ts=%p (ctx=%p) greedy=%d val=%d\n",
                 touch, ts, ts->ctx, optv, on);
            if (on)
                poldek_ts_xsetop(ts, POLDEK_OP_FOLLOW, 1, touch);

            ts->uninstall_greedy_deep = on;
            break;

        case POLDEK_OP_CONFLICTS:
            DBGF("%p set conflicts %d (t=%d)\n", ts, on, touch);
            break;

        case POLDEK_OP_OBSOLETES:
            DBGF("%p set obsoletes %d (t=%d)\n", ts, on, touch);
            break;

        case POLDEK_OP_FOLLOW:
            if (!on)
                poldek_ts_xsetop(ts, POLDEK_OP_GREEDY, 0, touch);
            break;

        case POLDEK_OP_USESUDO:
            DBGF("%p set use_sudo %d\n", ts, on);
            break;

        default:
            break;
    }

    if (on)
        bitvect_set(ts->_opvect, optv);
    else
        bitvect_clr(ts->_opvect, optv);

    if (touch)
        bitvect_set(ts->_opvect_touched, optv); /* touched */
}

void poldek_ts_setop(struct poldek_ts *ts, int optv, int on)
{
    poldek_ts_xsetop(ts, optv, on, 1);
}


int poldek_ts_getop(const struct poldek_ts *ts, int optv)
{
    n_assert(bitvect_slot(optv) < sizeof(ts->_opvect)/sizeof(bitvect_slot_itype));
    return bitvect_isset(ts->_opvect, optv) > 0;
}

int poldek_ts_op_touched(const struct poldek_ts *ts, int optv)
{
    n_assert(bitvect_slot(optv) < sizeof(ts->_opvect)/sizeof(bitvect_slot_itype));
    return bitvect_isset(ts->_opvect_touched, optv) > 0;
}

int poldek_ts_getop_v(const struct poldek_ts *ts, int optv, ...)
{
    unsigned v = 0;
    va_list ap;

    va_start(ap, optv);
    while (optv > 0) {
        if (poldek_ts_getop(ts, optv)) {
            DBGF("getop_v %d ON\n", optv);
            v++;
        }
        optv = va_arg(ap, int);
    }
    va_end(ap);
    return v;
}

int poldek_ts_is_interactive_on(const struct poldek_ts *ts)
{
    return ts->getop_v(ts, POLDEK_OP_CONFIRM_INST, POLDEK_OP_CONFIRM_UNINST,
                       POLDEK_OP_EQPKG_ASKUSER, 0);
}


static void cp_str(char **dst, const char *src)
{
    if (src)
        *dst = n_strdup(src);
    else
        *dst = NULL;
}

static int poldek_ts_init(struct poldek_ts *ts, struct poldek_ctx *ctx)
{
    memset(ts, 0, sizeof(*ts));
    ts->setop = poldek_ts_setop;
    ts->getop = poldek_ts_getop;
    ts->getop_v = poldek_ts_getop_v;

    if (ctx == NULL) {
        ts->aps = NULL;

    } else {
        ts->ctx = ctx;
        memcpy(ts->_opvect, ctx->ts->_opvect, sizeof(ts->_opvect));
        if (!poldek__is_setup_done(ctx))
            ts->_iflags |= TS_CONFIG_LATER;

        ts->aps = arg_packages_new();
        ts->pmctx = ctx->pmctx;
    }
    ts->_na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    ts->db = NULL;

    if (ctx) {  /* copy configuration from ctx's ts */
        cp_str(&ts->rootdir, ctx->ts->rootdir);
        cp_str(&ts->fetchdir, ctx->ts->fetchdir);
        cp_str(&ts->cachedir, ctx->ts->cachedir);
        cp_str(&ts->dumpfile, ctx->ts->dumpfile);
        cp_str(&ts->prifile, ctx->ts->prifile);
        cp_str(&ts->depgraph, ctx->ts->depgraph);

        ts->rpmacros = n_array_dup(ctx->ts->rpmacros, (tn_fn_dup)strdup);
        ts->rpmopts = n_array_dup(ctx->ts->rpmopts, (tn_fn_dup)strdup);
        ts->hold_patterns = n_array_dup(ctx->ts->hold_patterns,
                                        (tn_fn_dup)strdup);
        ts->ign_patterns = n_array_dup(ctx->ts->ign_patterns,
                                       (tn_fn_dup)strdup);

        ts->exclude_path = n_array_dup(ctx->ts->exclude_path,
                                       (tn_fn_dup)strdup);
    } else {                    /* no ctx? -> it's ctx internal ts */
        ts->rootdir = NULL;
        ts->fetchdir = NULL;
        ts->cachedir = NULL;
        ts->dumpfile = NULL;
        ts->prifile = NULL;

        ts->rpmacros = n_array_new(2, free, NULL);
        ts->rpmopts = n_array_new(4, free, (tn_fn_cmp)strcmp);
        ts->hold_patterns = n_array_new(4, free, (tn_fn_cmp)strcmp);
        ts->ign_patterns = n_array_new(4, free, (tn_fn_cmp)strcmp);
        ts->exclude_path = n_array_new(4, free, (tn_fn_cmp)strcmp);

    }
    DBGF("%p->%p, %p\n", ts, ts->hold_patterns, ctx);


    ts->pms = pkgmark_set_new(1024, 0);

    ts->ts_summary = n_hash_new(4, (tn_fn_free)n_array_free);
    ts->pkgs_installed = pkgs_array_new(16);
    ts->pkgs_removed   = pkgs_array_new(16);
    return 1;
}

static void poldek_ts_destroy(struct poldek_ts *ts)
{
    ts->_flags = 0;
    ts->ctx = NULL;

    if (ts->db)
        pkgdb_free(ts->db);

    ts->db = NULL;

    if (ts->aps)
        arg_packages_free(ts->aps);

    n_cfree(&ts->rootdir);
    n_cfree(&ts->fetchdir);
    n_cfree(&ts->cachedir);
    n_cfree(&ts->dumpfile);
    n_cfree(&ts->prifile);

    n_array_cfree(&ts->rpmopts);
    n_array_cfree(&ts->rpmacros);
    n_array_cfree(&ts->hold_patterns);
    n_array_cfree(&ts->ign_patterns);
    n_array_cfree(&ts->exclude_path);

    if (ts->pm_pdirsrc)
        source_free(ts->pm_pdirsrc);

    if (ts->pms)
        pkgmark_set_free(ts->pms);

    n_hash_free(ts->ts_summary);
    n_array_cfree(&ts->pkgs_installed);
    n_array_cfree(&ts->pkgs_removed);
    n_alloc_free(ts->_na);
}


void poldek_ts_setf(struct poldek_ts *ts, uint32_t flag)
{
    int type = 0;

    if (flag & POLDEK_TS_INSTALL)
        type = POLDEK_TS_INSTALL;

    else if (flag & POLDEK_TS_UNINSTALL)
        type = POLDEK_TS_UNINSTALL;

    else if (flag & POLDEK_TS_VERIFY)
        type = POLDEK_TS_VERIFY;

    if (type)
        poldek_ts_set_type(ts, type, "uninstall");

    ts->_flags |= flag;
}

void poldek_ts_clrf(struct poldek_ts *ts, uint32_t flag)
{
    switch (flag) {
        case POLDEK_TS_INSTALL:
        case POLDEK_TS_UNINSTALL:
        case POLDEK_TS_VERIFY:
            ts->type = 0;
            break;
    }
    ts->_flags &= ~flag;
}

uint32_t poldek_ts_issetf(struct poldek_ts *ts, uint32_t flag)
{
    return ts->_flags & flag;
}

int poldek_ts_issetf_all(struct poldek_ts *ts, uint32_t flag)
{
    return (ts->_flags & flag) == flag;
}

static char *prepare_path(char *pathname)
{
    if (pathname == NULL)
        return pathname;

    if (vf_url_type(pathname) & VFURL_LOCAL) {
        char *ppath;

        if ((ppath = util__abs_path(pathname)))
            return ppath;       /* malloced by abs_path */
    }

    return pathname;
}


char *poldek__conf_path(char *s, char *v)
{
    char *ss;

    if ((v && s && v != s) || s == NULL) {
        if (s)
            free(s);
        s = n_strdup(v);
    }

    ss = prepare_path(s);
    if (ss != s) {
        free(s);
        s = ss;
    }

    return s;
}

int poldek_ts_configure(struct poldek_ts *ts, int param, ...)
{
    va_list ap;
    int rc;

    va_start(ap, param);
    rc = poldek_ts_vconfigure(ts, param, ap);
    va_end(ap);
    return rc;
}


int poldek_ts_vconfigure(struct poldek_ts *ts, int param, va_list ap)
{
    int      rc;
    char     *vs;
    unsigned uv, uv_val;


    rc = 1;
    switch (param) {
        case POLDEK_CONF_OPT:
            uv = va_arg(ap, unsigned);
            uv_val = va_arg(ap, unsigned);
            ts->setop(ts, uv, uv_val);
            break;

        case POLDEK_CONF_CACHEDIR:
            if ((vs = va_arg(ap, char*))) {
                DBGF("cachedirX0 %s\n", vs);
                ts->cachedir = poldek__conf_path(ts->cachedir, vs);
                trimslash(ts->cachedir);
                DBGF("cachedirX %s\n", ts->cachedir);
            }
            break;

        case POLDEK_CONF_FETCHDIR:
            if ((vs = va_arg(ap, char*))) {
                ts->fetchdir = poldek__conf_path(ts->fetchdir, vs);
                trimslash(ts->fetchdir);
                DBGF("fetchdir %s\n", ts->fetchdir);
            }
            break;

        case POLDEK_CONF_ROOTDIR:
            if ((vs = va_arg(ap, char*))) {
                ts->rootdir = poldek__conf_path(ts->rootdir, vs);
                trimslash(ts->rootdir);
                DBGF("rootdir %s\n", ts->rootdir);
            }
            break;

#if 0                           /* XXX - NFY, to rethink */
        case POLDEK_CONF_PM_PDIRSRC:
            vv = va_arg(ap, void*);
            if (vv) {
                struct source *src = (struct source*)vv;
                if (src->path)
                    src->path = poldek__conf_path(src->path, NULL);
                ts->pm_pdirsrc = src;
            }
#endif

        case POLDEK_CONF_DUMPFILE:
            if ((vs = va_arg(ap, char*))) {
                DBGF("dumpfile %s\n", vs);
                ts->dumpfile = poldek__conf_path(ts->dumpfile, vs);
            }
            break;

        case POLDEK_CONF_PRIFILE:
            if ((vs = va_arg(ap, char*))) {
                DBGF("prifile %s\n", vs);
                ts->prifile = poldek__conf_path(ts->prifile, vs);
            }
            break;

        case POLDEK_CONF_DEPGRAPH:
            if ((vs = va_arg(ap, char*)))
                ts->depgraph = n_strdup(vs);
            break;

        case POLDEK_CONF_RPMMACROS:
            if ((vs = va_arg(ap, char*)))
                n_array_push(ts->rpmacros, n_strdup(vs));
            break;

        case POLDEK_CONF_RPMOPTS:
            if ((vs = va_arg(ap, char*))) {
                if (n_str_eq(vs, "--ignorearch"))
                    ts->setop(ts, POLDEK_OP_IGNOREARCH, 1);
                else if (n_str_eq(vs, "--ignoreos"))
                    ts->setop(ts, POLDEK_OP_IGNOREOS, 1);

                n_array_push(ts->rpmopts, n_strdup(vs));
            }
            break;

        case POLDEK_CONF_HOLD:
        case POLDEK_CONF_IGNORE: {
            tn_array *patterns = NULL;

            if (param == POLDEK_CONF_HOLD)
                patterns = ts->hold_patterns;
            else if (param == POLDEK_CONF_IGNORE)
                patterns = ts->ign_patterns;

            if ((vs = va_arg(ap, char*)) == NULL) { /* reset */
                n_array_clean(patterns);

            } else {
                if (strchr(vs, ',') == NULL) {
                    n_array_push(patterns, n_strdup(vs));

                } else {
                    const char **tl_save, **tl;

                    tl = tl_save = n_str_tokl(vs, ",");
                    while (*tl) {
                        if (**tl)
                            n_array_push(patterns, n_strdup(*tl));
                        tl++;
                    }
                    n_str_tokl_free(tl_save);
                }
            }
#if ENABLE_TRACE
            {
                int i;
                for (i=0; i < n_array_size(ts->hold_patterns); i++) {
                    char *mask = n_array_nth(ts->hold_patterns, i);
                    DBGF("hold %s\n", mask);
                }
            }
#endif

            break;
        }

        default:
            n_assert(0);
    }

    va_end(ap);
    return rc;
}

struct pkgdb *poldek_ts_dbopen(struct poldek_ts *ts, mode_t mode)
{
    if (mode == 0)
        mode = O_RDONLY;

    return pkgdb_open(ts->pmctx, ts->rootdir, NULL, mode,
                      ts->pm_pdirsrc ? "source" : NULL,
                      ts->pm_pdirsrc ? ts->pm_pdirsrc : NULL, NULL);
}

int poldek_ts_add_pkg(struct poldek_ts *ts, struct pkg *pkg)
{
    return arg_packages_add_pkg(ts->aps, pkg);
}

int poldek_ts_add_pkgs(struct poldek_ts *ts, tn_array *pkgs)
{
    int i;

    for (i=0; i < n_array_size(pkgs); i++)
        arg_packages_add_pkg(ts->aps, n_array_nth(pkgs, i));

    return i;
}

int poldek_ts_add_pkgmask(struct poldek_ts *ts, const char *mask)
{
    DBGF("%s\n", mask);
    return arg_packages_add_pkgmask(ts->aps, mask);
}

int poldek_ts_add_pkglist(struct poldek_ts *ts, const char *path)
{
    DBGF("%s\n", path);
    return arg_packages_add_pkglist(ts->aps, path);
}

int poldek_ts_add_pkgfile(struct poldek_ts *ts, const char *path)
{
    DBGF("%s\n", path);
    return arg_packages_add_pkgfile(ts->aps, path);
}

void poldek_ts_clean_args(struct poldek_ts *ts)
{
    arg_packages_clean(ts->aps);
}

tn_array* poldek_ts_get_args_asmasks(struct poldek_ts *ts, int hashed)
{
    return arg_packages_get_masks(ts->aps, hashed);
}

int poldek_ts_get_arg_count(struct poldek_ts *ts)
{
    return arg_packages_size(ts->aps);
}

#define TS_MARK_DEPS        (1 << 0)
#define TS_MARK_VERBOSE     (1 << 1)
#define TS_MARK_CAPSINLINE  (1 << 2)
static
int ts_mark_arg_packages(struct poldek_ts *ts, unsigned flags)
{
    int rc = 0;
    unsigned apsflags = 0;

    arg_packages_setup(ts->aps, ts->pmctx);
    if (arg_packages_size(ts->aps) == 0) {
        logn(LOGERR, _("Nothing to do"));
        return 0;
    }

    if (ts->getop(ts, POLDEK_OP_CAPLOOKUP))
        apsflags |= ARG_PACKAGES_RESOLV_CAPS;

    if (flags & TS_MARK_CAPSINLINE)
        apsflags |= ARG_PACKAGES_RESOLV_CAPSINLINE;

    if (arg_packages_resolve(ts->aps, ts->ctx->ps->pkgs,
                             ts->ctx->ps, apsflags)) {
        tn_array *pkgs;
        rc = 1;

        pkgs = arg_packages_get_resolved(ts->aps);
        if (flags & TS_MARK_VERBOSE) {
            int i;
            for (i=0; i < n_array_size(pkgs); i++) {
                struct pkg *pkg = n_array_nth(pkgs, i);
                msgn(1, _("mark %s"), pkg_id(pkg));
            }
        }

        if (flags & TS_MARK_DEPS)
            msgn(1, _("Processing dependencies..."));

        if (n_array_size(pkgs)) {
            rc = packages_mark(ts->pms, pkgs, flags & TS_MARK_DEPS);
            if (!rc && ts->getop_v(ts, POLDEK_OP_NODEPS, POLDEK_OP_FORCE, 0))
                rc = 1;
        }
        n_array_free(pkgs);
    }

    return rc;
}




static void cp_str_ifnull(char **dst, const char *src)
{
    if (*dst == NULL && src)
        *dst = n_strdup(src);
}


static void cp_arr_ifnull(tn_array **dst, tn_array *src)
{
    if ((*dst == NULL || n_array_size(*dst) == 0) && n_array_size(src)) {
        if (*dst)
            n_array_free(*dst);
        *dst = n_ref(src);
    }
}

int poldek_ts_set_type(struct poldek_ts *ts, uint32_t type, const char *typenam)
{
    if (ts->type)
        return 0;

    switch (type) {
        case POLDEK_TS_INSTALL:
        case POLDEK_TS_VERIFY:
            ts->type = type;
            break;

        case POLDEK_TS_UNINSTALL:
            /* set to default 0 if not touched before,
               opt propagation is messy a bit */
            if (!poldek_ts_op_touched(ts, POLDEK_OP_GREEDY)) {
                poldek_ts_setop(ts, POLDEK_OP_GREEDY, 0);
            }
            ts->type = type;
            break;

        default:
            n_die("%d: unknown ts type", type);
            n_assert(0);
    }

    ts->type = type;
    if (typenam)
        ts->typenam = n_strdup(typenam);
    return 1;
}

int poldek_ts_get_type(struct poldek_ts *ts)
{
    return ts->type;
}

static int ts_prerun0(struct poldek_ts *ts)
{
    n_assert(ts->ctx);
    n_assert(poldek__is_setup_done(ts->ctx));

    ts->pmctx = ts->ctx->pmctx;
    n_assert(ts->pmctx);

    if (ts->_iflags & TS_CONFIG_LATER)
        poldek__ts_postconf(ts->ctx, ts);

    n_array_clean(ts->pkgs_installed);
    n_array_clean(ts->pkgs_removed);
    n_hash_clean(ts->ts_summary);
    return 1;
}

static int ts_prerun(struct poldek_ts *ts)
{
    int rc = 1;

    cp_str_ifnull(&ts->rootdir, ts->ctx->ts->rootdir);
    cp_str_ifnull(&ts->fetchdir, ts->ctx->ts->fetchdir);
    cp_str_ifnull(&ts->cachedir, ts->ctx->ts->cachedir);
    cp_str_ifnull(&ts->dumpfile, ts->ctx->ts->dumpfile);
    cp_str_ifnull(&ts->prifile, ts->ctx->ts->prifile);

    cp_arr_ifnull(&ts->rpmacros, ts->ctx->ts->rpmacros);
    cp_arr_ifnull(&ts->rpmopts, ts->ctx->ts->rpmopts);
    DBGF("%p->%p, %p->%p\n", ts, ts->hold_patterns, ts->ctx->ts,
          ts->ctx->ts->hold_patterns);
    cp_arr_ifnull(&ts->hold_patterns, ts->ctx->ts->hold_patterns);
    cp_arr_ifnull(&ts->ign_patterns, ts->ctx->ts->ign_patterns);

#if 0
    {
        int i;

        for (i=0; i < n_array_size(ts->hold_patterns); i++) {
            char *mask = n_array_nth(ts->hold_patterns, i);
            DBGF("hold %s\n", mask);
        }
    }
#endif

    if (ts->rootdir == NULL)
        ts->rootdir = n_strdup("/");

    if (ts->getop(ts, POLDEK_OP_RPMTEST)) {
        //if (poldek_VERBOSE < 1)
        //    poldek_set_verbose(poldek_VERBOSE + 1);

    } else if (ts->getop_v(ts, POLDEK_OP_JUSTFETCH, POLDEK_OP_JUSTPRINT, 0)) {
        if (!poldek_util_is_rwxdir(ts->rootdir)) {
            logn(LOGERR, "%s: %m", ts->rootdir);
            rc = 0;
        }
    }

    if (rc)
        rc = arg_packages_setup(ts->aps, ts->pmctx);

    return rc;
}

tn_array *poldek__ts_install_ordered_packages(const struct poldek_ts *ts)
{
    tn_array *pkgs = n_array_new(512, (tn_fn_free)pkg_free, NULL);
    int i;

    for (i=0; i < n_array_size(ts->ctx->ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ts->ctx->ps->ordered_pkgs, i);

        if (pkg_is_marked(ts->pms, pkg))
            n_array_push(pkgs, pkg_link(pkg));
    }

    return pkgs;
}

/* install summary saved to ts to propagate it to high level api  */
void poldek__ts_update_summary(struct poldek_ts *ts,
                               const char *prefix, const tn_array *pkgs,
                               unsigned pmsflags, const struct pkgmark_set *pms)
{
    tn_array *supkgs;
    int i;

    n_assert(pkgs);
    if (n_array_size(pkgs) == 0)
        return;

    if (pms == NULL)
        n_assert(pmsflags == 0);

    if ((supkgs = n_hash_get(ts->ts_summary, prefix)) == NULL)
        supkgs = pkgs_array_new(n_array_size(pkgs));

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        if (pmsflags && pms && !pkgmark_isset(pms, pkg, pmsflags))
            continue;

        n_array_push(supkgs, pkg_link(pkg));
    }

    if (n_array_size(supkgs) == 0) {
        n_array_free(supkgs);

    } else {
        n_hash_insert(ts->ts_summary, prefix, supkgs);
        n_array_sort(supkgs);
    }

}

void poldek__ts_display_summary(struct poldek_ts *ts)
{
    int ninst = 0, ndep = 0, nrm = 0, npkgs = 0, parseable = 0;
    long int sinsts = 0, sdeps = 0, srems = 0, sdiff = 0;
    tn_array *ipkgs, *idepkgs, *rmpkgs, *pkgs;
    char ms[1024], *to, *prefix;
    int i, n;

    ipkgs = n_hash_get(ts->ts_summary, "I");
    idepkgs = n_hash_get(ts->ts_summary, "D");
    rmpkgs = n_hash_get(ts->ts_summary, "R");

    ninst = ipkgs ? n_array_size(ipkgs) : 0;
    ndep  = idepkgs ? n_array_size(idepkgs) : 0;
    nrm   = rmpkgs ? n_array_size(rmpkgs) : 0;

    if (ipkgs)
	for (i=0; i < ninst; i++) {
	    struct pkg *pkg = n_array_nth(ipkgs, i);
	    sinsts += pkg->size;
	}
    if (idepkgs)
	for (i=0; i < ndep; i++) {
	    struct pkg *pkg = n_array_nth(idepkgs, i);
	    sdeps += pkg->size;
	}
    if (rmpkgs)
	for (i=0; i < nrm; i++) {
	    struct pkg *pkg = n_array_nth(rmpkgs, i);
	    srems += pkg->size;
	}

    if (ts->type != POLDEK_TS_UNINSTALL) {
        to = _("to install");
        prefix = "I";
        pkgs = ipkgs;
        npkgs = ninst + ndep;
        sdiff = sinsts + sdeps - srems;
    } else {
        to = _("to remove");
        prefix = "R";
        pkgs = rmpkgs;
        npkgs = nrm + ndep;
        sdiff = - srems - sdeps;
        nrm = 0;
    }
    n_assert(pkgs);
    n_assert(npkgs);

#ifndef ENABLE_NLS
    n = n_snprintf(ms, sizeof(ms),
                   "There are %d package%s %s", npkgs, npkgs > 1 ? "s":"", to);
    if (ndep)
        n += n_snprintf(&ms[n], sizeof(ms) - n,
                        " (%d marked by dependencies)", ndep);

#else
    n = n_snprintf(ms, sizeof(ms),
                   ngettext("There are %d package %s",
                            "There are %d packages %s", npkgs), npkgs, to);

    if (ndep)
        n += n_snprintf(&ms[n], sizeof(ms),
                        ngettext(" (%d marked by dependencies)",
                                 " (%d marked by dependencies)", ndep), ndep);
#endif
    if (nrm)
        n += n_snprintf(&ms[n], sizeof(ms) - n, _(", %d to remove"), nrm);

    n_snprintf(&ms[n], sizeof(ms) - n,  ":");
    msgn(1, "%s", ms);

    parseable = ts->getop(ts, POLDEK_OP_PARSABLETS);

    if (npkgs)
        packages_display_summary(1, prefix, pkgs, parseable);

    if (idepkgs && ndep)
        packages_display_summary(1, "D", idepkgs, parseable);

    if (ts->type != POLDEK_TS_UNINSTALL) {
        if (rmpkgs)
            packages_display_summary(1, "R", rmpkgs, parseable);
    }

    if (sdiff != 0) {
        char size[64];
        snprintf_size(size, sizeof(size), labs(sdiff), 1, 1);

        if (sdiff > 0)
           msgn(1, _("This operation will use %s of disk space."), size);
        else
           msgn(1, _("This operation will free %s of disk space."), size);
    }
}

tn_array *poldek_ts_get_summary(const struct poldek_ts *ts, const char *mark)
{
    tn_array *pkgs;
    n_assert(mark != NULL);
    pkgs = n_hash_get(ts->ts_summary, mark);

    if (pkgs != NULL)
        return n_ref(pkgs);
    return pkgs;
}

static int ts_run_install_dist(struct poldek_ts *ts)
{
    if (!ts_mark_arg_packages(ts, TS_MARK_DEPS | TS_MARK_CAPSINLINE))
        return 0;

    return do_poldek_ts_install_dist(ts);
}

static int ts_run_upgrade_dist(struct poldek_ts *ts)
{
    int rc;

    n_assert(poldek_ts_issetf(ts, POLDEK_TS_UPGRADEDIST));

    ts->db = poldek_ts_dbopen(ts, O_RDONLY);
    if (ts->db == NULL)
        return 0;

    pkgdb_tx_begin(ts->db, ts);
    rc = do_poldek_ts_upgrade_dist(ts);
    if (rc && !ts->getop(ts, POLDEK_OP_RPMTEST))
        pkgdb_tx_commit(ts->db);
    pkgdb_free(ts->db);
    ts->db = NULL;
    return rc;
}

extern int i3_do_poldek_ts_install(struct poldek_ts *ts);

static int ts_run_install(struct poldek_ts *ts)
{
    int rc = 0;

    DBGF("%d %s\n", ts->type, ts->typenam);
    n_assert(ts->type = POLDEK_TS_INSTALL);

    if (poldek_ts_issetf_all(ts, POLDEK_TS_UPGRADEDIST))
        return ts_run_upgrade_dist(ts);

    if (poldek_ts_issetf_all(ts, POLDEK_TS_INSTALLDIST))
        return ts_run_install_dist(ts);

    DBGF("%p follow = %d\n", ts, ts->getop(ts, POLDEK_OP_FOLLOW));
    if (!ts_mark_arg_packages(ts, 0)) {
        DBGF("ts_mark_arg_packages failed\n");
        return 0;
    }


    ts->db = poldek_ts_dbopen(ts, O_RDONLY);
    if (ts->db == NULL)
        return 0;

    pkgdb_tx_begin(ts->db, ts);
    DBGF("0 arg_packages_size=%d\n", arg_packages_size(ts->aps));

    rc = i3_do_poldek_ts_install(ts);
    if (rc && !ts->getop(ts, POLDEK_OP_RPMTEST))
        pkgdb_tx_commit(ts->db);

    pkgdb_free(ts->db);
    ts->db = NULL;
    return rc;
}

static int ts_run_uninstall(struct poldek_ts *ts)
{
    int rc;

    n_assert(ts->type == POLDEK_TS_UNINSTALL);

    ts->db = poldek_ts_dbopen(ts, O_RDONLY);
    if (ts->db == NULL)
        return 0;
    pkgdb_tx_commit(ts->db);

    rc = do_poldek_ts_uninstall(ts);

    if (rc && !ts->getop(ts, POLDEK_OP_TEST))
        pkgdb_tx_commit(ts->db);

    MEMINF("before dbfree");
    pkgdb_free(ts->db);
    ts->db = NULL;
    MEMINF("END");

    return rc;
}

static int ts_run_verify(struct poldek_ts *ts)
{
    tn_array *pkgs = NULL, *pkgs_unordered = NULL;
    int nerr = 0, rc = 1;

    DBGF("%p\n", ts);
    //n_assert(poldek_ts_issetf(ts, POLDEK_TS_VERIFY));

    if (poldek_ts_get_arg_count(ts) == 0) {
        poldek_load_sources(ts->ctx);

    } else {
        if (!ts_prerun(ts))
            return 0;

        if (!poldek_load_sources(ts->ctx))
            return 0;

        rc = ts_mark_arg_packages(ts, TS_MARK_DEPS | TS_MARK_VERBOSE |
                                  TS_MARK_CAPSINLINE);
        (void)rc;               /* XXX unused for now */
    }

    if (poldek_ts_get_arg_count(ts) > 0) {
        pkgs = pkgmark_get_packages(ts->pms, PKGMARK_MARK | PKGMARK_DEP);
        if (pkgs)
            pkgs_unordered = n_ref(pkgs);

    } else {
        pkgs = n_ref(ts->ctx->ps->ordered_pkgs);
        if (pkgs)
            pkgs_unordered = n_ref(ts->ctx->ps->pkgs);
    }

    if (pkgs == NULL)
        return 0;

    if (ts->getop(ts, POLDEK_OP_VRFY_DEPS)) {
        msgn(0, _("Verifying dependencies..."));
        if (!packages_verify_dependecies(pkgs, ts->ctx->ps))
            nerr++;
    }

    if (ts->getop(ts, POLDEK_OP_DEPGRAPH)) {
        msgn(0, _("Generating dependency graph %s..."), ts->depgraph);
        if (!packages_generate_depgraph(pkgs, ts->ctx->ps, ts->depgraph))
            nerr++;
    }

    if (ts->getop(ts, POLDEK_OP_VRFY_ORDER)) {
        tn_array *ordered = NULL;
        if (!packages_order_and_verify(pkgs_unordered, &ordered, PKGORDER_INSTALL, 1))
            nerr++;
    }

    if (ts->getop(ts, POLDEK_OP_VRFY_CNFLS)) {
        int i, j;
        msgn(0, _("Verifying conflicts..."));

        for (i=0; i < n_array_size(pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgs, i);
            if (pkg->cnflpkgs == NULL)
                continue;

            msg(0, "%s -> ", pkg_id(pkg));
            for (j=0; j < n_array_size(pkg->cnflpkgs); j++) {
                struct reqpkg *cpkg = n_array_nth(pkg->cnflpkgs, j);
                msg(0, "_%s%s, ", cpkg->flags & REQPKG_OBSOLETE ? "*":"",
                    pkg_id(cpkg->pkg));
            }
            msg(0, "_\n");
        }
    }

    if (ts->getop(ts, POLDEK_OP_VRFY_FILECNFLS)) {
        msgn(0, _("Verifying file conflicts..."));
        file_index_report_conflicts(ts->ctx->ps->file_idx, pkgs);
    }

    if (ts->getop(ts, POLDEK_OP_VRFY_FILEORPHANS)) {
        msgn(0, _("Verifying file orphans..."));
        file_index_report_orphans(ts->ctx->ps->file_idx, pkgs);
    }

    if (ts->getop(ts, POLDEK_OP_VRFY_FILEMISSDEPS)) {
        msgn(0, _("Verifying file semi-orphans (missing dependencies)..."));
        file_index_report_semiorphans(ts->ctx->ps->file_idx, pkgs);
    }

    n_array_free(pkgs);
    n_array_free(pkgs_unordered);
    return nerr == 0;
}

int poldek_ts_run(struct poldek_ts *ts, unsigned flags)
{
    struct ts_run *ts_run = NULL;
    int i = 0;

    poldek_ts_setf(ts, flags);
    n_assert(ts->type);


    DBGF("%d(%s)\n", ts->type, ts->typenam);
    while (ts_run_tbl[i].type) {
        if (ts_run_tbl[i].type == ts->type) {
            ts_run = &ts_run_tbl[i];
            break;
        }
        i++;
    }

    if (!ts_prerun0(ts))
        return 0;

    n_assert(ts_run);
    if (ts_run->flags & TS_RUN_NEEDAVSET)
        poldek_load_sources(ts->ctx);

    if ((ts_run->flags & TS_RUN_NOPRERUN) == 0)
        if (!ts_prerun(ts))
            return 0;

    return ts_run->run(ts);
}
