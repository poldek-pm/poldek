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
#include "pkgset.h"
#include "pkgmisc.h"
#include "arg_packages.h"
#include "install3/ictx.h"
#include "misc.h"
#include "log.h"
#include "i18n.h"
#include "fileindex.h"

extern int poldek_conf_PROMOTE_EPOCH;
extern int poldek_conf_MULTILIB;

#define bitvect_slot_itype  uint32_t
#define bitvect_slot_size   sizeof(bitvect_slot_itype) * CHAR_BIT
#define bitvect_mask(b)     ((bitvect_slot_itype)1 << ((b) % (bitvect_slot_size)))
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

#define NOPRERUN     (1 << 3)
struct ts_run ts_run_tbl[] = {
    { POLDEK_TS_VERIFY, (ts_run_fn)ts_run_verify, NOPRERUN },
    { POLDEK_TS_INSTALL, (ts_run_fn)ts_run_install, 0 },
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
            DBGF("set (touch=%d) greedy ts=%p (ctx=%p) greedy=%d val=%d, act=%d\n",
                 touch, ts, ts->ctx, optv, on, ts->uninstall_greedy_deep);
            if (on)
                poldek_ts_xsetop(ts, POLDEK_OP_FOLLOW, 1, touch);

            if (on == 1) {
                ts->uninstall_greedy_deep++;
            } else {
                ts->uninstall_greedy_deep = on;
            }
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
    return bitvect_isset(ts->_opvect, (uint32_t)optv) > 0;
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


    ts->pms = pkgmark_set_new(ctx ? ctx->ps : NULL, 1024, 0);

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
    n_cfree(&ts->typenam);

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

tn_array *poldek_ts_get_args_asmasks(struct poldek_ts *ts, int hashed)
{
    return arg_packages_get_masks(ts->aps, hashed);
}

int poldek_ts_get_arg_count(struct poldek_ts *ts)
{
    return arg_packages_size(ts->aps);
}

/*
   -1: cannot be validated with stubs
   0: invalid
   1: valid
*/
int poldek_ts_validate_args_with_stubs(struct poldek_ts *ts, tn_array *stubpkgs)
{
    int quiet = ts->getop(ts, POLDEK_OP_CAPLOOKUP);
    tn_array *resolved = NULL;

    int rc = arg_packages__validate_with_stubs(ts->aps, stubpkgs, &resolved, quiet);
    if (rc != 1) {
        n_array_cfree(&resolved);
        return rc;
    }

    if (resolved == NULL || n_array_size(resolved) == 0)
        goto l_end;

    if (ts->pmctx == NULL && ts->ctx && ts->ctx->pmctx)
        ts->pmctx = ts->ctx->pmctx;

    if (ts->pmctx == NULL)
        goto l_end;

    void *db = NULL;
    if (ts->db == NULL) {
        ts->db = db = poldek_ts_dbopen(ts, O_RDONLY);
    }

    int freshen = ts->getop(ts, POLDEK_OP_FRESHEN);
    if (ts->db && !freshen) { /* check against db breaks upgrade foo-* */
        for (int i = 0; i < n_array_size(resolved); i++) {
            if (i3_is_pkg_installable(ts, n_array_nth(resolved, i), 1) != 1) {
                rc = 0;
            }
        }
    }

    if (db) {
        pkgdb_free(ts->db);
        ts->db = NULL;
    }

 l_end:
    n_array_cfree(&resolved);
    return rc;
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
            rc = packages_mark(ts->pms, pkgs, ts->ctx->ps, flags & TS_MARK_DEPS);
            if (!rc && ts->getop_v(ts, POLDEK_OP_NODEPS, POLDEK_OP_FORCE, 0))
                rc = 1;
        }
        n_array_free(pkgs);
    }

    return rc;
}

tn_array *poldek_ts_get_required_packages(struct poldek_ts *ts, const struct pkg *pkg) {
    return pkgset_get_required_packages(0, ts->ctx->ps, pkg);
}

tn_array *poldek_ts_get_requiredby_packages(struct poldek_ts *ts, const struct pkg *pkg) {
    return pkgset_get_requiredby_packages(0, ts->ctx->ps, pkg);
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

int poldek_ts_set_type(struct poldek_ts *ts, enum poldek_ts_type type, const char *typenam)
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

static int load_sources(struct poldek_ctx *ctx)
{
    return poldek_load_sources(ctx);
}

static int ts_run_install_dist(struct poldek_ts *ts)
{
    if (!load_sources(ts->ctx)) {
        return 0;
    }

    if (!ts_mark_arg_packages(ts, TS_MARK_DEPS | TS_MARK_CAPSINLINE)) {
        pkgmark_log_unsatisfied_dependecies(ts->pms);
        return 0;
    }

    return do_poldek_ts_install_dist(ts);
}

static int ts_run_upgrade_dist(struct poldek_ts *ts)
{
    int rc;

    n_assert(poldek_ts_issetf(ts, POLDEK_TS_UPGRADEDIST));

    if (!load_sources(ts->ctx)) {
        return 0;
    }

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

    if (!load_sources(ts->ctx)) {
        return 0;
    }

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

    pkgdb_tx_begin(ts->db, ts);

    rc = do_poldek_ts_uninstall(ts);

    if (rc && !ts->getop(ts, POLDEK_OP_TEST))
        pkgdb_tx_commit(ts->db);

    MEMINF("before dbfree");
    pkgdb_free(ts->db);
    ts->db = NULL;
    MEMINF("END");

    return rc;
}

/* just verify deps, conflicts, ordering, etc */
static int ts_run_verify(struct poldek_ts *ts)
{
    int nerr = 0;

    //n_assert(poldek_ts_issetf(ts, POLDEK_TS_VERIFY));

    if (!ts_prerun(ts))
        return 0;

    if (!load_sources(ts->ctx))
        return 0;

    unsigned flags = TS_MARK_DEPS | TS_MARK_CAPSINLINE;

    if (poldek_ts_get_arg_count(ts) == 0) { /* no args */
        arg_packages_add_pkgs(ts->aps, ts->ctx->ps->pkgs);
    } else {
        flags |= TS_MARK_VERBOSE;
    }

    ts_mark_arg_packages(ts, flags);

    tn_array *pkgs = pkgmark_get_packages(ts->pms, PKGMARK_ANY);
    if (pkgs == NULL)
        return 0;

    /* just print errors here, deps are already verified by ts_mark_arg_packages */
    if (ts->getop(ts, POLDEK_OP_VRFY_DEPS)) {
        msgn(3, _("Verifying dependencies..."));
        if (pkgmark_log_unsatisfied_dependecies(ts->pms) > 0)
            nerr++;
    }

#if 0                           /* removed feature */
    if (ts->getop(ts, POLDEK_OP_DEPGRAPH)) {
        msgn(0, _("Generating dependency graph %s..."), ts->depgraph);
        if (!packages_generate_depgraph(pkgs, ts->ctx->ps, ts->depgraph))
            nerr++;
    }
#endif

    if (ts->getop(ts, POLDEK_OP_VRFY_ORDER)) {
        if (!pkgmark_verify_package_order(ts->pms))
            nerr++;
    }

    if (ts->getop(ts, POLDEK_OP_VRFY_CNFLS)) {
        msgn(0, _("Verifying conflicts..."));
        if (!pkgmark_verify_package_conflicts(ts->pms))
            nerr++;
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
        file_index_report_semiorphans(ts->ctx->ps, pkgs);
    }

    n_array_free(pkgs);
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

    if ((ts_run->flags & NOPRERUN) == 0) {
        if (!ts_prerun(ts))
            return 0;
    }

    return ts_run->run(ts);
}
