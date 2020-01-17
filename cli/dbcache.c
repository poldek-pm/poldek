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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <signal.h>
#include <time.h>
#include <argp.h>

#include <trurl/trurl.h>
#include <sigint/sigint.h>

#include "compiler.h"
#include "pkgdir/pkgdir.h"
#include "i18n.h"
#include "log.h"
#include "cli.h"
#include "pm/pm.h"
#include "poldek_intern.h"      /* for ctx->ts->cachedir, etc, TOFIX */
#include "poldek_util.h"

#define RPMDBCACHE_PDIRTYPE "rpmdbcache"

static
struct pkgdir *load_installed_pkgdir(struct poclidek_ctx *cctx, int reload);

int poclidek__load_installed(struct poclidek_ctx *cctx, int reload)
{
    struct pkgdir *pkgdir;
    DBGF("%d\n", reload);

    if (cctx->dbpkgdir && !reload)
        return 0;

    if ((pkgdir = load_installed_pkgdir(cctx, reload)) == NULL)
        return 0;

    poclidek_dent_setup(cctx, POCLIDEK_INSTALLEDDIR, pkgdir->pkgs, reload);

    if (cctx->dbpkgdir)
        pkgdir_free(cctx->dbpkgdir);

    cctx->pkgs_installed = pkgdir->pkgs; /* "weak" ref */
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
    const char *ext = pkgdir_type_default_compr("rpmdbcache");
    n_assert(ext != NULL);

    n_snprintf(path, size, "%s/packages.%s.%s.%s", cachedir,
               RPMDBCACHE_PDIRTYPE, tmp, ext);
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


static
struct pkgdir *load_installed_pkgdir(struct poclidek_ctx *cctx, int reload)
{
    char             rpmdb_path[PATH_MAX], dbcache_path[PATH_MAX], dbpath[PATH_MAX];
    const char       *lc_lang;
    struct pkgdir    *dir = NULL, *prev_dir = NULL;
    struct pm_ctx    *pmctx;
    struct poldek_ts *ts = cctx->ctx->ts; /* for short */
    unsigned         ldflags = PKGDIR_LD_NOUNIQ;

    if (ts->getop(ts, POLDEK_OP_AUTODIRDEP))
        ldflags |= PKGDIR_LD_DIRINDEX;

    pmctx = poldek_get_pmctx(cctx->ctx);

    DBGF("reload %d\n", reload);

    /* not dbpath based pm? */
    if (!pm_dbpath(pmctx, dbpath, sizeof(dbpath)))
        return poldek_load_destination_pkgdir(cctx->ctx, ldflags);

    if (mkrpmdb_path(rpmdb_path, sizeof(rpmdb_path), ts->rootdir,
                     dbpath) == NULL)
        return NULL;

    if (mkdbcache_path(dbcache_path, sizeof(dbcache_path), ts->cachedir,
                       rpmdb_path) == NULL)
        return NULL;

    lc_lang = poldek_util_lc_lang("LC_MESSAGES");
    if (lc_lang == NULL)
        lc_lang = "C";

    ldflags |= PKGDIR_LD_DIRINDEX;

    if (!reload) {              /* use cache */
        time_t mtime_rpmdb, mtime_dbcache;
        mtime_dbcache = mtime(dbcache_path);
        mtime_rpmdb = pm_dbmtime(pmctx, rpmdb_path);

        if (mtime_rpmdb && mtime_dbcache && mtime_rpmdb < mtime_dbcache) {
            dir = pkgdir_open_ext(dbcache_path, NULL, RPMDBCACHE_PDIRTYPE,
                                  dbpath, NULL, 0, lc_lang);
            if (dir)
                if (!pkgdir_load(dir, NULL, ldflags)) {
                    pkgdir_free(dir);
                    dir = NULL;
                }
        }
        DBGF("%ld > %ld\n", mtime_rpmdb, mtime_dbcache);


        /*
           outdated cache, use it as prev_dir
           DISABLED due to no performance gain, as we have to
           load whole rpm headers from db
        */
        if (0 && mtime_rpmdb > mtime_dbcache) {
            prev_dir = dir;
            dir = NULL;
        }
    }

    if (dir == NULL) {          /* load database */
        DBGF("loading rpmdb %p\n", prev_dir);
        dir = pkgdb_to_pkgdir(cctx->ctx->pmctx, ts->rootdir, NULL, ldflags,
                              "prev_pkgdir", prev_dir, NULL);
    }

    if (dir == NULL) {
        logn(LOGERR, _("Load installed packages failed"));

    } else {
        int n = n_array_size(dir->pkgs);
        msgn(1, ngettext("%d package loaded",
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
    struct poldek_ts *ts = cctx->ctx->ts; /* for short */

    if (!pm_dbpath(cctx->ctx->pmctx, dbpath, sizeof(dbpath)))
        return 1;

    if (mkrpmdb_path(rpmdb_path, sizeof(rpmdb_path),
                     ts->rootdir, dbpath) == NULL)
        return 1;

    mtime_rpmdb = pm_dbmtime(cctx->ctx->pmctx, rpmdb_path);
    if (mtime_rpmdb > cctx->ts_dbpkgdir) /* db changed outside poldek */
        return 1;

    if (pkgdir_is_type(pkgdir, RPMDBCACHE_PDIRTYPE))
        path = pkgdir->idxpath;
    else
        path = mkdbcache_path(dbcache_path, sizeof(dbcache_path),
                              ts->cachedir, pkgdir->idxpath);

    if (path == NULL)
        return 0;

    if (mtime_rpmdb <= cctx->ts_dbpkgdir) { /* not touched, check cache */
        mtime_dbcache = mtime(path);
        if (mtime_dbcache && mtime_dbcache >= cctx->ts_dbpkgdir)
            return 1;
    }

    DBGF("path = %s, %s, %ld, %ld, %ld\n", path, pkgdir->idxpath,
         mtime_rpmdb, pkgdir->ts, mtime_dbcache);
    n_assert(*path != '\0');
    n_assert(strlen(path) > 10);
    DBGF("%s %s, %ld %ld\n", ts->cachedir, path, mtime_rpmdb, cctx->ts_dbpkgdir);

    return pkgdir_save_as(pkgdir, RPMDBCACHE_PDIRTYPE, path,
                          PKGDIR_CREAT_NOPATCH | PKGDIR_CREAT_NOUNIQ |
                          PKGDIR_CREAT_MINi18n | PKGDIR_CREAT_NOFL);
}
