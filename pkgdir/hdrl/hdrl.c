/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

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

#include <trurl/trurl.h>
#include <vfile/vfile.h>

#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pkgdir.h"
#include "pkgdir_intern.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgroup.h"
#include "pm/rpm/pm_rpm.h"

static
int do_load(struct pkgdir *pkgdir, unsigned ldflags);
static int do_update_a(const struct source *src, const char *idxpath,
                       enum pkgdir_uprc *uprc);
static int do_update(struct pkgdir *pkgdir, enum pkgdir_uprc *uprc);

static char *aliases[] = { "apt", NULL };

struct pkgdir_module pkgdir_module_hdrl = {
    NULL, 
    PKGDIR_CAP_UPDATEABLE | PKGDIR_CAP_UPDATEABLE_INC | PKGDIR_CAP_NOSAVAFTUP,
    "hdrl",
    (char **)aliases,
    "File with raw RPM package headers; used by apt-rpm",
    "pkglist",
    "bz2",
    NULL,
    do_load,
    NULL,
    do_update,
    do_update_a,
    NULL,
    NULL,
    NULL,
    NULL,
};


static
int load_header_list(const char *slabel, const char *path, tn_array *pkgs,
                     struct pkgroup_idx *pkgroups, unsigned ldflags,
                     tn_alloc *na)
{
    struct vfile         *vf;
    struct pkg           *pkg;
    Header               h;
    FD_t                 fdt = NULL;
    int                  n = 0;
    unsigned             vfmode = VFM_RO | VFM_CACHE | VFM_UNCOMPR | VFM_NOEMPTY;


    if ((vf = vfile_open_ul(path, VFT_IO, vfmode, slabel)) == NULL)
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
    
    while ((h = pm_rpmhdr_readfdt(fdt))) {
        if (pm_rpmhdr_issource(h)) { /* omit src.rpms */
            headerFree(h);
            continue;
        }
        
        if ((pkg = pm_rpm_ldhdr(na, h, NULL, 0, PKG_LDWHOLE))) {
            if (ldflags & PKGDIR_LD_DESC) {
                pkg->pkg_pkguinf = pkguinf_ldrpmhdr(na, h);
                pkg_set_ldpkguinf(pkg);
            }

            n_array_push(pkgs, pkg);
            pkg->groupid = pkgroup_idx_update_rpmhdr(pkgroups, h);
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
    if (pkgdir->pkgroups == NULL)
        pkgdir->pkgroups = pkgroup_idx_new();
    n = load_header_list(pkgdir_idstr(pkgdir), pkgdir->idxpath, pkgdir->pkgs,
                         pkgdir->pkgroups, ldflags, pkgdir->na);
    return n;
}


static
int hdrl_update(const char *path, int vfmode, const char *sl,
                enum pkgdir_uprc *uprc)
{
    struct vfile         *vf;
    FD_t                 fdt = NULL;
    int                  rc = 1;

    *uprc = PKGDIR_UPRC_NIL;
    if ((vf = vfile_open_ul(path, VFT_IO, vfmode, sl)) == NULL) {
        *uprc = PKGDIR_UPRC_ERR_UNKNOWN;
        return 0;
    }

    fdt = fdDup(vf->vf_fd);
    if (fdt == NULL || Ferror(fdt)) {
        const char *err = "unknown error";
        if (fdt)
            err = Fstrerror(fdt);
        
        logn(LOGERR, "rpmio's fdDup failed: %s", err);
        *uprc = PKGDIR_UPRC_ERR_UNKNOWN;
        rc = 0;
    }
    
    if (fdt)
        Fclose(fdt);

    if (rc) {
        if (vf->vf_flags & VF_FETCHED) /* updated */
            *uprc = PKGDIR_UPRC_UPDATED;
        else
            *uprc = PKGDIR_UPRC_UPTODATE;
    }
    
    vfile_close(vf);
    return rc;
}



static int do_update_a(const struct source *src, const char *idxpath,
                       enum pkgdir_uprc *uprc)
{
    int vfmode;

    vfmode = VFM_RO | VFM_NOEMPTY | VFM_NODEL;
    return hdrl_update(idxpath, vfmode, src->name, uprc);
}

static int do_update(struct pkgdir *pkgdir, enum pkgdir_uprc *uprc)
{
    int vfmode;
    
    vfmode = VFM_RO | VFM_NOEMPTY | VFM_NODEL | VFM_CACHE_NODEL;
    return hdrl_update(pkgdir->idxpath, vfmode, pkgdir->name, uprc);
}

#if 0                           /* NFY */
/* extra RPM tags, taken from apt-rpm */
#define CRPMTAG_FILENAME          1000000
#define CRPMTAG_FILESIZE          1000001

extern int hdrl_tags[];
extern int hdrl_tags_size;


static void *pkg_to_rpmhdr(struct pkg *pkg)
{
    Header h;

    h = headerNew();
    
    headerAddEntry(h, CRPMTAG_MD5, RPM_STRING_TYPE, md5, 1);
}


static
int do_create(struct pkgdir *pkgdir, const char *pathname, unsigned flags)
{
    struct tndb      *db = NULL;
    int              i, nerr = 0;
    struct pndir     *idx;
    tn_array         *keys = NULL;
    tn_buf           *nbuf = NULL;
    unsigned         pkg_st_flags = flags;
    tn_hash          *db_dscr_h = NULL;
    struct pndir_paths paths;
    
    idx = pkgdir->mod_data;

    if (pkgdir->ts == 0) 
        pkgdir->ts = time(0);

    if (pathname == NULL && idx && idx->_vf) 
        pathname = vfile_localpath(idx->_vf);

    if (pathname == NULL && pkgdir->idxpath)
        pathname = pkgdir->idxpath;

    
    
    
#endif
