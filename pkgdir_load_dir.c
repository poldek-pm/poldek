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

#ifdef HAVE_GETLINE
# define _GNU_SOURCE 1
#else
# error "getline() is needed, sorry"
#endif

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>

#include <vfile/vfile.h>

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "rpmadds.h"
#include "pkgdir.h"
#include "pkg.h"
#include "h2n.h"
#include "pkgroup.h"
#include "pkgdb.h"

static char *std_depdirs[] = { "bin", "etc", "lib", "sbin", "usr/X11R6/bin",
                               "usr/bin", "usr/lib", "usr/sbin", NULL };

static
int load_dir(const char *dirpath, tn_array *pkgs, struct pkgroup_idx *pkgroups)
{
    struct dirent  *ent;
    struct stat    st;
    DIR            *dir;
    int            n;
    char           *sepchr = "/";

    
    if ((dir = opendir(dirpath)) == NULL) {
	logn(LOGERR, "opendir %s: %m", dirpath);
	return -1;
    }
    
    if (dirpath[strlen(dirpath) - 1] == '/')
        sepchr = "";
    
    n = 0;
    while( (ent = readdir(dir)) ) {
        char path[PATH_MAX];
        
        if (fnmatch("*.rpm", ent->d_name, 0) != 0) 
            continue;

        if (fnmatch("*.src.rpm", ent->d_name, 0) == 0) 
            continue;

        snprintf(path, sizeof(path), "%s%s%s", dirpath, sepchr, ent->d_name);
        
        if (stat(path, &st) != 0) {
            logn(LOGERR, "stat %s: %m", path);
            continue;
        }
        
        if (S_ISREG(st.st_mode)) {
            Header h;
            FD_t fdt;
            
            if ((fdt = Fopen(path, "r")) == NULL) {
                logn(LOGERR, "open %s: %s", path, rpmErrorString());
                continue;
            }
            
            if (rpmReadPackageHeader(fdt, &h, NULL, NULL, NULL) != 0) {
                logn(LOGWARN, "%s: read header failed, skipped", path);
                
            } else {
                struct pkg *pkg;
                 

                if (headerIsEntry(h, RPMTAG_SOURCEPACKAGE)) /* omit src.rpms */
                    continue;

                if ((pkg = pkg_ldhdr(h, path, st.st_size, PKG_LDWHOLE))) {
                    pkg->pkg_pkguinf = pkguinf_ldhdr(h);
                    pkg_set_ldpkguinf(pkg);
                    n_array_push(pkgs, pkg);
                    pkg->groupid = pkgroup_idx_update(pkgroups, h);
                    n++;
                }
                headerFree(h);
            }
            Fclose(fdt);
            
            if (n && n % 200 == 0) 
                msg(1, "_%d..", n);
            	
        }
    }

    if (n && n > 200)
        msg(1, "_%d\n", n);
    closedir(dir);
    return n;
}


static
int load_header_list(const char *path, tn_array *pkgs,
                     struct pkgroup_idx *pkgroups)
{
    struct vfile         *vf;
    struct pkg           *pkg;
    Header               h;
    int                  n = 0;
    unsigned             vfmode = VFM_RO | VFM_CACHE | VFM_UNCOMPR;
    

    if ((vf = vfile_open(path, VFT_RPMIO, vfmode)) == NULL)
        return -1;
    
    while ((h = headerRead(vf->vf_fdt, HEADER_MAGIC_YES))) {
        if (headerIsEntry(h, RPMTAG_SOURCEPACKAGE)) { /* omit src.rpms */
            headerFree(h);
            continue;
        }
        
        if ((pkg = pkg_ldhdr(h, path, 0, PKG_LDWHOLE))) {
            pkg->pkg_pkguinf = pkguinf_ldhdr(h);
            pkg_set_ldpkguinf(pkg);
            n_array_push(pkgs, pkg);
            pkg->groupid = pkgroup_idx_update(pkgroups, h);
            n++;
        }
        	
        headerFree(h);
    }
    
    vfile_close(vf);
    return n;
}


static void is_depdir_req(const struct capreq *req, tn_array *depdirs) 
{
    if (capreq_is_file(req)) {
        const char *reqname;
        char *p;
        int reqlen;
        
        reqname = capreq_name(req);
        reqlen = strlen(reqname);
        
        p = strrchr(reqname, '/');
        
        if (p != reqname) {
            char *dirname;
            int len;

            len = p - reqname;
            dirname = alloca(len + 1);
            memcpy(dirname, reqname, len);
            dirname[len] = '\0';
            p = dirname;

            
        } else if (*(p+1) != '\0') {
            char *dirname;
            dirname = alloca(reqlen + 1);
            memcpy(dirname, reqname, reqlen + 1);
            p = dirname;
        }

        if (*(p+1) != '\0' && *p == '/')
            p++;
        
        if (n_array_bsearch(depdirs, p) == NULL) {
            n_array_push(depdirs, n_strdup(p));
            n_array_sort(depdirs);
        }
    }
}


static void pkgdir_setup_depdirs(struct pkgdir *pkgdir) 
{
    int i;

    n_assert(pkgdir->depdirs == NULL);
    pkgdir->depdirs = n_array_new(16, free, (tn_fn_cmp)strcmp);
    n_array_ctl(pkgdir->depdirs, TN_ARRAY_AUTOSORTED);

    i = 0;
    while (std_depdirs[i] != NULL)
        n_array_push(pkgdir->depdirs, n_strdup(std_depdirs[i++]));

    n_array_sort(pkgdir->depdirs);
    for (i=0; i<n_array_size(pkgdir->pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);

        if (pkg->reqs) 
            n_array_map_arg(pkg->reqs, (tn_fn_map2) is_depdir_req,
                            pkgdir->depdirs);
    }
}

#define LOAD_DIR 1         /* scan directory */
#define LOAD_HDL 2         /* load package header file (named hdlist in RH) */

static
struct pkgdir *pkgdir_load_dir_or_hdl(const char *name, const char *path, int t)
{
    struct pkgdir        *pkgdir = NULL;
    tn_array             *pkgs;
    struct pkgroup_idx   *pkgroups;
    int                  n = 0;
    unsigned             pkgdir_flags = 0;
    char                 *pkgdir_path = NULL;
    

    pkgs = pkgs_array_new(1024);
    pkgroups = pkgroup_idx_new();

    switch (t) {
        case LOAD_DIR:
            n = load_dir(path, pkgs, pkgroups);
            pkgdir_flags = PKGDIR_LDFROM_DIR;
            pkgdir_path = n_strdup(path);
            break;

        case LOAD_HDL:
            n = load_header_list(path, pkgs, pkgroups);
            pkgdir_flags = PKGDIR_LDFROM_HDL;
            pkgdir_path = pkgdir_setup_pkgprefix(path);
            break;

        default:
            n_assert(0);
            break;
    }

    if (n < 0) {
        n_array_free(pkgs);
        pkgroup_idx_free(pkgroups);
        return NULL;
    }
    
    pkgdir = pkgdir_malloc();
    pkgdir->name = n_strdup(name);
    pkgdir->path = pkgdir_path;
    pkgdir->pkgs = pkgs;
    pkgdir->pkgroups = pkgroups;
    
    if (n_array_size(pkgs)) {
        int i;
            
        for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
            pkg->pkgdir = pkgdir;
        }
            
        pkgdir_setup_depdirs(pkgdir);
        pkgdir_uniq(pkgdir);
    }
    
    return pkgdir;
}

struct pkgdir *pkgdir_load_dir(const char *name, const char *path)
{
    return pkgdir_load_dir_or_hdl(name, path, LOAD_DIR);
}


struct pkgdir *pkgdir_load_hdl(const char *name, const char *path,
                               const char *prefix)
{
    struct pkgdir *pkgdir;
    
    if ((pkgdir = pkgdir_load_dir_or_hdl(name, path, LOAD_HDL)) && prefix) {
        free(pkgdir->path);
        pkgdir->path = n_strdup(prefix);
    }
    
    return pkgdir;
}


static
void db_map_fn(unsigned int recno, void *header, void *pkgs) 
{
    struct pkg        *pkg;
    char              nevr[1024];
    int               len;

    recno = recno;
    pkg = pkg_ldhdr(header, "db", 0, PKG_LDNEVR);
    pkg_snprintf(nevr, sizeof(nevr), pkg);
    
    len = strlen(nevr);
    n_array_push(pkgs, pkg);
    if (n_array_size(pkgs) % 100 == 0)
        msg(1, "_.");
}

static tn_array *load_db_packages(const char *rootdir, const char *path) 
{
    char dbfull_path[PATH_MAX];
    char *dbpath = NULL;
    struct pkgdb *db;
    tn_array *pkgs;

    if (path == NULL) {
        path = rpm_get_dbpath();
        dbpath = (char*)path;
    }
    
    snprintf(dbfull_path, sizeof(dbfull_path), "%s%s",
             *(rootdir + 1) == '\0' ? "" : rootdir, path != NULL ? path : "");

    if (dbpath)
        free(dbpath);
    
    pkgs = pkgs_array_new(1024);
    if ((db = pkgdb_open(rootdir, path, O_RDONLY)) == NULL)
        return NULL;

    msg(1, _("Loading db packages%s%s%s..."), *dbfull_path ? " [":"",
        dbfull_path, *dbfull_path ? "]":"");
    
    rpm_dbmap(db->dbh, db_map_fn, pkgs);
    pkgdb_free(db);
    
    n_array_sort(pkgs);
    msgn(1, _("_done"));
    
    msgn(1, ngettext("%d package loaded",
                     "%d packages loaded", n_array_size(pkgs)),
        n_array_size(pkgs));

    return pkgs;
}


struct pkgdir *pkgdir_load_db(const char *rootdir, const char *path) 
{
    struct pkgdir        *pkgdir = NULL;
    tn_array             *pkgs;
    char                 dbfull_path[PATH_MAX];

    if ((pkgs = load_db_packages(rootdir, path)) == NULL)
        return NULL;
    
    snprintf(dbfull_path, sizeof(dbfull_path), "%s%s",
             *(rootdir + 1) == '\0' ? "" : rootdir,
             path != NULL ? path : "");
        
    pkgdir = pkgdir_malloc();
    pkgdir->name = n_strdup("db");
    pkgdir->path = n_strdup(dbfull_path);
    pkgdir->pkgs = pkgs;
        
    pkgdir->pkgroups = NULL;
    pkgdir->flags = PKGDIR_LDFROM_DB;
        
    if (n_array_size(pkgs)) {
        int i;
            
        for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
            pkg->pkgdir = pkgdir;
        }
            
        pkgdir_setup_depdirs(pkgdir);
    }

    return pkgdir;
}

