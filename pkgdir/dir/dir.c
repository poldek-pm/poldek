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
#include "rpm/rpmhdr.h"
#include "rpm/rpm_pkg_ld.h"
#include "pkgdir.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgroup.h"
//#include "pkgdb.h"

static
int do_load(struct pkgdir *pkgdir, unsigned ldflags);

struct pkgdir_module pkgdir_module_dir = {
    PKGDIR_CAP_NOPREFIX, 
    "dir",
    NULL,
    "dynamic index build by scanning directory for packages",
    NULL,
    NULL,
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
int load_dir(const char *dirpath, tn_array *pkgs, struct pkgroup_idx *pkgroups,
             tn_hash *avlangs, unsigned ldflags, struct pkgdir *prev_pkgdir)
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
    while ((ent = readdir(dir))) {
        char path[PATH_MAX];
        
        if (fnmatch("*.rpm", ent->d_name, 0) != 0) 
            continue;

        //if (fnmatch("*.src.rpm", ent->d_name, 0) == 0) 
        //    continue;
        
        snprintf(path, sizeof(path), "%s%s%s", dirpath, sepchr, ent->d_name);
        
        if (stat(path, &st) != 0) {
            logn(LOGERR, "stat %s: %m", path);
            continue;
        }
        
        if (S_ISREG(st.st_mode)) {
            struct pkg *pkg = NULL;
            tn_array *pkg_langs;
            Header h;

            
            if (!rpmhdr_loadfile(path, &h)) {
                logn(LOGWARN, "%s: read header failed, skipped", path);
                continue;
            }
            
            //if (rpmhdr_issource(h)) /* omit src.rpms */
            //    continue;

            if (prev_pkgdir) {
                struct pkg *tmp;
                pkg = pkg_ldrpmhdr(h, n_basenam(path), st.st_size, PKG_LDNEVR);
                
                if (pkg && (tmp = n_array_bsearch(prev_pkgdir->pkgs, pkg)) &&
                    pkg_deepstrcmp_name_evr(pkg, tmp) == 0) {

                    if (tmp->groupid > 0 && prev_pkgdir->pkgroups) {
                        int gid;
                        gid = pkgroup_idx_remap_groupid(pkgroups,
                                                        prev_pkgdir->pkgroups,
                                                        tmp->groupid, 1);
                        tmp->groupid = gid;
                    }
                    
                    
                    pkg = pkg_link(tmp);
                    msgn(3, "%s: seems untouched, loaded from previous index",
                         pkg_snprintf_s(tmp));
                    
                } else {
                    if (pkg) {
                        msgn(1, "%s: not found, new?", pkg_snprintf_s(pkg));
                        pkg_free(pkg);
                    }

                    pkg = NULL;
                }
            }
            
            if (pkg == NULL) {
                pkg = pkg_ldrpmhdr(h, n_basenam(path), st.st_size, PKG_LDWHOLE);
                
                if (ldflags & PKGDIR_LD_DESC) {
                    pkg->pkg_pkguinf = pkguinf_ldhdr(h);
                    pkg_set_ldpkguinf(pkg);
                    if ((pkg_langs = pkguinf_langs(pkg->pkg_pkguinf))) {
                        int i;
                        
                        for (i=0; i < n_array_size(pkg_langs); i++) {
                            char *l = n_array_nth(pkg_langs, i);
                            if (!n_hash_exists(avlangs, l))
                                n_hash_insert(avlangs, l, NULL);
                        }
                    }
                }
                pkg->groupid = pkgroup_idx_update_rpmhdr(pkgroups, h);
            }

            if (pkg) {
                n_array_push(pkgs, pkg);
                n++;
            }
        
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
int do_load(struct pkgdir *pkgdir, unsigned ldflags)
{
    int n;
    
    if (pkgdir->pkgs == NULL)
        pkgdir->pkgs = pkgs_array_new(1024);
    
    if (pkgdir->pkgroups == NULL)
        pkgdir->pkgroups = pkgroup_idx_new();
    
    n = load_dir(pkgdir->path, pkgdir->pkgs, pkgdir->pkgroups,
                 pkgdir->avlangs_h, ldflags, pkgdir->prev_pkgdir);
    
    return n;
}

