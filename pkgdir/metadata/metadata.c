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

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>
#include <trurl/nstream.h>
#include <trurl/nmalloc.h>

#include <vfile/vfile.h>


#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pkgdir.h"
#include "pkgdir_intern.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgfl.h"
#include "pkgmisc.h"
#include "pkgroup.h"

#include "load.h"

struct pkg_data {
    char *hdr_path;
};

static int do_open(struct pkgdir *pkgdir, unsigned flags);
static int do_load(struct pkgdir *pkgdir, unsigned ldflags);
static int do_update(struct pkgdir *pkgdir, int *npatches);
static int do_update_a(const struct source *src, const char *idxpath,
                       enum pkgdir_uprc *uprc);
static void do_free(struct pkgdir *pkgdir);

static const char *metadata_repodir = "repodata";
static const char *metadata_indexfile = "repomd.xml";

struct pkgdir_module pkgdir_module_metadata = {
    NULL, 
    PKGDIR_CAP_UPDATEABLE_INC | PKGDIR_CAP_UPDATEABLE | PKGDIR_CAP_NOSAVAFTUP,
    "metadata", NULL,
    "package metadata format",
    NULL, 
    NULL,      /* metadata location is predefined as repodata/repomd.xml */
    do_open,
    do_load,
    NULL,
    do_update, 
    do_update_a,
    NULL, 
    do_free,
    NULL,
    NULL
};

struct idx {
    tn_hash *repomd;
};

static tn_hash *open_metadata_repomd(const char *path, int vfmode, const char *idx_name,
                                     enum pkgdir_uprc *uprc)
{
    struct vfile *vf;
    tn_hash *repomd = NULL;
    char apath[PATH_MAX];
    
    if (uprc)
        *uprc = PKGDIR_UPRC_ERR_UNKNOWN;

    n_snprintf(apath, sizeof(apath), "%s/%s/%s", path, metadata_repodir,
               metadata_indexfile);
    
    if ((vf = vfile_open_ul(apath, VFT_IO, vfmode, idx_name))) {
        repomd = metadata_load_repomd(vfile_localpath(vf));

        if (uprc) {
            if (vf->vf_flags & VF_FETCHED)
                *uprc = PKGDIR_UPRC_UPDATED;
            else
                *uprc = PKGDIR_UPRC_UPTODATE;
        }
        
        vfile_close(vf);
    }
    
    return repomd;
}

static struct vfile *open_metadata_file(tn_hash *repomd, const char *rootpath,
                                        const char *name, int vfmode, const char *idx_name)
{
    struct repomd_ent *ent;
    struct vfile *vf;
    char path[PATH_MAX];

    if ((ent = n_hash_get(repomd, name)) == NULL)
        return NULL;
    
    n_snprintf(path, sizeof(path), "%s/%s", rootpath, ent->location);
    if ((vf = vfile_open_ul(path, VFT_IO, vfmode, idx_name)) == NULL)
        return NULL;
    
    return vf;
}

    
static
int idx_open(struct idx *idx, struct pkgdir *pkgdir, int vfmode)
{
    idx->repomd = open_metadata_repomd(pkgdir->path, vfmode, pkgdir->name, NULL);
    if (idx->repomd == NULL)
        return 0;
    
    return idx->repomd != NULL;
}

static
void idx_close(struct idx *idx) 
{
    if (idx->repomd)
        n_hash_free(idx->repomd);

    idx->repomd = NULL;
}


static
int do_open(struct pkgdir *pkgdir, unsigned flags)
{
    struct pkgroup_idx   *pkgroups = NULL;
    struct idx           idx;
    unsigned             vfmode = VFM_RO | VFM_CACHE | VFM_NOEMPTY;
    
    flags = flags;              /* unused */

    DBGF("idxpath %s\n", pkgdir->idxpath);
    if (!idx_open(&idx, pkgdir, vfmode))
        return 0;
    
    pkgdir->mod_data = n_malloc(sizeof(idx));
    memcpy(pkgdir->mod_data, &idx, sizeof(idx));
    pkgdir->pkgroups = NULL;
    
    //if (nerr)
    //    idx_close(&idx);
    
    //return nerr == 0;
    return 1;
}

static
void do_free(struct pkgdir *pkgdir) 
{
    if (pkgdir->mod_data) {
        struct idx *idx = pkgdir->mod_data;
        idx_close(idx);
        free(idx);
        pkgdir->mod_data = NULL;
    }
}

static 
struct pkguinf *load_pkguinf(tn_alloc *na, const struct pkg *pkg,
                             void *ptr, tn_array *langs)
{
    unsigned        vfmode = VFM_RO | VFM_CACHE | VFM_NOEMPTY;
    struct pkguinf  *pkgu = NULL;
    char            path[PATH_MAX], *hdrpath;
    
    langs = langs;               /* ignored, no support */
    if (!pkg->pkgdir)
        return NULL;

    return NULL;
}

static
void pkg_data_free(tn_alloc *na, void *ptr) 
{
    na->na_free(na, ptr);
}

static
int do_load(struct pkgdir *pkgdir, unsigned ldflags)
{
    struct idx         *idx;
    tn_array           *pkgs;
    unsigned           vfmode = VFM_RO | VFM_CACHE | VFM_NOEMPTY;
    struct vfile       *vf;

    idx = pkgdir->mod_data;

    vf = open_metadata_file(idx->repomd, pkgdir->path, "primary",
                            vfmode, pkgdir->name);
    if (vf == NULL)
        return 0;

    pkgs = metadata_load_primary(pkgdir->na, vfile_localpath(vf));
    vfile_close(vf);

    if (pkgs) {
        int i;
        for (i=0; i<n_array_size(pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgs, i);
            pkg->pkgdir = pkgdir;
#if 0 /* XXX TODO */
            pkg->pkgdir_data = pkgdir->na->na_malloc(pkgdir->na,
                                                     strlen(en->nvr) + 1); 
            memcpy(pkg->pkgdir_data, en->nvr, strlen(en->nvr) + 1);
            pkg->pkgdir_data_free = pkg_data_free;
            pkg->load_pkguinf = load_pkguinf;
#endif            
            n_array_push(pkgdir->pkgs, pkg);
        }
        n_array_free(pkgs);
    }

    return n_array_size(pkgdir->pkgs);
}

static
int metadata_update(const char *path, int vfmode, const char *sl,
                    enum pkgdir_uprc *uprc)
{
    tn_hash         *repomd;
    struct vfile    *vf;
    int             rc = 1;
    
    *uprc = PKGDIR_UPRC_NIL;
    repomd = open_metadata_repomd(path, vfmode, sl, uprc);
    if (repomd == NULL)
        return 0;

    vf = open_metadata_file(repomd, path, "primary", vfmode, sl);
    if (vf == NULL)
        rc = 0;
    else
        vfile_close(vf);
    
    n_hash_free(repomd);
    return rc;
}


static int do_update_a(const struct source *src, const char *path,
                       enum pkgdir_uprc *uprc)
{
    int vfmode;

    vfmode = VFM_RO | VFM_NOEMPTY | VFM_NODEL;
    return metadata_update(path, vfmode, src->name, uprc);
}

static 
int do_update(struct pkgdir *pkgdir, enum pkgdir_uprc *uprc) 
{
    int vfmode;
    
    vfmode = VFM_RO | VFM_NOEMPTY | VFM_NODEL | VFM_CACHE_NODEL;
    return metadata_update(pkgdir->path, vfmode, pkgdir->name, uprc);
}
