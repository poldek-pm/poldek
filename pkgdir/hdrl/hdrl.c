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
#include "rpmadds.h"
#include "pkgdir.h"
#include "pkg.h"
#include "h2n.h"
#include "pkgroup.h"

static
int do_load(struct pkgdir *pkgdir, tn_array *depdirs, unsigned ldflags);

static char *aliases[] = { "apt", "wuch", NULL };

struct pkgdir_module pkgdir_module_hdrl = {
    PKGDIR_CAP_UPDATEABLE, 
    "hdrl",
    (char **)aliases, 
    "hdlist",
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
int load_header_list(const char *path, tn_array *pkgs,
                     struct pkgroup_idx *pkgroups,
                     tn_array *depdirs, unsigned ldflags)
{
    struct vfile         *vf;
    struct pkg           *pkg;
    Header               h;
    int                  n = 0;
    unsigned             vfmode = VFM_RO | VFM_CACHE | VFM_UNCOMPR;

    depdirs = depdirs;
    
    if ((vf = vfile_open(path, VFT_RPMIO, vfmode)) == NULL)
        return -1;
    
    while ((h = headerRead(vf->vf_fdt, HEADER_MAGIC_YES))) {
        if (headerIsEntry(h, RPMTAG_SOURCEPACKAGE)) { /* omit src.rpms */
            headerFree(h);
            continue;
        }
        
        if ((pkg = pkg_ldhdr(h, path, 0, PKG_LDWHOLE))) {
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
    
    vfile_close(vf);
    return n;
}

static
int do_load(struct pkgdir *pkgdir, tn_array *depdirs, unsigned ldflags)
{
    int n;
    n = load_header_list(pkgdir->path, pkgdir->pkgs, pkgdir->pkgroups,
                         depdirs, ldflags);
    return n;
}

