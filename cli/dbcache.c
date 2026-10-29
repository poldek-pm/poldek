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
#include <sys/time.h>
#include <sys/errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
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

    if (cctx->dbpkgdir && !reload)
        return 0;

    if ((pkgdir = load_installed_pkgdir(cctx, reload)) == NULL)
        return 0;

    poclidek_dent_setup(cctx, POCLIDEK_INSTALLEDDIR, pkgdir->pkgs, reload);

    if (cctx->dbpkgdir)
        pkgdir_free(cctx->dbpkgdir);

    cctx->pkgs_installed = pkgdir->pkgs; /* "weak" ref */
    cctx->dbpkgdir = pkgdir;
    return 1;
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
struct pkgdir *load_rpmdbcache(const char *cachedir,
                               const char *dbpath,
                               const char *rpmdb_path, const char *dbstamp,
                               unsigned ldflags)
{
    struct pkgdir    *dir = NULL;
    const char       *lc_lang;
    char             path[PATH_MAX];

    if (mkdbcache_path(path, sizeof(path), cachedir, rpmdb_path) == NULL)
        return NULL;

    if (access(path, R_OK) == 0) {
        lc_lang = poldek_util_lc_lang("LC_MESSAGES");
        if (lc_lang == NULL)
            lc_lang = "C";

        dir = pkgdir_open_ext(path, rpmdb_path, RPMDBCACHE_PDIRTYPE, dbpath,
                              NULL, 0, lc_lang);

        DBGF("dbcache stamp=%s, db stamp=%s, %s\n", dir ? dir->dbstamp : "(null)",
             dbstamp, path);

        /* made from another state of the database, or by an older poldek */
        if (dir && (dir->dbstamp == NULL || n_str_ne(dir->dbstamp, dbstamp))) {
            pkgdir_free(dir);
            dir = NULL;
        }

        if (dir) {
            /* rpmdbcache use symlinked rpmdb's dirindex,
               (see pkgdir/rpmdb/rpmdbcache.c)              */
            ldflags |= PKGDIR_LD_DIRINDEX_NOCREATE;

            if (!pkgdir_load(dir, NULL, ldflags)) {
                pkgdir_free(dir);
                dir = NULL;
            }
        }
    }

#if 0
    /*
      outdated cache could be reused as a base for loading raw database,
      BUT there is no performance gain, as we have to load complete
      rpm headers from db
    */
    if (0 && rpmdb_mtime > mtime) {
        prev_dir = dir;
        dir = NULL;
    }
#endif

    return dir;
}

/* copied from ../misc.h */
static void *timethis_begin(void)
{
    struct timeval *tv;

    tv = n_malloc(sizeof(*tv));
    gettimeofday(tv, NULL);
    return tv;
}

static void timethis_end(int verbose_level, void *tvp, const char *prefix)
{
    struct timeval tv, *tv0 = (struct timeval *)tvp;

    gettimeofday(&tv, NULL);

    tv.tv_sec -= tv0->tv_sec;
    tv.tv_usec -= tv0->tv_usec;
    if (tv.tv_usec < 0) {
        tv.tv_sec--;
        tv.tv_usec = 1000000 + tv.tv_usec;
    }

    msgn(verbose_level, "time [%s] %ld.%06ld\n", prefix,
         (unsigned long)tv.tv_sec, (unsigned long) tv.tv_usec);
    free(tvp);
}

static
struct pkgdir *load_installed_pkgdir(struct poclidek_ctx *cctx, int reload)
{
    char             dbpath[PATH_MAX], rpmdb_path[PATH_MAX];
    struct pkgdir    *dir = NULL, *prev_dir = NULL;
    struct pm_ctx    *pmctx;
    struct poldek_ts *ts = cctx->ctx->ts; /* for short */
    unsigned         ldflags = PKGDIR_LD_NOUNIQ;

    if (ts->getop(ts, POLDEK_OP_AUTODIRDEP))
        ldflags |= PKGDIR_LD_DIRINDEX;

    pmctx = poldek_get_pmctx(cctx->ctx);

    /* not dbpath based pm? */
    if (!pm_dbpath(pmctx, dbpath, sizeof(dbpath)))
        return poldek_load_destination_pkgdir(cctx->ctx, ldflags);

    if (mkrpmdb_path(rpmdb_path, sizeof(rpmdb_path), ts->rootdir,
                     dbpath) == NULL)
        return NULL;

    //MEMINF("%s", "before rpmdbload\n");
    if (!reload) {              /* use cache */
        char *dbstamp = pm_dbstamp(pmctx, rpmdb_path);
        if (dbstamp) {
            dir = load_rpmdbcache(ts->cachedir, dbpath,
                                  rpmdb_path, dbstamp,
                                  ldflags);
            free(dbstamp);
        }
    }

    if (dir == NULL) {          /* load database */
        void *t = timethis_begin();
        DBGF("loading rpmdb %p\n", prev_dir);
        /* load whole packages here as it speeds 2x up rpmdb dirindex creation */
        ldflags |= PKGDIR_LD_FULLFLIST;
        dir = pkgdb_to_pkgdir(cctx->ctx->pmctx, ts->rootdir, NULL, ldflags,
                              "prev_pkgdir", prev_dir, NULL);

        timethis_end(3, t, "rpmdb.load");
    }

    //MEMINF("%s", "after rpmdbload\n");
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
    char         dbcache_path[PATH_MAX], *dbstamp;
    const char   *path;
    struct poldek_ts *ts = cctx->ctx->ts; /* for short */
    int          changed;

    /* not read from a database, or read from the cache */
    if (pkgdir->dbstamp == NULL || pkgdir_is_type(pkgdir, RPMDBCACHE_PDIRTYPE))
        return 1;

    /* changed since it was read, by poldek's own transaction too */
    dbstamp = pm_dbstamp(cctx->ctx->pmctx, pkgdir->idxpath);
    changed = dbstamp == NULL || n_str_ne(dbstamp, pkgdir->dbstamp);
    free(dbstamp);
    if (changed)
        return 1;

    path = mkdbcache_path(dbcache_path, sizeof(dbcache_path),
                          ts->cachedir, pkgdir->idxpath);
    if (path == NULL)
        return 0;

    DBGF("path = %s, %s, %s\n", path, pkgdir->idxpath, pkgdir->dbstamp);
    n_assert(*path != '\0');
    n_assert(strlen(path) > 10);

    return pkgdir_save_as(pkgdir, RPMDBCACHE_PDIRTYPE, path,
                          PKGDIR_CREAT_NOPATCH | PKGDIR_CREAT_NOUNIQ |
                          PKGDIR_CREAT_MINi18n | PKGDIR_CREAT_NOFL);
}
