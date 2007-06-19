/*
  Copyright (C) 2000 - 2005 Pawel A. Gajda <mis@k2.net.pl>

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
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <trurl/trurl.h>
#include <sigint/sigint.h>

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

static int do_open(struct pkgdir *pkgdir, unsigned flags);
static void do_free(struct pkgdir *pkgdir);
static int do_load(struct pkgdir *pkgdir, unsigned ldflags);

struct pkgdir_module pkgdir_module_rpmdb = {
    NULL,
    0,
    "rpmdb",
    NULL,
    "RPM package database",
    NULL,
    NULL, 
    do_open,
    do_load,
    NULL,
    NULL,
    NULL,
    NULL, 
    do_free,
    NULL,
    NULL,
};

static Header ldhdr(const struct pkg *pkg, void *foo) 
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
    if (pkg->pkgdir->mod_data == NULL) /* pkgdir are saved now  */
        pmctx = pm_new("rpm");
    
    db = pkgdb_open(pmctx, "/", pkg->pkgdir->idxpath,
                    O_RDONLY, NULL);
    if (db == NULL)
        return NULL;
    
    pkgdb_it_init(db, &it, PMTAG_RECNO, (const char*)&pkg->recno);
    dbrec = pkgdb_it_get(&it);
    
    // rpm's error: rpmdb_it_get_count(&it) with RECNO is always 0 
    if (dbrec && dbrec->hdr)
        h = pm_rpmhdr_link(dbrec->hdr);

    pkgdb_it_destroy(&it);
    pkgdb_free(db);
    if (pkg->pkgdir->mod_data == NULL)
        pm_free(pmctx);
    return h;
}

    

static 
struct pkguinf *load_pkguinf(tn_alloc *na, const struct pkg *pkg,
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
tn_tuple *load_nodep_fl(tn_alloc *na, const struct pkg *pkg, void *ptr,
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

/* load package from header and add it to pkgdir */
static
int load_package(unsigned int recno, void *header, struct pkgdir *pkgdir) 
{
    struct pkg  *pkg;
    char        **langs;
        
    pkg = pm_rpm_ldhdr(pkgdir->na, header, NULL, 0, PKG_LDCAPREQS);
    
    if (pkg == NULL)
        return 0;
    
    pkg->recno = recno;
    pkg->load_pkguinf = load_pkguinf;
    pkg->load_nodep_fl = load_nodep_fl;
    
    if (poldek_VERBOSE > 3)
        msgn(4, "rpmdb: ld %s", pkg_id(pkg));
    
#if 0                           /* hope outdated  */
    if (strcmp(pkg->name, "quake2") != 0) /* broken rpmdb... */
        pkg->groupid = pkgroup_idx_update_rpmhdr(pkgdir->pkgroups, header);
#endif
    n_array_push(pkgdir->pkgs, pkg);

    if ((langs = pm_rpmhdr_langs(header))) {
        int i = 0;
        while (langs[i])
            pkgdir__update_avlangs(pkgdir, langs[i++], 1);
        free(langs);
    }
    
    return 1;
}

static
int load_db_packages(struct pm_ctx *pmctx, struct pkgdir *pkgdir,
                     const char *rootdir) 
{
    struct pkgdb       *db;
    struct pkgdb_it    it;
    const struct pm_dbrec *dbrec;
    char               dbfull_path[PATH_MAX];    
    int                n;
    
    snprintf(dbfull_path, sizeof(dbfull_path), "%s%s",
             *(rootdir + 1) == '\0' ? "" : rootdir,
             pkgdir->idxpath != NULL ? pkgdir->idxpath : "");

    db = pkgdb_open(pmctx, rootdir, pkgdir->idxpath, O_RDONLY, NULL);
    if (db == NULL)
        return 0;

    msg(3, _("Loading db packages%s%s%s..."), *dbfull_path ? " [":"",
        dbfull_path, *dbfull_path ? "]":"");

    pkgdb_it_init(db, &it, PMTAG_RECNO, NULL);

    n = 0;
    while ((dbrec = pkgdb_it_get(&it))) {
        if (dbrec->hdr) {
            if (load_package(dbrec->recno, dbrec->hdr, pkgdir))
                n++;
        }
        msg(1, "_.");
        if (sigint_reached()) {
            n = 0;
            break;
        }
    }
    msgn(1, "_done");
    
    pkgdb_it_destroy(&it);
    pkgdb_free(db);

    if (n == 0)
        n_array_clean(pkgdir->pkgs);
    
    return n_array_size(pkgdir->pkgs);
}

/* just check if database exists */
static
int do_open(struct pkgdir *pkgdir, unsigned flags)
{
    struct pm_ctx *pmctx;
    struct pkgdb  *db;


    flags = flags;          /* unused */
    
    n_assert(pkgdir->mod_data == NULL);
    pkgdir->mod_data = pmctx = pm_new("rpm");
    
    if ((db = pkgdb_open(pmctx, "/", pkgdir->idxpath, O_RDONLY, NULL)) == NULL)
        return 0;

    pkgdb_free(db);
    return 1;
}


static
int do_load(struct pkgdir *pkgdir, unsigned ldflags)
{
    int i;
    struct pm_ctx *pmctx = pkgdir->mod_data;

    ldflags = ldflags;          /* unused */

    n_assert(pmctx);
    if (pkgdir->pkgroups == NULL)
        pkgdir->pkgroups = pkgroup_idx_new();

    pkgdir->ts = pm_dbmtime(pmctx, pkgdir->idxpath);

    DBGF("prev_dir %p\n", pkgdir->prev_pkgdir);
    
    if (!load_db_packages(pkgdir->mod_data, pkgdir, "/"))
        return 0;
    
    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
        pkg->pkgdir = pkgdir;
    }

    return n_array_size(pkgdir->pkgs);
}

static void do_free(struct pkgdir *pkgdir)
{
    if (pkgdir->mod_data)
        pm_free(pkgdir->mod_data);
}

