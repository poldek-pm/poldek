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

#define PKGDIR_INTERNAL
#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgdir.h"
#include "rpm.h"
#include "pkgdb.h"


static
int do_load(struct pkgdir *pkgdir, tn_array *depdirs, unsigned ldflags);

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


static
void db_map_fn(unsigned int recno, void *header, void *pkgs) 
{
    struct pkg        *pkg;
    char              nevr[1024];
    int               len;

    recno = recno;
    if ((pkg = pkg_ldhdr(header, "db", 0, PKG_LDNEVR))) {
        pkg_snprintf(nevr, sizeof(nevr), pkg);
    
        len = strlen(nevr);
        n_array_push(pkgs, pkg);
        if (n_array_size(pkgs) % 100 == 0)
            msg(1, "_.");
    }
}

static
int load_db_packages(tn_array *pkgs, const char *rootdir, const char *path,
                     unsigned ldflags) 
{
    char dbfull_path[PATH_MAX];
    struct pkgdb *db;

    ldflags = ldflags;          /* unused */
    
    snprintf(dbfull_path, sizeof(dbfull_path), "%s%s",
             *(rootdir + 1) == '\0' ? "" : rootdir, path != NULL ? path : "");

    
    if ((db = pkgdb_open(rootdir, path, O_RDONLY)) == NULL)
        return 0;

    msg(1, _("Loading db packages%s%s%s..."), *dbfull_path ? " [":"",
        dbfull_path, *dbfull_path ? "]":"");
    
    rpm_dbmap(db->dbh, db_map_fn, pkgs);
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
int do_load(struct pkgdir *pkgdir, tn_array *depdirs, unsigned ldflags)
{
    depdirs = depdirs;
    
    if (!load_db_packages(pkgdir->pkgs, "/", pkgdir->idxpath, ldflags))
        return 0;
    
    pkgdir->ts = rpm_dbmtime(pkgdir->idxpath);
    return n_array_size(pkgdir->pkgs);
}

