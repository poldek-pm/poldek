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

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>
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
#include "pm/rpm/pm_rpm.h"

struct pkg_data {
    char *hdr_path;
};

static int do_open(struct pkgdir *pkgdir, unsigned flags);
static int do_load(struct pkgdir *pkgdir, unsigned ldflags);
static int do_update(struct pkgdir *pkgdir, int *npatches);
static int do_update_a(const struct source *src, const char *idxpath);
static void do_free(struct pkgdir *pkgdir);

struct pkgdir_module pkgdir_module_yum = {
    PKGDIR_CAP_UPDATEABLE_INC | PKGDIR_CAP_UPDATEABLE,
    "yum", NULL,
    "yum index",
    "headers/header.info",
    NULL,
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


struct yum_entry {
    uint32_t  epoch;
    char      *fn;
    char      nvr[0];
};

struct idx {
    struct vfile *vf;
    tn_array *ents;
};

static int yum_entry_cmp(struct yum_entry *en1, struct yum_entry *en2) 
{
    return strcmp(en1->nvr, en2->nvr);
}


static
int idx_open(struct idx *idx, const char *path, int vfmode,
             const char *pdir_name)
{
    idx->vf = vfile_open_sl(path, VFT_TRURLIO, vfmode, pdir_name);
    if (idx->vf)
        idx->ents = n_array_new(1024, free, (tn_fn_cmp)yum_entry_cmp);
    
    return idx->vf != NULL;
}

static
void idx_close(struct idx *idx) 
{
    if (idx->vf)
        vfile_close(idx->vf);

    if (idx->ents)
        n_array_free(idx->ents);

    idx->vf = NULL;
    idx->ents = NULL;
}


static
int do_open(struct pkgdir *pkgdir, unsigned flags)
{
    struct vfile         *vf;
    char                 linebuf[1024 * 16];
    int                  nline, nerr = 0, nread, n;
    struct pkgroup_idx   *pkgroups = NULL;
    struct idx           idx;
    unsigned             vfmode = VFM_RO | VFM_CACHE | VFM_NOEMPTY;
    char                 *path = pkgdir->path;
    
    flags = flags;              /* unused */
    
    if (!idx_open(&idx, pkgdir->idxpath, vfmode, pkgdir->name))
        return 0;
    
    vf = idx.vf;
    nerr = 0;

    while ((nread = n_stream_gets(vf->vf_tnstream, linebuf, sizeof(linebuf))) > 0) {
        char              *p, *q, *nvr, *fn, *ep, nevr[512];
        const char        *name, *ver, *rel;
        int               fn_len;
        struct yum_entry  *en;
        int32_t           epoch, dummy;
        
        while (nread > 0 && isspace(linebuf[nread - 1]))
            linebuf[--nread] = '\0';

        p = linebuf;
        nline++;
        if (nread < 2 || !isdigit(*p) || (q = strchr(p, ':')) == NULL) {
            logn(LOGERR, _("%s:%d syntax error"),
                 vf->vf_tmpath ? vf->vf_tmpath : path, nline);
            continue;
        }
        ep = p;
        *q = '\0';
        q++;
        
        nvr = q;
        if ((q = strchr(q, '=')) == NULL) {
            logn(LOGERR, _("%s:%d syntax error"),
                 vf->vf_tmpath ? vf->vf_tmpath : path, nline);
            continue;
        }
        *q = '\0';
        fn = q + 1;

        epoch = (int32_t)strtol(ep, (char **)NULL, 10);
        //printf("nvr = %s, fn = %s, %d\n", nvr, fn, nread);
        
        if (!parse_nevr(nvr, &name, &dummy, &ver, &rel)) {
            logn(LOGERR, _("%s:%d syntax error"),
                 vf->vf_tmpath ? vf->vf_tmpath : path, nline);
            continue;
        }
        fn_len = strlen(fn);
        n = n_snprintf(nevr, sizeof(nevr), "%s-%d-%s-%s", name, epoch, ver, rel);
        en = n_malloc(sizeof(*en) + n + 1 + fn_len + 1);
        n += 1;
        memcpy(en->nvr, nevr, n);
        memcpy(&en->nvr[n], fn, fn_len + 1);
        en->fn = &en->nvr[n];
        DBGF_F("yum  en.nevr = %s, en.fn = %s, %s\n", en->nvr, en->fn, fn);
        n_array_push(idx.ents, en);
    }
    n_array_sort(idx.ents);
    pkgdir->mod_data = n_malloc(sizeof(idx));
    memcpy(pkgdir->mod_data, &idx, sizeof(idx));
    //pkgdir->flags |= pkgdir_flags;
    pkgdir->pkgroups = pkgroups;
    
// l_end:
    if (nerr)
        idx_close(&idx);
    
    return nerr == 0;
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
#if 0
struct nbuf_header {
    tn_buf *nbuf;
    Header h;
};

void nbuf_header_free(struct nbuf_header *hdr) 
{
    if (hdr->h)
        headerFree(hdr->h);
    
    if (hdr->nbuf)
        n_buf_free(hdr->nbuf);
}
#endif

static
Header do_loadrpmhdr(const char *path, int vfmode, const char *pdir_name)
{
    struct vfile       *vf = NULL;
    tn_buf             *nbuf;
    tn_buf             buf[4096];
    int                n;
    Header             h, ch = NULL;
    
    if ((vf = vfile_open_sl(path, VFT_GZIO, vfmode, pdir_name)) == NULL)
        return NULL;

    nbuf = n_buf_new(1024 * 64);
    while ((n = gzread(vf->vf_gzstream, buf, sizeof(buf))) > 0)
        n_buf_write(nbuf, buf, n);
    vfile_close(vf);

    h = headerLoad(n_buf_ptr(nbuf)); /* rpm's memleak */
    if (h == NULL) {
        logn(LOGERR, "%s: load header failed", n_basenam(path));
        
    } else if (headerIsEntry(h, RPMTAG_SOURCEPACKAGE)) { /* omit src.rpms */
        h = NULL;
    }
    
    if (h)
        ch = headerCopy(h);
    n_buf_free(nbuf);
    return ch;
}

static
struct pkg *do_loadpkg(tn_alloc *na, Header h, int ldflags, const char *pkgfn) 
{
    struct pkg *pkg;
    if ((pkg = pm_rpm_ldhdr(NULL, h, pkgfn, 0, PKG_LDWHOLE))) {
        if (ldflags & PKGDIR_LD_DESC) {
            pkg->pkg_pkguinf = pkguinf_ldrpmhdr(na, h);
            pkg_set_ldpkguinf(pkg);
        }
    }

    return pkg;
}

static 
struct pkguinf *load_pkguinf(tn_alloc *na, const struct pkg *pkg, void *ptr)
{
    unsigned        vfmode = VFM_RO | VFM_CACHE | VFM_NOEMPTY;
    struct pkguinf  *pkgu = NULL;
    char            *nvr = ptr;
    char            path[PATH_MAX];
    Header          h;
    

    if (!pkg->pkgdir)
        return NULL;
    
    n_snprintf(path, sizeof(path), "%s/%s.hdr", pkg->pkgdir->path, nvr);
    
    pkg = pkg;
    if ((h = do_loadrpmhdr(path, vfmode, n_basenam(path)))) {
        pkgu = pkguinf_ldrpmhdr(na, h);
        headerFree(h);
    }

    return pkgu;
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
    struct pkg         *pkg;
    unsigned           vfmode = VFM_RO | VFM_CACHE | VFM_NOEMPTY;
    int i;

    idx = pkgdir->mod_data;
    for (i=0; i < n_array_size(idx->ents); i++) {
        struct yum_entry *en = n_array_nth(idx->ents, i);
        char path[PATH_MAX];
        Header h;

        n_snprintf(path, sizeof(path), "%s/%s.hdr", pkgdir->path, en->nvr);

        if ((h = do_loadrpmhdr(path, vfmode, pkgdir->name))) {
            pkg = do_loadpkg(pkgdir->na, h, ldflags, en->fn);
            headerFree(h);
            if (pkg) {
                pkg->pkgdir = pkgdir;
                 /* .hdr */
                pkg->pkgdir_data = pkgdir->na->na_malloc(pkgdir->na,
                                                         strlen(en->nvr) + 1); 
                memcpy(pkg->pkgdir_data, en->nvr, strlen(en->nvr) + 1);
                pkg->pkgdir_data_free = pkg_data_free;
                pkg->load_pkguinf = load_pkguinf;
                n_array_push(pkgdir->pkgs, pkg);
                if (n_array_size(pkgdir->pkgs) > 100)
                    break;
            }
        }
    }

    return n_array_size(pkgdir->pkgs);
}

static
int yum_update(const char *path, int vfmode)
{
    struct vfile    *vf;
    int                  rc = 1;
    
    if ((vf = vfile_open(path, VFT_IO, vfmode)) == NULL)
        return 0;

    vfile_close(vf);
    return rc;
}


static
int do_update_a(const struct source *src, const char *idxpath)
{
    int vfmode;

    src = src;                  /* unused */
    vfmode = VFM_RO | VFM_NOEMPTY | VFM_NODEL;
    return yum_update(idxpath, vfmode);
}

static 
int do_update(struct pkgdir *pkgdir, int *npatches) 
{
    int vfmode;

    npatches = npatches;
    
    vfmode = VFM_RO | VFM_NOEMPTY | VFM_NODEL | VFM_CACHE_NODEL;
    return yum_update(pkgdir->idxpath, vfmode);
}
