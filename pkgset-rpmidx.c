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
#include <rpmlib.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>

#include "log.h"
#include "vfile.h"
#include "rpmadds.h"
#include "depdirs.h"
#include "misc.h"
#include "pkg.h"
#include "pkgset-def.h"
#include "pkgset.h"
#include "pkgset-load.h"
#include "rpm.h"

static
struct pkg *pkg_new_from_rpmhdr(const char *fname, Header h, void *udata,
                                size_t udsize);
static
struct pkg *pkgset_addpkg_rpmhdr(struct pkgset *ps, const char *path,
                                 Header h);


int pkgset_load_dir(struct pkgset *ps, const char *dirpath)
{
    char *path;
    struct dirent *ent;
    struct stat st;
    DIR *dir;
    int n, dirpath_len, path_rest_size;

    path_rest_size = 1024;
    dirpath_len = strlen(dirpath);
    path = alloca(dirpath_len + path_rest_size + 1);
    strcpy(path, dirpath);

    if (path[dirpath_len - 1] != '/') { /* add trailing / */
        path[dirpath_len++] = '/';
        path[dirpath_len] = '\0';
        path_rest_size--;
    }
    
    if ((dir = opendir(dirpath)) == NULL) {
	log(LOGERR, "opendir %s: %m\n", dirpath);
	return -1;
    }

    n = 0;
    while( (ent = readdir(dir)) ) {
        if (fnmatch("*.rpm", ent->d_name, 0) != 0) 
            continue;

        if (fnmatch("*.src.rpm", ent->d_name, 0) == 0) 
            continue;
        
        path[dirpath_len] = '\0';
        strncat(&path[dirpath_len], ent->d_name, path_rest_size);
        
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
        }
    }
    
    closedir(dir);
    return n;
}


int pkgset_create_rpmidx(const char *dirpath, const char *pathname)
{
    int dirpath_len, path_rest_size;
    char *path;
    struct dirent *ent;
    struct stat st;
    DIR *dir;
    FD_t fdto;
    int err, n;
    
        
    path_rest_size = 1024;
    dirpath_len = strlen(dirpath);
    path = alloca(dirpath_len + path_rest_size + 1);
    strcpy(path, dirpath);

    if (path[dirpath_len - 1] != '/') { /* add trailing / */
        path[dirpath_len++] = '/';
        path[dirpath_len] = '\0';
        path_rest_size--;
    }
    
    if ((dir = opendir(dirpath)) == NULL) {
	log(LOGERR, "opendir %s: %m\n", dirpath);
	return -1;
    }

    if ((fdto = Fopen(pathname, "w")) == NULL) {
        log(LOGERR, "open %s: %s\n", pathname, rpmErrorString());
        closedir(dir);
        return -1;
    }

    n = 0;
    err = 0;
    while( (ent = readdir(dir)) && !err) {
        if (fnmatch("*.rpm", ent->d_name, 0) != 0) 
            continue;

        if (fnmatch("*.src.rpm", ent->d_name, 0) == 0) 
            continue;
        
        path[dirpath_len] = '\0';
        strncat(&path[dirpath_len], ent->d_name, path_rest_size);
        
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
    return n;
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
        return -1;

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

 
__inline__ 
static int valid_fname(const char *fname, mode_t mode, struct pkg *pkg) 
{
    char *denychars = "\r\n\t |;";
    char *prdenychars = "\\r\\n\\t |;";

    if (strpbrk(fname, denychars)) {
        msg(1, "%s: %s \"%s\" contains character(s) "
            "I don't like (%s)\n", pkg_snprintf_s(pkg),
            S_ISDIR(mode) ? "dirname" : "filename", fname,
            prdenychars);
        return 0;
    }
    return 1;
}
/* -1 on error  */
int pkgfl_loadhdr(Header h, struct pkg *pkg)
{
    int t1, t2, t3, t4, c1, c2, c3, c4;
    char **names = NULL, **dirs = NULL, **symlinks = NULL, **skipdirs;
    int32_t   *diridxs;
    uint32_t  *sizes;
    uint16_t  *modes;
    struct    flfile *flfile;
    struct    pkgfl_ent **fentdirs = NULL;
    int       *fentdirs_items;
    int       i, j, ndirs = 0, nerr = 0;
    
    mem_info(4, "*");
    
    if (!headerGetEntry(h, RPMTAG_BASENAMES, (void*)&t1, (void*)&names, &c1))
        return 0;

    n_assert(t1 == RPM_STRING_ARRAY_TYPE);
    if (!headerGetEntry(h, RPMTAG_DIRNAMES, (void*)&t2, (void*)&dirs, &c2))
        goto l_endfunc;
    
    n_assert(t2 == RPM_STRING_ARRAY_TYPE);
    if (!headerGetEntry(h, RPMTAG_DIRINDEXES, (void*)&t3,(void*)&diridxs, &c3))
        goto l_endfunc;
    n_assert(t3 == RPM_INT32_TYPE);


    headerGetEntry(h, RPMTAG_FILEMODES, (void*)&t4, (void*)&modes, &c4);
    n_assert(modes);
    n_assert(c4);
    headerGetEntry(h, RPMTAG_FILESIZES, (void*)&t4, (void*)&sizes, &c4);
    n_assert(sizes);
    n_assert(c4);
    headerGetEntry(h, RPMTAG_FILELINKTOS, (void*)&t4, (void*)&symlinks, &c4);
    
    n_assert(c1 == c3);

    skipdirs = alloca(sizeof(*skipdirs) * c2);
    fentdirs = alloca(sizeof(*fentdirs) * c2);
    fentdirs_items = alloca(sizeof(*fentdirs_items) * c2);

    /* skip unneded dirnames */
    for (i=0; i<c2; i++) {
        struct pkgfl_ent *flent;
        
        fentdirs_items[i] = 0;
        if (!valid_fname(dirs[i], 0, pkg))
            nerr++;

        if (!in_depdirs(dirs[i] + 1)) {
            msg(5, "skip files in dir %s\n", dirs[i]);
            skipdirs[i] = NULL;
            fentdirs[i] = NULL;
            continue;
        }
        
        skipdirs[i] = dirs[i];
        for (j=0; j<c1; j++)
            if (diridxs[j] == i)
                fentdirs_items[i]++;
        
        flent = pkgfl_ent_new(dirs[i], strlen(dirs[i]), fentdirs_items[i]);
        fentdirs[i] = flent;
        ndirs++;
    }

    
    msg(4, "%d files in package\n", c1);
    for (i=0; i<c1; i++) {
        struct pkgfl_ent *flent;
        register int j = diridxs[i];
        int len;

        if (!valid_fname(names[i], modes[i], pkg))
            nerr++;
        
        msg(5, "  %d: %s %s%s \n", i, skipdirs[j] ? "add " : "skip",
            dirs[j], names[i]);
            
        if (skipdirs[j] == NULL)
            continue;
        
        flent = fentdirs[j];
        len = strlen(names[i]);
        if (symlinks) { 
            flfile = flfile_new(sizes ? sizes[i] : 0,
                                modes ? modes[i] : 0,
                                names[i], len,
                                symlinks[i],
                                strlen(symlinks[i]));
        } else {
            flfile = flfile_new(sizes ? sizes[i] : 0,
                                modes ? modes[i] : 0,
                                names[i], len,
                                NULL,
                                0);
            
        }
        
        flent->files[flent->items++]= flfile;
        n_assert(flent->items <= fentdirs_items[j]);
    }
    
 l_endfunc:
    
    if (c1 && names)
        rpm_headerEntryFree(names, t1);

    if (c2 && dirs)
        rpm_headerEntryFree(dirs, t2);

    if (c4 && symlinks)
        rpm_headerEntryFree(symlinks, t4);

    pkg->fl = NULL;
    if (nerr) 
        log(LOGERR, "%s skiped cause filenames errors\n", pkg_snprintf_s(pkg));
    
    else if (ndirs) {
        pkg->fl = n_array_new(ndirs, NULL, pkgfl_ent_cmp);
        for (i=0; i<c2; i++) 
            if (fentdirs[i] != NULL)
                n_array_push(pkg->fl, fentdirs[i]);
    }
    
    return nerr ? -1 : 1;
}

static
struct pkg *pkg_new_from_rpmhdr(const char *fname, Header h, void *udata,
                                size_t udsize)
{
    struct pkg *pkg;
    uint32_t   *epoch;
    char       *name, *version, *release, *arch;
    int        type;
    
    headerNVR(h, (void*)&name, (void*)&version, (void*)&release);
    if (name == NULL || version == NULL || release == NULL) {
        log(LOGERR, "%s: read name/version/release failed\n", fname);
        return NULL;
    }
    
    if (!headerGetEntry(h, RPMTAG_EPOCH, &type, (void *)&epoch, NULL)) 
        epoch = NULL;

    if (!headerGetEntry(h, RPMTAG_ARCH, &type, (void *)&arch, NULL)) {
        log(LOGERR, "%s: read architecture tag failed\n", fname);
        return NULL;
    }
    
    pkg = pkg_new_udata(name, epoch ? *epoch : 0, version, release, arch,
                        NULL, udata, udsize);

    if (pkg == NULL)
        return NULL;

    msg(4, "== %s ==\n", pkg_snprintf_s(pkg));
    
    pkg->caps = capreq_arr_new();
    get_pkg_caps(pkg->caps, h);
    
    if (n_array_size(pkg->caps)) 
        n_array_sort(pkg->caps);
    else {
        n_array_free(pkg->caps);
        pkg->caps = NULL;
    }
    
    pkg->reqs = capreq_arr_new();
    get_pkg_reqs(pkg->reqs, h);

    if (n_array_size(pkg->reqs) == 0) {
        n_array_free(pkg->reqs);
        pkg->reqs = NULL;
    }

    pkg->cnfls = capreq_arr_new();
    get_pkg_cnfls(pkg->cnfls, h);
    get_pkg_obsls(pkg->cnfls, h);

    if (n_array_size(pkg->cnfls) > 0) {
        n_array_sort(pkg->cnfls);
        
    } else {
        n_array_free(pkg->cnfls);
        pkg->cnfls = NULL;
    };
    
    if (pkgfl_loadhdr(h, pkg) == -1) {
        pkg_free(pkg);
        pkg = NULL;
    }
    return pkg;
}


static
struct pkg *pkgset_addpkg_rpmhdr(struct pkgset *ps, const char *path, Header h)
{
    struct pkg *pkg;
    
    pkg = pkg_new_from_rpmhdr(path, h, NULL, 0);
    
    if (pkg) 
        n_array_push(ps->pkgs, pkg);
    
    return pkg;
}
