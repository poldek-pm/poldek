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


#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>

#define PKGDIR_INTERNAL
#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgdir.h"
#include "rpm/rpm_pkg_ld.h"
#include "rpm/rpmdb_it.h"
#include "rpm/rpmhdr.h"
#include "pkgdb.h"

struct pkg_data {
};

    

static
int do_load(struct pkgdir *pkgdir, unsigned ldflags);

struct pkgdir_module pkgdir_module_rpmdb = {
    0,
    "rpmdb",
    NULL, 
    "",
    NULL,
    do_load,
    NULL,
    NULL,
    NULL,
    NULL, 
    NULL,
    NULL
};

static Header ldhdr(const struct pkg *pkg, struct pkg_data *pd) 
{
    struct pkgdb        *db;
    struct rpmdb_it     it;
    const struct dbrec  *dbrec;
    Header              h;

    pd = pd;
    n_assert(pkg->recno > 0);
    
    if (pkg->pkgdir == NULL)
        return NULL;
    
    if ((db = pkgdb_open("/", pkg->pkgdir->idxpath, O_RDONLY)) == NULL)
        return NULL;
    
    rpmdb_it_init(db->dbh, &it, RPMITER_RECNO, (const char*)&pkg->recno);
    dbrec = rpmdb_it_get(&it);
    
    // rpm's error: rpmdb_it_get_count(&it) is always 0 
    if (dbrec->h)
        h = headerLink(dbrec->h);

    rpmdb_it_destroy(&it);
    pkgdb_free(db);
    
    return h;
}

    

static 
struct pkguinf *load_pkguinf(const struct pkg *pkg, void *ptr)
{
    struct pkguinf      *pkgu = NULL;
    Header               h;

    if ((h = ldhdr(pkg, ptr))) {
        pkgu = pkguinf_ldhdr(h);
        headerFree(h);
    }
    
    return pkgu;
}

static 
tn_array *load_nodep_fl(const struct pkg *pkg, void *ptr,
                              tn_array *foreign_depdirs)
{
    tn_array            *fl = NULL;
    Header              h;

    foreign_depdirs = foreign_depdirs;
    
    if ((h = ldhdr(pkg, ptr))) {
        fl = pkgfl_array_new(32);
        pkgfl_ldhdr(fl, h, PKGFL_ALL, pkg->name);
        if (n_array_size(fl) == 0) {
            n_array_free(fl);
            fl = NULL;
        }
        headerFree(h);
    }
    
    return fl;
}


struct map_struct {
    tn_array  *pkgs;
    tn_hash   *langs;
};

static
void db_map_fn(unsigned int recno, void *header, void *ptr) 
{
    struct pkg        *pkg;
    struct map_struct *ms;
    //char              nevr[1024];
    //int               len;

    if ((pkg = pkg_ldrpmhdr(header, "db", 0, PKG_LDNEVR))) {
        char **hdr_langs;
        
        ms = ptr;
        //pkg_snprintf(nevr, sizeof(nevr), pkg);
        //len = strlen(nevr);
        pkg->recno = recno;
        pkg->load_pkguinf = load_pkguinf;
        pkg->load_nodep_fl = load_nodep_fl;
        n_array_push(ms->pkgs, pkg);

        hdr_langs = rpmhdr_langs(header);
        
        if (hdr_langs) {
            int i = 0;
            while (hdr_langs[i]) {
                if (!n_hash_exists(ms->langs, hdr_langs[i]))
                    n_hash_insert(ms->langs, hdr_langs[i], NULL);
                i++;
            }
            free(hdr_langs);
        }
        
        if (n_array_size(ms->pkgs) % 100 == 0)
            msg(1, "_.");
    }
}

static
int load_db_packages(tn_array *pkgs, const char *rootdir, const char *path,
                     tn_hash *avlangs, unsigned ldflags) 
{
    char dbfull_path[PATH_MAX];
    struct pkgdb       *db;
    struct map_struct  ms;
    
    ldflags = ldflags;          /* unused */
    
    snprintf(dbfull_path, sizeof(dbfull_path), "%s%s",
             *(rootdir + 1) == '\0' ? "" : rootdir, path != NULL ? path : "");

    
    if ((db = pkgdb_open(rootdir, path, O_RDONLY)) == NULL)
        return 0;

    msg(1, _("Loading db packages%s%s%s..."), *dbfull_path ? " [":"",
        dbfull_path, *dbfull_path ? "]":"");

    ms.pkgs = pkgs;
    ms.langs = avlangs;
    
    rpm_dbmap(db->dbh, db_map_fn, &ms);
    pkgdb_free(db);

    
    msgn(1, _("_done"));

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
    
    if (!load_db_packages(pkgdir->pkgs, "/", pkgdir->idxpath,
                          pkgdir->avlangs_h, ldflags))
        return 0;

    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
        pkg->pkgdir = pkgdir;
    }
    
    
    pkgdir->ts = rpm_dbmtime(pkgdir->idxpath);
    return n_array_size(pkgdir->pkgs);
}

