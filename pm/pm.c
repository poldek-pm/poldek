/*
  Copyright (C) 2000 - 2004 Pawel A. Gajda <mis@k2.net.pl>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
  $Id$
*/

#include <trurl/nassert.h>
#include <trurl/nmalloc.h>

#include "vfile/vfile.h"
#include "i18n.h"
#include "capreq.h"
#include "poldek_ts.h"
#include "pm.h"
#include "mod.h"
#include "log.h"

struct pm_ctx *pm_new(const char *name)
{
    struct pm_ctx *ctx;
    const struct pm_module *mod;
    void *modh;
    
    if ((mod = pm_module_find(name)) == NULL)
        return NULL;

    if ((modh = mod->init()) == NULL)
        return NULL;
    
    ctx = n_malloc(sizeof(*ctx));
    ctx->mod = mod;
    ctx->modh = modh;
    return ctx;
}


void pm_free(struct pm_ctx *ctx)
{
    if (ctx->modh)
        ctx->mod->destroy(ctx->modh);
    memset(ctx, 0, sizeof(*ctx));
    free(ctx);
}

const char *pm_get_name(struct pm_ctx *ctx)
{
    return ctx->mod->name;
}


int pm_pminstall(struct pkgdb *db, tn_array *pkgs, tn_array *pkgs_toremove,
                 struct poldek_ts *ts)
{
    int i, rc;
    char path[PATH_MAX];


    rc = db->_ctx->mod->pm_install(db, pkgs, pkgs_toremove, ts);
    
    if (!rc || ts->getop(ts, POLDEK_OP_RPMTEST) ||
        ts->getop(ts, POLDEK_OP_KEEP_DOWNLOADS))
        return rc;

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        int url_type;
        
        url_type = pkg_file_url_type(pkg);
        if ((url_type & (VFURL_PATH | VFURL_UNKNOWN)))
            continue;
            
        if (pkg_localpath(pkg, path, sizeof(path), ts->cachedir)) {
            DBGF("unlink %s\n", path); 
            unlink(path);
        }
    }
    
    return rc;
}

int pm_pmuninstall(struct pkgdb *db, tn_array *pkgs, struct poldek_ts *ts)
{
    return db->_ctx->mod->pm_uninstall(db, pkgs, ts);
}

int pm_verify_signature(struct pm_ctx *ctx, const char *path, unsigned flags)
{
    if (ctx->mod->pkg_verify_sign)
        return ctx->mod->pkg_verify_sign(ctx->modh, path, flags);
    return 1;
}

time_t pm_dbmtime(struct pm_ctx *ctx, const char *path) 
{
    if (ctx->mod->dbmtime)
        return ctx->mod->dbmtime(ctx->modh, path);
    return 0;
}

char *pm_dbpath(struct pm_ctx *ctx, char *path, size_t size)
{
    if (ctx->mod->dbpath)
        return ctx->mod->dbpath(ctx->modh, path, size);
    return NULL;
}

int pm_machine_score(struct pm_ctx *ctx,
                     enum pm_machine_score_tag tag, const char *val)
{
    if (ctx->mod->machine_score == NULL)
        return 1;

    return ctx->mod->machine_score(ctx->modh, tag, val);
}


tn_array *pm_get_pmcaps(struct pm_ctx *ctx)
{
    if (ctx->mod->pm_caps)
        return ctx->mod->pm_caps(ctx->modh);
    return NULL;
}

struct pkg *pm_load_package(struct pm_ctx *ctx,
                            tn_alloc *na, const char *path, unsigned ldflags)
{
    if (ctx->mod->ldpkg)
        return ctx->mod->ldpkg(ctx->modh, na, path, ldflags);

    return NULL;
}


int pm_get_dbdepdirs(struct pm_ctx *ctx,
                     const char *rootdir, const char *dbpath,
                     tn_array *depdirs)
{
    if (ctx->mod->dbdepdirs == NULL)
        return 0;
    
    return ctx->mod->dbdepdirs(ctx->modh, rootdir, dbpath, depdirs) >= 0;
}


int pm_configure(struct pm_ctx *ctx, const char *key, void *val)
{
    if (ctx->mod->configure)
        return ctx->mod->configure(ctx->modh, key, val);
    
    return 0;
}


