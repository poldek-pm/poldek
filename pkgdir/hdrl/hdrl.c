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
#include "pkgdir.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgroup.h"
#include "rpm/rpm_pkg_ld.h"

static
int do_load(struct pkgdir *pkgdir, unsigned ldflags);
static int do_update_a(const struct source *src);
static int do_update(struct pkgdir *pkgdir, int *npatches);

static char *aliases[] = { "apt", "wuch", NULL };

struct pkgdir_module pkgdir_module_hdrl = {
    PKGDIR_CAP_UPDATEABLE | PKGDIR_CAP_UPDATEABLE_INC, 
    "hdrl",
    (char **)aliases,
    "file with raw package headers; used by apt-rpm",
    "hdlist",
    NULL,
    do_load,
    NULL,
    do_update,
    do_update_a,
    NULL,
    NULL,
    NULL
};



static
int load_header_list(const char *path, tn_array *pkgs,
                     struct pkgroup_idx *pkgroups,
                     unsigned ldflags)
{
    struct vfile         *vf;
    struct pkg           *pkg;
    Header               h;
    FD_t                 fdt = NULL;
    int                  n = 0;
    unsigned             vfmode = VFM_RO | VFM_CACHE | VFM_UNCOMPR | VFM_NOEMPTY;


    if ((vf = vfile_open(path, VFT_IO, vfmode)) == NULL)
        return -1;

    fdt = fdDup(vf->vf_fd);
    if (fdt == NULL || Ferror(fdt)) {
        const char *err = "unknown error";
        if (fdt)
            err = Fstrerror(fdt);
        
        logn(LOGERR, "rpmio's fdDup failed: %s", err);

        if (fdt)
            Fclose(fdt);
        vfile_close(vf);
        return -1;
    }
    
    while ((h = headerRead(fdt, HEADER_MAGIC_YES))) {
        if (headerIsEntry(h, RPMTAG_SOURCEPACKAGE)) { /* omit src.rpms */
            headerFree(h);
            continue;
        }
        
        if ((pkg = pkg_ldrpmhdr(h, path, 0, PKG_LDWHOLE))) {
            if (ldflags & PKGDIR_LD_DESC) {
                pkg->pkg_pkguinf = pkguinf_ldhdr(h);
                pkg_set_ldpkguinf(pkg);
            }

            n_array_push(pkgs, pkg);
            pkg->groupid = pkgroup_idx_update(pkgroups, h);
            n++;
        }
        	
        headerFree(h);
    }
    
    if (fdt)
        Fclose(fdt);
    vfile_close(vf);
    
    if (n == 0)
        logn(LOGWARN, "%s: empty or invalid 'hdrl' file", n_basenam(path));
    
    return n;
}

static
int do_load(struct pkgdir *pkgdir, unsigned ldflags)
{
    int n;
    n = load_header_list(pkgdir->path, pkgdir->pkgs, pkgdir->pkgroups, ldflags);
    return n;
}


static
int hdrl_update(const char *path, int vfmode)
{
    struct vfile         *vf;
    FD_t                 fdt = NULL;
    int                  rc = 1;
    
    if ((vf = vfile_open(path, VFT_IO, vfmode)) == NULL)
        return 0;

    fdt = fdDup(vf->vf_fd);
    if (fdt == NULL || Ferror(fdt)) {
        const char *err = "unknown error";
        if (fdt)
            err = Fstrerror(fdt);
        
        logn(LOGERR, "rpmio's fdDup failed: %s", err);
        rc = 0;
    }
    
    
    if (fdt)
        Fclose(fdt);
    vfile_close(vf);
    return rc;
}


static
int do_update_a(const struct source *src)
{
    int vfmode;
    
    vfmode = VFM_RO | VFM_NOEMPTY | VFM_NODEL;;
    return hdrl_update(src->path, vfmode);
}

static 
int do_update(struct pkgdir *pkgdir, int *npatches) 
{
    int vfmode;

    npatches = npatches;
    
    vfmode = VFM_RO | VFM_NOEMPTY | VFM_NODEL | VFM_CACHE_NODEL;
    return hdrl_update(pkgdir->idxpath, vfmode);
}
