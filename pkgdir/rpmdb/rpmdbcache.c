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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "pkgdir_intern.h"
#include "pm/rpm/pm_rpm.h"
#include "pm/pm.h"

extern struct pkgdir_module pkgdir_module_pndir;
static struct pkgdir_module *init_rpmdbcache(struct pkgdir_module *mod);

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
    
    DBGF("%s\n", pkg_snprintf_s(pkg));
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

    langs = langs;               /* ignored, no support */
    
    if ((h = ldhdr(pkg, ptr))) {
        pkgu = pkguinf_ldrpmhdr(na, h);
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

static
int dbcache_create(struct pkgdir *pkgdir, const char *pathname, unsigned flags)

{
    flags |= PKGDIR_CREAT_NOPATCH | PKGDIR_CREAT_NOUNIQ |
        PKGDIR_CREAT_MINi18n | PKGDIR_CREAT_NODESC | PKGDIR_CREAT_NOFL |
        PKGDIR_CREAT_wRECNO;

    return pkgdir_module_pndir.create(pkgdir, pathname, flags);
}

struct pkgdir_module *init_rpmdbcache(struct pkgdir_module *mod)
{
    *mod = pkgdir_module_pndir;
    mod->cap_flags = PKGDIR_CAP_INTERNALTYPE;
    mod->name = "rpmdbcache";
    mod->aliases = NULL;
    mod->description = "RPM package database cache";
    
    mod->update = NULL;
    mod->update_a = NULL;
    mod->unlink = NULL;
    mod->create = dbcache_create;
    mod->load = dbcache_load;
    return mod;
}
