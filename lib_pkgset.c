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

#include <stdio.h>
#include <fnmatch.h>

#include <trurl/nmalloc.h>
#include <trurl/nassert.h>

#include "compiler.h"
#include "vfile/vfile.h"
#include "sigint/sigint.h"

#include "pkgdir/source.h"
#include "pkgdir/pkgdir.h"
#include "pkgset.h"
#include "pkgmisc.h"
#include "conf.h"
#include "log.h"
#include "misc.h"
#include "i18n.h"
#include "poldek.h"
#include "poldek_intern.h"
#include "pm/pm.h"
#include "split.h"

extern const char *poldek_conf_PKGDIR_DEFAULT_TYPE;

int poldek__load_sources_internal(struct poldek_ctx *ctx, unsigned ps_setup_flags)
{
    struct pkgset *ps;
    struct poldek_ts *ts;
    unsigned ldflags = 0;

    n_assert(ctx->pmctx);
    n_assert(ctx->ps == NULL);

    ts = ctx->ts;

    if ((ps = pkgset_new(ctx->pmctx)) == NULL)
        return 0;

    if (pm_get_dbdepdirs(ctx->pmctx, ctx->ts->rootdir, NULL, ps->depdirs) >= 0)
        ps->flags |= PSET_RT_DBDIRS_LOADED;

    if (ctx->ts->getop(ctx->ts, POLDEK_OP_IGNORE))
        ldflags |= PKGDIR_LD_DOIGNORE;

    if (ctx->ts->getop(ctx->ts, POLDEK_OP_LDFULLFILELIST))
        ldflags |= PKGDIR_LD_FULLFLIST;

    if (ctx->ts->getop(ctx->ts, POLDEK_OP_LDALLDESC))
	ldflags |= PKGDIR_LD_ALLDESC;

#if 0 /* XXX now files are loaded on demand */
    if (strcmp(pm_get_name(ctx->pmctx), "pset") == 0)
        ldflags |= PKGDIR_LD_FULLFLIST;
#endif

    if (ctx->ts->getop(ctx->ts, POLDEK_OP_AUTODIRDEP))
        ldflags |= PKGDIR_LD_DIRINDEX;

    /* create/update stubindex by default */
    ldflags |= PKGDIR_LD_UPDATE_STUBINDEX;

    if (!pkgset_load(ps, ldflags, ctx->sources)) {
        if (poldek_verbose() > 0)
            logn(LOGWARN, _("no packages loaded"));
    }

    MEMINF("after load");

    if (ps == NULL)
        return 0;

    if (ctx->ts->getop(ctx->ts, POLDEK_OP_HOLD))
        packages_score(ps->pkgs, ctx->ts->hold_patterns, PKG_HELD);

    if (ctx->ts->getop(ctx->ts, POLDEK_OP_IGNORE))
        packages_score_ignore(ps->pkgs, ctx->ts->ign_patterns, 1);

    ctx->pkgdirs = n_ref(ps->pkgdirs);

    if (ts->getop(ts, POLDEK_OP_UNIQN))
        ps_setup_flags |= PSET_UNIQ_PKGNAME;

    pkgset_setup(ps, ps_setup_flags);

    if (ctx->ts->prifile)
        packages_set_priorities(ps->pkgs, ctx->ts->prifile);

    ctx->ps = ps;
    MEMINF("after ps setup");

    return 1;
}

tn_array *poldek_load_stubs(struct poldek_ctx *ctx)
{
    tn_array *sources = ctx->sources;
    int i;

    if (!poldek__is_setup_done(ctx)) {
        logn(LOGERR | LOGDIE, "poldek_setup() call is a must...");
    }

    n_array_isort_ex(sources, (tn_fn_cmp)source_cmp_pri);
    tn_array *stubpkgs = pkgs_array_new(4096);

    for (i=0; i < n_array_size(sources); i++) {
        struct source *src = n_array_nth(sources, i);

        if (src->flags & PKGSOURCE_NOAUTO)
            continue;

        if (src->type == NULL)
            source_set_type(src, poldek_conf_PKGDIR_DEFAULT_TYPE);

        tn_array *pkgs = source_stubload(src);
        if (pkgs == NULL) {     /* need all stubs or nothing */
            n_array_cfree(&stubpkgs);
            return 0;
        }

        while (n_array_size(pkgs) > 0) {
            struct pkg *pkg = n_array_shift(pkgs);

            if (pkg_is_scored(pkg, PKG_IGNORED))
                pkg_free(pkg);
            else
                n_array_push(stubpkgs, pkg);
        }
    }
    n_array_sort(stubpkgs);
    n_array_isort_ex(stubpkgs, (tn_fn_cmp)pkg_cmp_name_evr_arch_rev_srcpri);

    struct poldek_ts *ts = ctx->ts;
    packages_uniq(stubpkgs, ts->getop(ts, POLDEK_OP_UNIQN) ? true : false);

    return stubpkgs;
}

tn_array *poldek_get_avail_packages(struct poldek_ctx *ctx)
{
    return poldek_search_avail_packages(ctx, POLDEK_ST_RECNO, NULL);
}

tn_array *poldek_search_avail_packages(struct poldek_ctx *ctx,
                                       enum poldek_search_tag tag,
                                       const char *value)
{
    if (!poldek_load_sources(ctx))
        return NULL;

    return pkgset_search(ctx->ps, (enum pkgset_search_tag)tag, value);
}


tn_array *poldek_get_sources(struct poldek_ctx *ctx)
{
    return ctx->sources ? n_ref(ctx->sources) : NULL;
}

tn_array *poldek_get_pkgdirs(struct poldek_ctx *ctx)
{
    return ctx->pkgdirs ? n_ref(ctx->pkgdirs) : NULL;
}

struct pkgdb;
struct pkgdb *poldek_open_installeddb(struct poldek_ctx *ctx)
{
    return poldek_ts_dbopen(ctx->ts, 0);
}
