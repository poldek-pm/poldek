/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fnmatch.h>
#include <obstack.h>
#include <rpm/rpmlib.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/nstr.h>

#include <vfile/vfile.h>

#include "log.h"
#include "rpmadds.h"
#include "depdirs.h"
#include "misc.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgset-def.h"
#include "pkgset.h"
#include "pkgset-load.h"
#include "rpm.h"

static
struct pkg *pkgset_addpkg_rpmhdr(struct pkgset *ps, const char *path,
                                 Header h);


int pkgset_load_dir(struct pkgset *ps, const char *dirpath)
{
    struct dirent *ent;
    struct stat st;
    DIR *dir;
    int n;
    

    if ((dir = opendir(dirpath)) == NULL) {
	log(LOGERR, "opendir %s: %m\n", dirpath);
	return 0;
    }

    n = 0;
    while( (ent = readdir(dir)) ) {
        char path[PATH_MAX];
        
        if (fnmatch("*.rpm", ent->d_name, 0) != 0) 
            continue;

        if (fnmatch("*.src.rpm", ent->d_name, 0) == 0) 
            continue;

        snprintf(path, sizeof(path), "%s/%s", dirpath, ent->d_name);
        
        if (stat(path, &st) != 0) {
            log(LOGERR, "stat %s: %m", path);
            continue;
        }
        
        if (S_ISREG(st.st_mode)) {
            Header h;
            FD_t fdt;
            
            if ((fdt = Fopen(path, "r")) == NULL) {
                log(LOGERR, "open %s: %s\n", path, rpmErrorString());
                continue;
            }
            
            if (rpmReadPackageHeader(fdt, &h, NULL, NULL, NULL) != 0) {
                log(LOGERR, "%s: read header failed, skiped\n", path);
                
            } else {
                if (headerIsEntry(h, RPMTAG_SOURCEPACKAGE)) /* omit src.rpms */
                    continue;
                
                if (pkgset_addpkg_rpmhdr(ps, "", h))
                    n++;
                headerFree(h);
            }
            Fclose(fdt);
            
            if (n && n % 100 == 0) 
                msg_l(2, "_%d..", n);
            	
        }
    }
    msg_l(2, "_%d\n", n);
    closedir(dir);
    return n;
}


int pkgset_create_rpmidx(const char *dirpath, const char *pathname)
{
    struct dirent *ent;
    struct stat st;
    DIR *dir;
    FD_t fdto;
    int err, n;
    
    if ((dir = opendir(dirpath)) == NULL) {
	log(LOGERR, "opendir %s: %m\n", dirpath);
	return 0;
    }

    if ((fdto = Fopen(pathname, "w")) == NULL) {
        log(LOGERR, "open %s: %s\n", pathname, rpmErrorString());
        closedir(dir);
        return 0;
    }

    n = 0;
    err = 0;
    while( (ent = readdir(dir)) && !err) {
        char path[PATH_MAX];
        
        if (fnmatch("*.rpm", ent->d_name, 0) != 0) 
            continue;

        if (fnmatch("*.src.rpm", ent->d_name, 0) == 0) 
            continue;

        snprintf(path, sizeof(path), "%s/%s", dirpath, ent->d_name);
        
        if (stat(path, &st) != 0) {
            log(LOGERR, "stat %s: %m", path);
            continue;
        }
        
        if (S_ISREG(st.st_mode)) {
            Header h;
            FD_t fdt;
            
            if ((fdt = Fopen(path, "r")) == NULL) {
                log(LOGERR, "open %s: %s\n", path, rpmErrorString());
                continue;
            }
            
            if (rpmReadPackageHeader(fdt, &h, NULL, NULL, NULL) != 0) {
                log(LOGERR, "%s: read header failed, skiped\n", path);
                
            } else {
                if (headerIsEntry(h, RPMTAG_SOURCEPACKAGE)) { 
                    msg(2, "omiting source package %s\n", path);
                    
                } else {
                    if (headerWrite(fdto, h, HEADER_MAGIC_YES) != 0) {
                        log(LOGERR, "write header %s: %s\n", path,
                            rpmErrorString());
                        err = 1;
                    }
                    n++;
                }
                headerFree(h);
            }
            Fclose(fdt);
        }
    }

    if (!err)
        msg(1, "%d headers saved in %s\n", n, pathname);
    
    Fclose(fdto);
    closedir(dir);
    return err ? 0 : n;
}

#if 0 
int is_poldeks(const char *fpath) 
{
    FILE *f;
    char line[1024];

    if ((f = fopen(fpath, "r")) == NULL)
        return 0;
    
    if (fgets(line, sizeof(line), f)) {
        
    }
#endif

   
int pkgset_load_rpmidx(struct pkgset *ps, const char *fpath)
{
    Header h;
    struct vfile *vf;
    int n = 0;
    
    
    if ((vf = vfile_open(fpath, VFT_RPMIO, VFM_RO)) == NULL) 
        return 0;

    while ((h = headerRead(vf->vf_fdt, HEADER_MAGIC_YES))) {
        if (headerIsEntry(h, RPMTAG_SOURCEPACKAGE)) { /* omit src.rpms */
            headerFree(h);
            continue;
        }
        
        if (pkgset_addpkg_rpmhdr(ps, fpath, h))
            n++;

        headerFree(h);
    }

    vfile_close(vf);
    return n;
}

 
static
struct pkg *pkgset_addpkg_rpmhdr(struct pkgset *ps, const char *path, Header h)
{
    struct pkg *pkg;
    
    if ((pkg = pkg_ldhdr(h, path, PKG_LDWHOLE))) {
        pkg->pkg_pkguinf = pkguinf_ldhdr(h);
        pkg_set_ldpkguinf(pkg);
        n_array_push(ps->pkgs, pkg);
    }
    
    return pkg;
}
