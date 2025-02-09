/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

static int do_open(struct pkgdir *pkgdir, unsigned flags);
static int do_load(struct pkgdir *pkgdir, unsigned ldflags);
static int do_update(struct pkgdir *pkgdir, enum pkgdir_uprc *uprc);
static int do_update_a(const struct source *src, const char *idxpath,
                       enum pkgdir_uprc *uprc);
static void do_free(struct pkgdir *pkgdir);

const char *metadata_localidxpath(const struct pkgdir *pkgdir);

#define REPODATA "repodata"
#define REPOMD   "repomd.xml"

static const char *metadata_repodir = REPODATA;
static const char *metadata_indexfile = REPOMD;

struct pkgdir_module pkgdir_module_metadata = {
    NULL,
    PKGDIR_CAP_UPDATEABLE_INC | PKGDIR_CAP_UPDATEABLE | PKGDIR_CAP_NOSAVAFTUP,
    "metadata", NULL,
    "XML Package Metadata format",
    REPODATA "/" REPOMD,
    NULL,
    do_open,
    do_load,
    NULL,
    do_update,
    do_update_a,
    NULL,
    do_free,
    metadata_localidxpath,
    NULL
};

struct idx {
    struct vfile *repomd_vf;
    tn_hash *repomd;
};

const char *metadata_localidxpath(const struct pkgdir *pkgdir)
{
    struct idx *idx = pkgdir->mod_data;

    if (idx && idx->repomd_vf)
        return vfile_localpath(idx->repomd_vf);

    return pkgdir->idxpath;
}


static int prepare_path(char *buf, int size, const char *path, ...)
{
    int n;

    n = vf_cleanpath(buf, size, path);
    n_assert(n >= 0);

    if (n) {
        va_list args;
        char *s;

        va_start(args, path);
        while ((s = va_arg(args, char*)))
            n += n_snprintf(&buf[n], size - n, "/%s", s);
        va_end(args);
    }

    return n;
}

static
int open_repomd(struct idx *idx, const char *path, int vfmode,
                const char *pdir_name)
{
    char apath[PATH_MAX];

    if (!prepare_path(apath, sizeof(apath), path, metadata_repodir,
                      metadata_indexfile, NULL)) {
        logn(LOGERR, "%s: prepare_path() failed", path);
        return 0;
    }

    idx->repomd_vf = vfile_open_ul(apath, VFT_IO, vfmode, pdir_name);
    if (idx->repomd_vf == NULL)
        return 0;

    idx->repomd = metadata_load_repomd(vfile_localpath(idx->repomd_vf));
    if (idx->repomd == NULL) {
        vfile_close(idx->repomd_vf);
        idx->repomd_vf = NULL;
        return 0;
    }

    return 1;
}

static
int verify_digest(struct repomd_ent *ent, const char *path)
{
    char digest[256];
    FILE *stream;
    int type, len;

    if (n_str_eq(ent->checksum_type, "sha"))
        type = DIGEST_SHA1;

    if (n_str_eq(ent->checksum_type, "sha256"))
        type = DIGEST_SHA256;

    else if (n_str_eq(ent->checksum_type, "md5"))
        type = DIGEST_MD5;

    else {
        logn(LOGERR, "%s: %s: unknown digest type", ent->location,
             ent->checksum_type);
        return 0;
    }

    if ((stream = fopen(path, "r")) == NULL) {
        logn(LOGERR, "%s: open %m\n", path);
        return 0;
    }

    len = sizeof(digest);
    mhexdigest(stream, digest, &len, type);
    n_assert(len >= 0);

    if (len == 0)
        return 0;

    n_assert(len > 0);
    n_assert(digest[len] == '\0');
    return n_str_eq(ent->checksum, digest);
}


static
struct vfile *open_metadata_file(tn_hash *repomd,
                                 const char *rootpath, const char *name,
                                 int vfmode, const char *pdir_name,
                                 int quiet)
{
    struct repomd_ent *ent;
    struct vfile *vf;
    char path[PATH_MAX];

    if ((ent = n_hash_get(repomd, name)) == NULL)
        return NULL;

    if (!prepare_path(path, sizeof(path), rootpath, ent->location, NULL)) {
        logn(LOGERR, "%s: prepare_path() failed", path);
        return NULL;
    }
    if (quiet)
        vfmode |= VFM_QUITERR;

    if ((vf = vfile_open_ul(path, VFT_IO, vfmode, pdir_name)) == NULL)
        return NULL;

    if (!verify_digest(ent, vfile_localpath(vf))) {
        if (!quiet)
            logn(LOGERR, "%s: broken file", vfile_localpath(vf));
        vfile_close(vf);
        vf = NULL;
    }

    return vf;
}


static
int idx_open(struct idx *idx, struct pkgdir *pkgdir, int vfmode)
{
    struct vfile *vf;
    const char *pdir_name = pkgdir->name;

    if (!open_repomd(idx, pkgdir->path, vfmode, pdir_name))
        return 0;

    vf = open_metadata_file(idx->repomd, pkgdir->path, "primary", vfmode,
                            pdir_name, 0);

    if (vf) {
        vfile_close(vf); /* just download primary.xml */

    } else {
        n_hash_free(idx->repomd);
        idx->repomd = NULL;
    }

    return idx->repomd != NULL;
}

static
void idx_close(struct idx *idx)
{
    if (idx->repomd)
        n_hash_free(idx->repomd);
    idx->repomd = NULL;

    if (idx->repomd_vf)
        vfile_close(idx->repomd_vf);
    idx->repomd_vf = NULL;
}


static
int do_open(struct pkgdir *pkgdir, unsigned flags)
{
    //struct pkgroup_idx   *pkgroups = NULL;
    struct idx           idx;
    unsigned             vfmode = VFM_RO | VFM_NOEMPTY | VFM_NODEL;

    if ((flags & PKGDIR_OPEN_REFRESH) == 0)
        vfmode |= VFM_CACHE;

    if (!idx_open(&idx, pkgdir, vfmode))
        return 0;

    DBGF("%s\n", pkgdir->path);

    pkgdir->ts = poldek_util_mtime(vfile_localpath(idx.repomd_vf));
    pkgdir->mod_data = n_malloc(sizeof(idx));
    memcpy(pkgdir->mod_data, &idx, sizeof(idx));
    pkgdir->pkgroups = NULL;
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

#if 0                           /* XXX TODO */
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
#endif

static
int do_load(struct pkgdir *pkgdir, unsigned ldflags)
{
    struct idx         *idx;
    tn_array           *pkgs;
    unsigned           vfmode = VFM_RO | VFM_CACHE | VFM_NOEMPTY;
    struct vfile       *vf;

    ldflags = ldflags;
    idx = pkgdir->mod_data;

    vf = open_metadata_file(idx->repomd, pkgdir->path, "primary",
                            vfmode, pkgdir->name, 0);
    if (vf == NULL)
        return 0;

    if (pkgdir->pkgroups == NULL)
        pkgdir->pkgroups = pkgroup_idx_new();

    metadata_loadmod_init();

    pkgs = metadata_load_primary(pkgdir, vfile_localpath(vf));
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

    metadata_loadmod_destroy();

    return n_array_size(pkgdir->pkgs);
}


static
int metadata_update(const struct pkgdir *pkgdir, enum pkgdir_uprc *uprc)
{
    struct idx *idxptr, idx;
    int vfmode;

    *uprc = PKGDIR_UPRC_NIL;

    idxptr = pkgdir->mod_data;
    if (idxptr->repomd_vf->vf_flags & VF_FETCHED) { /* already downloaded */
        *uprc = PKGDIR_UPRC_UPDATED;
        return 1;
    }

    vfmode = VFM_RO | VFM_NOEMPTY | VFM_NODEL;
    /* if (!force) - force not implemented yet */
    vfmode |= VFM_CACHE_NODEL;

    if (!open_repomd(&idx, pkgdir->path, vfmode, pkgdir->name)) {
        *uprc = PKGDIR_UPRC_ERR_UNKNOWN;
        return 0;
    }

    *uprc = PKGDIR_UPRC_UPTODATE;

    if ((idx.repomd_vf->vf_flags & VF_FETCHED)) {
        struct pkgdir *tmp;
        *uprc = PKGDIR_UPRC_UPDATED;

        if ((tmp = pkgdir_srcopen(pkgdir->src, PKGDIR_OPEN_REFRESH)))
            pkgdir_free(tmp);
        else
            *uprc = PKGDIR_UPRC_ERR_UNKNOWN;
    }

    return 1;
}


static
int do_update(struct pkgdir *pkgdir, enum pkgdir_uprc *uprc)
{
    return metadata_update(pkgdir, uprc);
}

static
int do_update_a(const struct source *src, const char *path,
                enum pkgdir_uprc *uprc)
{
    struct pkgdir *pkgdir;
    struct idx *idxptr, idx;
    int vfmode;

    path = path;          /* unused */
    *uprc = PKGDIR_UPRC_NIL;

    pkgdir = pkgdir_srcopen(src, 0);

    if (pkgdir == NULL) {
        int rc = 0;
        if ((pkgdir = pkgdir_srcopen(src, PKGDIR_OPEN_REFRESH))) {
            pkgdir_free(pkgdir);
            *uprc = PKGDIR_UPRC_UPDATED;
            rc = 1;
        }
        return rc;
    }

    idxptr = pkgdir->mod_data;
    if (idxptr->repomd_vf->vf_flags & VF_FETCHED) { /* already downloaded */
        pkgdir_free(pkgdir);
        *uprc = PKGDIR_UPRC_UPDATED;
        return 1;
    }

    vfmode = VFM_RO | VFM_NOEMPTY | VFM_NODEL;
    /* if (!force) - force not implemented yet */
    vfmode |= VFM_CACHE_NODEL;

    if (!open_repomd(&idx, pkgdir->path, vfmode, pkgdir->name)) {
        *uprc = PKGDIR_UPRC_ERR_UNKNOWN;
        return 0;
    }

    *uprc = PKGDIR_UPRC_UPTODATE;

    if ((idx.repomd_vf->vf_flags & VF_FETCHED)) {
        struct pkgdir *tmp;
        *uprc = PKGDIR_UPRC_UPDATED;

        if ((tmp = pkgdir_srcopen(src, PKGDIR_OPEN_REFRESH)))
            pkgdir_free(tmp);
        else
            *uprc = PKGDIR_UPRC_ERR_UNKNOWN;
    }

    return 1;
}


/*
static
 int do_update(struct pkgdir *pkgdir, enum pkgdir_uprc *uprc)
 {
     int vfmode;
     DBGF("metadata_update\n");
     vfmode = VFM_RO | VFM_NOEMPTY | VFM_NODEL | VFM_CACHE_NODEL;
+
+    idx = pkgdir->mod_data;
+    if (idx->_vf->vf_flags & VF_FETCHED)
+        return 1;
+
+
     return metadata_update(pkgdir->idxpath, vfmode, pkgdir->name, uprc);
 }

*/
