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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include <trurl/trurl.h>

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgfl.h"
#include "pkgdir.h"
#include "pkgdir_dirindex.h"
#include "pkgdir_intern.h"
#include "poldek_util.h"
#ifdef HAVE_RPMORG
# include "pm/rpmorg/pm_rpm.h"
#else
# include "pm/rpm/pm_rpm.h"
#endif
#include "pm/pm.h"

extern struct pkgdir_module pkgdir_module_pndir;
static struct pkgdir_module *init_rpmdbcache(struct pkgdir_module *mod);

#define MOD_COMPR COMPR_ZST

struct pkgdir_module pkgdir_module_rpmdbcache = {
    init_rpmdbcache,
    0,
    "rpmdbcache",
    NULL,
    "RPM package database cache",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

static Header ldhdr_byrecno(const struct pkg *pkg, void *foo)
{
    struct pm_ctx    *pmctx;
    struct pkgdb     *db;
    struct pkgdb_it  it;
    const struct pm_dbrec *dbrec;
    Header           h = NULL;

    foo = foo;
    n_assert(pkg->recno > 0);

    pmctx = pm_new("rpm");
    db = pkgdb_open(pmctx, "/", NULL, O_RDONLY, NULL);
    if (db == NULL)
        goto l_end;

    pkgdb_it_init(db, &it, PMTAG_RECNO, (const char*)&pkg->recno);

    if ((dbrec = pkgdb_it_get(&it)) == NULL)
        goto l_end;

    // rpm's error: rpmdb_it_get_count(&it) with RECNO is always 0
    if (dbrec->hdr)
        h = pm_rpmhdr_link(dbrec->hdr);

    pkgdb_it_destroy(&it);
    pkgdb_free(db);

 l_end:
    pm_free(pmctx);
    return h;
}

static Header ldhdr_bynevr(const struct pkg *pkg, void *foo)
{
    struct pm_ctx    *pmctx;
    struct pkgdb     *db;
    struct pm_dbrec  dbrec;
    Header           h = NULL;

    foo = foo;
    pmctx = pm_new("rpm");

    db = pkgdb_open(pmctx, "/", NULL, O_RDONLY, NULL);
    if (db == NULL)
        goto l_end;

    if (pkgdb_get_package_hdr(db, pkg, &dbrec)) {
        n_assert(dbrec.hdr);
        h = pm_rpmhdr_link(dbrec.hdr);
    }

    pkgdb_free(db);

 l_end:
    pm_free(pmctx);
    return h;
}

static Header ldhdr(const struct pkg *pkg, void *foo)
{
    if (pkg->recno)
        return ldhdr_byrecno(pkg, foo);
    return ldhdr_bynevr(pkg, foo);
}

static
struct pkguinf *dbcache_load_pkguinf(tn_alloc *na, const struct pkg *pkg,
                                     void *ptr, tn_array *langs)
{
    struct pkguinf      *pkgu = NULL;
    Header               h;

    if ((h = ldhdr(pkg, ptr))) {
        pkgu = pkguinf_ldrpmhdr(na, h, langs);
        pm_rpmhdr_free(h);
    }

    return pkgu;
}

static
tn_tuple *dbcache_load_nodep_fl(tn_alloc *na, const struct pkg *pkg, void *ptr,
                                tn_array *foreign_depdirs)
{
    tn_tuple            *fl = NULL;
    Header              h;

    foreign_depdirs = foreign_depdirs;
    if ((h = ldhdr(pkg, ptr))) {
        pm_rpm_ldhdr_fl(na, &fl, h, PKGFL_ALL, pkg->name);
        if (fl && n_tuple_size(fl) == 0) {
            n_tuple_free(na, fl);
            fl = NULL;
        }
        pm_rpmhdr_free(h);
    }

    return fl;
}

static
int dbcache_load(struct pkgdir *pkgdir, unsigned ldflags)
{
    int rc;

    if ((rc = pkgdir_module_pndir.load(pkgdir, ldflags))) {
        int i;

        for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
            //if (!pkg->recno) {
            //    logn(LOGERR, "%s: not a rpmdbcache index", pkgdir_idstr(pkgdir));
            //    rc = 0;
            //    break;
            //}

            pkg->load_nodep_fl = dbcache_load_nodep_fl;
            pkg->load_pkguinf = dbcache_load_pkguinf;

        }
    }

    return rc;
}

/* symlink original dirindex as rpmdbcache's one */
static bool symlink_dirindex(const struct pkgdir *pkgdir, const char *path)
{
    bool ok = true;

    n_assert(pkgdir->dirindex);

    struct pkgdir *saved = pkgdir_malloc();
    saved->path = n_strdup(path);
    saved->idxpath = n_strdup(path);
    saved->compr = n_strdup(MOD_COMPR);
    saved->mod = &pkgdir_module_pndir;
    saved->type = "rpmdbcache";

    if (pkgdir_module_pndir.open(saved, 0)) {
        const char *src = pkgdir__dirindex_path(pkgdir->dirindex);
        char dest[PATH_MAX];
        int n = pkgdir__dirindex_make_path(dest, sizeof(dest), saved);

        const char *cachedir = vf_cachedir();
        size_t cachedir_len = strlen(cachedir);

        DBGF("cachedir %s\n", cachedir);
        DBGF("%s -> %s\n", dest, src);
        n_assert(strlen(src) > cachedir_len + 1);

        const char *linkto = &src[cachedir_len + 1];
        int rc = symlink(linkto, dest);

        if (rc != 0) {
            ok = false;
        } else {
            /* symlink .md5 too */
            char md_src[PATH_MAX];

            n = n_snprintf(&dest[n], sizeof(dest) - n, "%s", ".md5");
            n_assert(n == 4);

            n = n_snprintf(md_src, sizeof(md_src), "%s.md5", src);
            n_assert((size_t)n == strlen(src) + 4);

            DBGF("%s -> %s\n", dest, md_src);
            n_assert(strlen(md_src) > cachedir_len + 1);

            linkto = &md_src[cachedir_len + 1];
            if (symlink(linkto, dest) != 0)
                ok = false;
        }
    }

    pkgdir_free(saved);

    return ok;
}

static
int dbcache_create(struct pkgdir *pkgdir, const char *path, unsigned flags)

{
    flags |= PKGDIR_CREAT_NOPATCH | PKGDIR_CREAT_NOUNIQ |
        PKGDIR_CREAT_MINi18n | PKGDIR_CREAT_NODESC | PKGDIR_CREAT_NOFL |
        PKGDIR_CREAT_wRECNO;

    int ok = pkgdir_module_pndir.create(pkgdir, path, flags);

    /* set same mtime as rpmdb (to use symlinked dirindex) */
    poldek_util_set_mtime(path, pkgdir_mtime(pkgdir));

    /* symlink original dirindex as rpmdbcache's one */
    if (pkgdir->dirindex)
        symlink_dirindex(pkgdir, path);

    return ok;
}

struct pkgdir_module *init_rpmdbcache(struct pkgdir_module *mod)
{
    *mod = pkgdir_module_pndir;
    mod->cap_flags = PKGDIR_CAP_INTERNALTYPE;
    mod->name = "rpmdbcache";
    mod->aliases = NULL;
    mod->description = "RPM package database cache";

    mod->default_compr = MOD_COMPR;
    mod->update = NULL;
    mod->update_a = NULL;
    mod->unlink = NULL;
    mod->create = dbcache_create;
    mod->load = dbcache_load;
    return mod;
}
