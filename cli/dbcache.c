/* 
  Copyright (C) 2000 - 2004 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <signal.h>
#include <time.h>
#include <argp.h>
#include <time.h>

#include <trurl/trurl.h>
#include <sigint/sigint.h>

#include "pkgdir/pkgdir.h"
#include "i18n.h"
#include "log.h"
#include "cli.h"
#include "pm/pm.h"
#include "poldek_intern.h"      /* for ctx->ts->cachedir, etc, TOFIX */
#include "poldek_util.h"

static
struct pkgdir *load_installed_pkgdir(struct poclidek_ctx *cctx, int reload);

int poclidek_load_installed(struct poclidek_ctx *cctx, int reload) 
{
    struct pkgdir *pkgdir;
    struct pkg_dent *dent;
        
    if ((pkgdir = load_installed_pkgdir(cctx, reload)) == NULL)
        return 0;

    if ((dent = poclidek_dent_find(cctx, POCLIDEK_INSTALLEDDIR)) == NULL)
        dent = pkg_dent_adddir(cctx, cctx->rootdir, POCLIDEK_INSTALLEDDIR);
        
    pkg_dent_add_pkgs(cctx, dent, pkgdir->pkgs);
    cctx->pkgs_installed = n_ref(pkgdir->pkgs);
    n_array_ctl(cctx->pkgs_installed, TN_ARRAY_AUTOSORTED);
    cctx->dbpkgdir = pkgdir;
    cctx->ts_dbpkgdir = pkgdir->ts;
    return 1;
}

static time_t mtime(const char *pathname) 
{
    struct stat st;
    //printf("stat %s %d\n", pathname, stat(pathname, &st));
    if (stat(pathname, &st) != 0)
        return 0;

    return st.st_mtime;
}

static
char *mkdbcache_path(char *path, size_t size, const char *cachedir,
                     const char *dbfull_path)
{
    int len;
    char tmp[PATH_MAX], *p;
    
    n_assert(cachedir);
    if (*dbfull_path == '/')
        dbfull_path++;

    len = n_snprintf(tmp, sizeof(tmp), "%s", dbfull_path);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
        len--;
    }
    n_assert(len);
    p = tmp;

    while (*p) {
        if (*p == '/')
            *p = '.';
        p++;
    }

    snprintf(path, size, "%s/packages.dbcache.%s.gz", cachedir, tmp);
    return path;
}


static
char *mkrpmdb_path(char *path, size_t size, const char *root, const char *dbpath)
{
    *path = '\0';
    n_snprintf(path, size, "%s%s",
               root ? (*(root + 1) == '\0' ? "" : root) : "",
               dbpath != NULL ? dbpath : "");
    return *path ? path : NULL;
}

#define RPMDBCACHE_PDIRTYPE "pndir"



static
struct pkgdir *load_installed_pkgdir(struct poclidek_ctx *cctx, int reload) 
{
    char            rpmdb_path[PATH_MAX], dbcache_path[PATH_MAX], dbpath[PATH_MAX];
    const char      *lc_lang;
    struct pkgdir   *dir = NULL;
    struct pmctx    *pmctx;

    pmctx = poldek_get_pmctx(cctx->ctx);
    
    if (!pm_dbpath(pmctx, dbpath, sizeof(dbpath)))
        return poldek_load_destination_pkgdir(cctx->ctx);
    
    if (mkrpmdb_path(rpmdb_path, sizeof(rpmdb_path),
                     cctx->ctx->ts->rootdir, dbpath) == NULL)
        return NULL;

    
    if (mkdbcache_path(dbcache_path, sizeof(dbcache_path),
                       cctx->ctx->ts->cachedir, rpmdb_path) == NULL)
        return NULL;

    lc_lang = poldek_util_lc_lang("LC_MESSAGES");
    if (lc_lang == NULL) 
        lc_lang = "C";
    
    if (!reload) {              /* use cache */
        time_t mtime_rpmdb, mtime_dbcache;
        mtime_dbcache = mtime(dbcache_path);
        mtime_rpmdb = pm_dbmtime(pmctx, rpmdb_path);
        if (mtime_rpmdb && mtime_dbcache && mtime_rpmdb < mtime_dbcache) {
            dir = pkgdir_open_ext(dbcache_path, NULL, RPMDBCACHE_PDIRTYPE,
                                  dbpath, NULL, 0, lc_lang);
            if (dir)
                if (!pkgdir_load(dir, NULL, 0)) {
                    pkgdir_free(dir);
                    dir = NULL;
                }
        }
    }
    
    if (dir == NULL)
        dir = poldek_load_destination_pkgdir(cctx->ctx);

    if (dir == NULL)
        logn(LOGERR, _("Load installed packages failed"));
    
    else {
        int n = n_array_size(dir->pkgs);
        msgn(2, ngettext("%d package loaded",
                         "%d packages loaded", n), n);
    } 
    
    return dir;
}


int poclidek_save_installedcache(struct poclidek_ctx *cctx,
                                 struct pkgdir *pkgdir)
{
    time_t       mtime_rpmdb, mtime_dbcache;
    char         rpmdb_path[PATH_MAX], dbcache_path[PATH_MAX], dbpath[PATH_MAX];
    const char   *path;

    if (!pm_dbpath(cctx->ctx->pmctx, dbpath, sizeof(dbpath)))
        return 1;

    if (mkrpmdb_path(rpmdb_path, sizeof(rpmdb_path),
                     cctx->ctx->ts->rootdir, dbpath) == NULL)
        return 1;

    mtime_rpmdb = pm_dbmtime(cctx->ctx->pmctx, rpmdb_path);
    if (mtime_rpmdb > cctx->ts_dbpkgdir) /* db changed outside poldek */
        return 1;
    
    if (pkgdir_is_type(pkgdir, RPMDBCACHE_PDIRTYPE))
        path = pkgdir->idxpath;
    else 
        path = mkdbcache_path(dbcache_path, sizeof(dbcache_path),
                              cctx->ctx->ts->cachedir, pkgdir->idxpath);
    
    if (path == NULL)
        return 0;
    
    if (mtime_rpmdb <= cctx->ts_dbpkgdir) { /* not touched, check cache */
        mtime_dbcache = mtime(path);
        if (mtime_dbcache && mtime_dbcache >= cctx->ts_dbpkgdir)
            return 1;
    }
    
    DBGF("path = %s, %s, %d, %d, %d\n", path, pkgdir->idxpath,
         mtime_rpmdb, pkgdir->ts, mtime_dbcache);
    n_assert(*path != '\0');
    n_assert(strlen(path) > 10);
    DBGF("%s %s, %d %d\n", cctx->ctx->ts->cachedir, path,
         mtime_rpmdb, cctx->ts_dbpkgdir);
    msg(0, "\n");
    return pkgdir_save_as(pkgdir, RPMDBCACHE_PDIRTYPE, path,
                          PKGDIR_CREAT_NOPATCH | PKGDIR_CREAT_NOUNIQ |
                          PKGDIR_CREAT_MINi18n | PKGDIR_CREAT_NODESC);
    
}

