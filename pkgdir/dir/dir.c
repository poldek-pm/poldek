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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>
#include <sys/param.h>          /* for PATH_MAX */

#include <trurl/trurl.h>

#include <vfile/vfile.h>

#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pm/rpm/pm_rpm.h"
#include "pkgdir.h"
#include "pkgdir_intern.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgroup.h"

static
int do_load(struct pkgdir *pkgdir, unsigned ldflags);

static char *aliases[] = { "rpmdir", NULL };

struct pkgdir_module pkgdir_module_dir = {
    NULL, 
    PKGDIR_CAP_NOPREFIX, 
    "dir",
    (char **)aliases,
    "Dynamic index built by scanning directory for packages",
    NULL,
    NULL,
    NULL,
    do_load,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static tn_hash *build_mtime_index(tn_array *pkgs) 
{
    tn_hash *ht;
    int i;
    
    ht = n_hash_new(n_array_size(pkgs), NULL);

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        char key[2048];

        if (pkg->fmtime && pkg->fsize) {
            n_snprintf(key, sizeof(key), "%s-%d-%d", pkg_filename_s(pkg),
                       pkg->fmtime, pkg->fsize);
            n_hash_insert(ht, key, pkg);
        }
    }
    
    if (n_hash_size(ht) == 0) {
        n_hash_free(ht);
        return NULL;
    }
    
    return ht;
}

struct pkg *search_in_mtime_index(tn_hash *mtime_index, const char *fn,
                                  struct stat *st)
{
    char key[2048];

    n_snprintf(key, sizeof(key), "%s-%d-%d", fn, st->st_mtime, st->st_size);
    return n_hash_get(mtime_index, key);
}

struct pkg *search_in_prev(struct pkgdir *prev_pkgdir, Header h, const char *fn,
                           struct stat *st)
{
    struct pkg *tmp = NULL, *pkg;
    
    pkg = pm_rpm_ldhdr(NULL, h, fn, st->st_size, PKG_LDNEVR);
    if (pkg && (tmp = n_array_bsearch(prev_pkgdir->pkgs, pkg))) {
        if (pkg_deepstrcmp_name_evr(pkg, tmp) != 0)
            tmp = NULL;
    }
    
    if (pkg)
        pkg_free(pkg);

    return tmp;
}

void remap_groupid(struct pkg *pkg, struct pkgroup_idx *pkgroups,
                  struct pkgdir *prev_pkgdir)
{
    if (pkg->groupid > 0 && prev_pkgdir->pkgroups) {
        int gid;
        gid = pkgroup_idx_remap_groupid(pkgroups,
                                        prev_pkgdir->pkgroups,
                                        pkg->groupid, 1);
        pkg->groupid = gid;
    }
}


static
int load_dir(struct pkgdir *pkgdir,
             const char *dirpath, tn_array *pkgs, struct pkgroup_idx *pkgroups,
             unsigned ldflags, struct pkgdir *prev_pkgdir,
             tn_alloc *na)
{
    tn_hash        *mtime_index = NULL;  
    struct dirent  *ent;
    struct stat    st;
    DIR            *dir;
    int            n, nnew = 0;
    char           *sepchr = "/";
    
    if ((dir = opendir(dirpath)) == NULL) {
        logn(LOGERR, "opendir %s: %m", dirpath);
        return -1;
    }

    if (prev_pkgdir)
        mtime_index = build_mtime_index(prev_pkgdir->pkgs);

    if (dirpath[strlen(dirpath) - 1] == '/')
        sepchr = "";

    n = 0;
    while ((ent = readdir(dir))) {
        char path[PATH_MAX];
        struct pkg *pkg = NULL;
        tn_array *pkg_langs;
        Header h = NULL;

        
        if (fnmatch("*.rpm", ent->d_name, 0) != 0) 
            continue;

        //if (fnmatch("*.src.rpm", ent->d_name, 0) == 0) 
        //    continue;
        
        snprintf(path, sizeof(path), "%s%s%s", dirpath, sepchr, ent->d_name);
        
        if (stat(path, &st) != 0) {
            logn(LOGERR, "stat %s: %m", path);
            continue;
        }
        
        if (!S_ISREG(st.st_mode))
            continue;

        if (mtime_index) {
            pkg = search_in_mtime_index(mtime_index, ent->d_name, &st);
            if (pkg) {
                msgn(3, "%s: file seems untouched, loaded from previous index",
                     pkg_filename_s(pkg));
                pkg = pkg_link(pkg);
                remap_groupid(pkg, pkgroups, prev_pkgdir);
            }
        }
        

        if (pkg == NULL) {
            if (!pm_rpmhdr_loadfile(path, &h)) {
                logn(LOGWARN, "%s: read header failed, skipped", path);
                continue;
            }
            
            //if (rpmhdr_issource(h)) /* omit src.rpms */
            //    continue;

            if (prev_pkgdir) {
                pkg = search_in_prev(prev_pkgdir, h, ent->d_name, &st);
                if (pkg) {
                    msgn(3, "%s: seems untouched, loaded from previous index",
                         pkg_snprintf_s(pkg));
                    pkg = pkg_link(pkg);
                    remap_groupid(pkg, pkgroups, prev_pkgdir);
                }
            }
        }
            
        
        if (pkg == NULL) {      /* not in previous index */
            nnew++;
            n_assert(h);
            msgn(3, "%s: loading header...", n_basenam(path));
            pkg = pm_rpm_ldhdr(na, h, n_basenam(path), st.st_size,
                               PKG_LDWHOLE);
                
            if (ldflags & PKGDIR_LD_DESC) {
                pkg->pkg_pkguinf = pkguinf_ldrpmhdr(na, h);
                pkg_set_ldpkguinf(pkg);
                if ((pkg_langs = pkguinf_langs(pkg->pkg_pkguinf))) {
                    int i;
                        
                    for (i=0; i < n_array_size(pkg_langs); i++)
                        pkgdir__update_avlangs(pkgdir,
                                               n_array_nth(pkg_langs, i), 1);
                }
            }
            pkg->groupid = pkgroup_idx_update_rpmhdr(pkgroups, h);
        }

        if (h)
            headerFree(h);
            
        if (pkg) {
            pkg->fmtime = st.st_mtime;
            n_array_push(pkgs, pkg);
            n++;
        }
        
        if (n && n % 200 == 0) 
            msg(1, "_%d..", n);
    }

    /* if there are ones from prev_pkgdir then assume that
       they provide all avlangs */
    
    if (prev_pkgdir && n_array_size(pkgs) - nnew > 0) { 
        tn_array *langs = n_hash_keys(prev_pkgdir->avlangs_h);
        int i, nprev;

        nprev = n_array_size(pkgs) - nnew;
        for (i=0; i < n_array_size(langs); i++)
            pkgdir__update_avlangs(pkgdir, n_array_nth(langs, i), nprev);
        n_array_free(langs);
    }

    if (n && n > 200)
        msg(1, "_%d\n", n);
    closedir(dir);
    if (mtime_index)
        n_hash_free(mtime_index);
    return n;
}

static
int do_load(struct pkgdir *pkgdir, unsigned ldflags)
{
    int n;
    
    if (pkgdir->pkgroups == NULL)
        pkgdir->pkgroups = pkgroup_idx_new();
    
    n = load_dir(pkgdir,
                 pkgdir->path, pkgdir->pkgs, pkgdir->pkgroups,
                 ldflags, pkgdir->prev_pkgdir, pkgdir->na);
    
    return n;
}

