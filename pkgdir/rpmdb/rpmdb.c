/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@k2.net.pl>

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
#include "pkgroup.h"

static void do_free(struct pkgdir *pkgdir);
static int do_load(struct pkgdir *pkgdir, unsigned ldflags);

static struct pkgdir_module *init_rpmdbcache(struct pkgdir_module *mod);

struct pkgdir_module pkgdir_module_rpmdb = {
    NULL,
    0,
    "rpmdb",
    NULL,
    "RPM package database",
    NULL,
    NULL, 
    NULL,
    do_load,
    NULL,
    NULL,
    NULL,
    NULL, 
    do_free,
    NULL,
    NULL,
};

static Header ldhdr(const struct pkg *pkg, void *foo, int dont_mod_data) 
{
    struct pm_ctx    *pmctx;
    struct pkgdb     *db;
    struct pkgdb_it  it;
    const struct pm_dbrec *dbrec;
    Header              h = NULL;

    foo = foo;
    n_assert(pkg->recno > 0);
    
    if (pkg->pkgdir == NULL)
        return NULL;

    pmctx = pkg->pkgdir->mod_data;
    if (dont_mod_data || pkg->pkgdir->mod_data == NULL)
        pmctx = pm_new("rpm");
    
    db = pkgdb_open(pmctx, "/", dont_mod_data ? NULL : pkg->pkgdir->idxpath,
                    O_RDONLY, NULL);
    if (db == NULL)
        return NULL;
    
    pkgdb_it_init(db, &it, PMTAG_RECNO, (const char*)&pkg->recno);
    dbrec = pkgdb_it_get(&it);
    
    // rpm's error: rpmdb_it_get_count(&it) with RECNO is always 0 
    if (dbrec->hdr)
        h = pm_rpmhdr_link(dbrec->hdr);

    pkgdb_it_destroy(&it);
    pkgdb_free(db);
    if (dont_mod_data || pkg->pkgdir->mod_data == NULL)
        pm_free(pmctx);
    return h;
}

    

static 
struct pkguinf *do_load_pkguinf(tn_alloc *na, const struct pkg *pkg,
                                void *ptr, tn_array *langs, int dont_mod_data)
{
    struct pkguinf      *pkgu = NULL;
    Header               h;

    langs = langs;               /* ignored, no support */
    if ((h = ldhdr(pkg, ptr, dont_mod_data))) {
        pkgu = pkguinf_ldrpmhdr(na, h);
        pm_rpmhdr_free(h);
    }
    
    return pkgu;
}

static
struct pkguinf *rpmdb_load_pkguinf(tn_alloc *na, const struct pkg *pkg,
                                   void *ptr, tn_array *langs)
{
    return do_load_pkguinf(na, pkg, ptr, langs, 0);
}


static 
tn_tuple *do_load_nodep_fl(tn_alloc *na, const struct pkg *pkg, void *ptr,
                           tn_array *foreign_depdirs, int dont_mod_data)
{
    tn_tuple            *fl = NULL;
    Header              h;

    foreign_depdirs = foreign_depdirs;
    if ((h = ldhdr(pkg, ptr, dont_mod_data))) {
        pm_rpm_ldhdr_fl(na, &fl, h, PKGFL_ALL, pkg->name);
        if (fl && n_tuple_size(fl) == 0) {
            n_tuple_free(na, fl);
            fl = NULL;
        }
        pm_rpmhdr_free(h);
    }
    
    return fl;
}

tn_tuple *rpmdb_load_nodep_fl(tn_alloc *na, const struct pkg *pkg, void *ptr,
                              tn_array *foreign_depdirs)
{
    return do_load_nodep_fl(na, pkg, ptr, foreign_depdirs, 0);
}



struct map_struct {
    tn_array  *pkgs;
    struct pkgdir *pkgdir;
    struct pkgroup_idx *pkgroups;
    tn_alloc  *na;
};

static
void db_map_fn(unsigned int recno, void *header, void *ptr) 
{
    struct pkg        *pkg;
    struct map_struct *ms = ptr;

    if ((pkg = pm_rpm_ldhdr(ms->na, header, NULL, 0, PKG_LDCAPREQS))) {
        char **hdr_langs;
        
        pkg->recno = recno;
        pkg->load_pkguinf = rpmdb_load_pkguinf;
        pkg->load_nodep_fl = rpmdb_load_nodep_fl;
        
        if (poldek_VERBOSE > 3)
            msgn(4, "rpmdb: ld %s", pkg_snprintf_s(pkg));
        
        if (strcmp(pkg->name, "quake2") != 0) /* broken rpmdb... */
            pkg->groupid = pkgroup_idx_update_rpmhdr(ms->pkgroups, header);
        n_array_push(ms->pkgs, pkg);

        hdr_langs = pm_rpmhdr_langs(header);
        
        if (hdr_langs) {
            int i = 0;
            while (hdr_langs[i]) {
                pkgdir__update_avlangs(ms->pkgdir, hdr_langs[i], 1);
                i++;
            }
            free(hdr_langs);
        }
        
        if (n_array_size(ms->pkgs) % 100 == 0)
            msg(3, "_.");
    }
}

static
int load_db_packages(struct pm_ctx *pmctx, struct pkgdir *pkgdir,
                     tn_array *pkgs, const char *rootdir, const char *path,
                     struct pkgroup_idx *pkgroups,
                     unsigned ldflags, tn_alloc *na) 
{
    char dbfull_path[PATH_MAX];
    struct pkgdb       *db;
    struct map_struct  ms;
    
    ldflags = ldflags;          /* unused */
    
    snprintf(dbfull_path, sizeof(dbfull_path), "%s%s",
             *(rootdir + 1) == '\0' ? "" : rootdir, path != NULL ? path : "");

    
    if ((db = pkgdb_open(pmctx, rootdir, path, O_RDONLY, NULL)) == NULL)
        return 0;

    msg(3, _("Loading db packages%s%s%s..."), *dbfull_path ? " [":"",
        dbfull_path, *dbfull_path ? "]":"");

    ms.pkgs = pkgs;
    ms.pkgdir = pkgdir;
    ms.pkgroups = pkgroups;
    ms.na = na;

    pkgdb_map(db, db_map_fn, &ms);
    pkgdb_free(db);
    msgn(3, _("_done"));
    return n_array_size(pkgs);
}

#if 0
static int do_open() 
{
    mtime_rpmdb = rpm_dbmtime(dbfull_path);
}
#endif


static
int do_load(struct pkgdir *pkgdir, unsigned ldflags)
{
    int i;
    struct pm_ctx *pmctx;
    
    n_assert(pkgdir->mod_data == NULL);
    pkgdir->mod_data = pmctx = pm_new("rpm");

    if (pkgdir->pkgroups == NULL)
        pkgdir->pkgroups = pkgroup_idx_new();
    
    if (!load_db_packages(pkgdir->mod_data, pkgdir, pkgdir->pkgs, "/",
                          pkgdir->idxpath, pkgdir->pkgroups, ldflags,
                          pkgdir->na))
        return 0;

    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
        pkg->pkgdir = pkgdir;
    }

    pkgdir->ts = pm_dbmtime(pmctx, pkgdir->idxpath);
    return n_array_size(pkgdir->pkgs);
}

static void do_free(struct pkgdir *pkgdir)
{
    if (pkgdir->mod_data)
        pm_free(pkgdir->mod_data);
}

/* rpmdbcache */

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


extern struct pkgdir_module pkgdir_module_pndir;

static
struct pkguinf *dbcache_load_pkguinf(tn_alloc *na, const struct pkg *pkg,
                                   void *ptr, tn_array *langs)
{
    return do_load_pkguinf(na, pkg, ptr, langs, 1);
}

tn_tuple *dbcache_load_nodep_fl(tn_alloc *na, const struct pkg *pkg, void *ptr,
                              tn_array *foreign_depdirs)
{
    return do_load_nodep_fl(na, pkg, ptr, foreign_depdirs, 1);
}


static
int dbcache_load(struct pkgdir *pkgdir, unsigned ldflags)
{
    int rc;

    if ((rc = pkgdir_module_pndir.load(pkgdir, ldflags))) {
        int i;
            
        for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
            if (!pkg->recno) {
                logn(LOGERR, "%s: not a rpmdbcache index", pkgdir_idstr(pkgdir));
                rc = 0;
                break;
            }

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
        PKGDIR_CREAT_MINi18n | PKGDIR_CREAT_NODESC | PKGDIR_CREAT_NOFL;

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
