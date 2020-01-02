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

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>
#include <fcntl.h>
#include <sys/param.h>          /* for PATH_MAX */

#include <openssl/evp.h>
#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>
#include <trurl/nmalloc.h>
#include <trurl/n_snprintf.h>
#include <vfile/vfile.h>

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "pndir.h"

const char *pndir_digest_ext = ".md";
static const char *option_brandnew = "brand-new";

static int pndir_digest_read(struct pndir_digest *pdg, struct vfile *vfmd);


struct pndir_digest *pndir_digest_new(const char *path, int vfmode,
                                      const char *srcnam)
{
    struct pndir_digest  *pdg;
    struct vfile        *vf = NULL;
    char                mdpath[PATH_MAX];

    if (path != NULL) {
        const char *ext = pndir_digest_ext;

        pndir_mkdigest_path(mdpath, sizeof(mdpath), path, ext);
        vfmode |= VFM_NOEMPTY;
        if ((vf = vfile_open_ul(mdpath, VFT_IO, vfmode, srcnam)) == NULL)
            return NULL;
    }

    pdg = n_malloc(sizeof(*pdg));
    memset(pdg, 0, sizeof(*pdg));

    pdg->vf = vf;

    if (vf) {
        if (!pndir_digest_read(pdg, NULL)) {
            pndir_digest_free(pdg);
            pdg = NULL;
        }
    }

    return pdg;
}

void pndir_digest_init(struct pndir_digest *pdg)
{
    memset(pdg, 0, sizeof(*pdg));
    pdg->vf = NULL;
}


void pndir_digest_destroy(struct pndir_digest *pdg)
{
    if (pdg->vf) {
        vfile_close(pdg->vf);
        pdg->vf = NULL;
    }
}

void pndir_digest_free(struct pndir_digest *pdg)
{
    pndir_digest_destroy(pdg);
    memset(pdg, 0, sizeof(*pdg));
    free(pdg);
}

static int fill_digest(struct pndir_digest *pdg, char *mdbuf, int size)
{
    int req_size;

    n_assert(*pdg->md == '\0');

    req_size = TNIDX_DIGEST_SIZE;

    if (size < req_size)
        return 0;

    memcpy(pdg->md, mdbuf, TNIDX_DIGEST_SIZE);
    pdg->md[TNIDX_DIGEST_SIZE] = '\0';

    return 1;
}


int pndir_digest_readfd(struct pndir_digest *pdg, int fd, const char *path)
{
    char buf[2 * TNIDX_DIGEST_SIZE]; /* +40bytes for params  */
    int nread, req_size;

    if (lseek(fd, 0L, SEEK_SET) != 0) {
        logn(LOGERR, "%s: lseek(0): %m", path);
        return 0;
    }

    memset(buf, 0, sizeof(buf));
    nread = read(fd, buf, sizeof(buf));
    req_size = TNIDX_DIGEST_SIZE;

    if (nread < req_size) {
        logn(LOGERR, _("%s: broken digest file (%d)"), path, nread);
        return 0;
    }

    if (!fill_digest(pdg, buf, nread))
        return 0;

    buf[nread] = '\0';
    DBGF("read %s\n", buf);

    const char *opts = &buf[TNIDX_DIGEST_SIZE];

    if (strstr(opts, option_brandnew))
        pdg->flags |= PNDIGEST_BRANDNEW;

    if (strstr(opts, COMPR_ZST))
        n_strncpy(pdg->compr, COMPR_ZST, sizeof(pdg->compr));
    else if (strstr(opts, COMPR_GZ))
        n_strncpy(pdg->compr, COMPR_GZ, sizeof(pdg->compr));
    else if (strstr(opts, COMPR_NONE))
        n_strncpy(pdg->compr, COMPR_NONE, sizeof(pdg->compr));
    else
        n_strncpy(pdg->compr, COMPR_GZ, sizeof(pdg->compr)); /* backward compat. */

    return 1;
}


static
int pndir_digest_read(struct pndir_digest *pdg, struct vfile *vfmd)
{
    if (vfmd == NULL)
        vfmd = pdg->vf;

    if (vfmd == NULL)
        return 0;

    return pndir_digest_readfd(pdg, vfmd->vf_fd, vfmd->vf_path);
}

static int do_mkdigest_path(char *path, int size, const char *pathname,
                        const char *ext, char **compr)
{
    char *p, *q = NULL;
    int n;

    n = n_snprintf(path, size, "%s", pathname);

    p = strrchr(n_basenam(path), '.');
    if (p)
        q = p + 1;

    if (compr && q) {
        if (n_str_eq(q, COMPR_GZ))
            *compr = COMPR_GZ;
        else if (n_str_eq(q, COMPR_ZST))
            *compr = COMPR_ZST;
        else
            *compr = COMPR_NONE;
    }

    if (p == NULL) { /* no extension? */
        p = &path[n];

    } else if (strncmp(p, ".md", 3) == 0) { /* already a digest path */
        return n;

    } else if (!n_str_in(q, COMPR_GZ, COMPR_ZST, NULL)) {
        p = &path[n];

    } else {
        n -= 3;
    }

    n += n_snprintf(p, size - (p - path), "%s", ext);
    DBGF("%s, %s -> %s\n", pathname, ext, path);
    return n;
}

int pndir_mkdigest_path(char *path, int size, const char *pathname,
                        const char *ext)
{
    return do_mkdigest_path(path, size, pathname, ext, NULL);
}

int pndir_digest_save(struct pndir_digest *pdg, const char *pathname,
                      const struct pkgdir *pkgdir)
{
    char            path[PATH_MAX];
    struct vfile    *vf;
    int             n, brandnew = 0;
    char            *compr = NULL;

    /* no patch was generated based on this pkgdir nor patch
       applied to (client side) */
    if ((pkgdir->flags & (PKGDIR_DIFFED | PKGDIR_PATCHED)) == 0)
        brandnew = 1;

    n = do_mkdigest_path(path, sizeof(path), pathname, pndir_digest_ext, &compr);
    if (n <= 4) {
        logn(LOGERR, "%s: path too short", path);
        return 0;
    }

    if ((vf = vfile_open_ul(path, VFT_STDIO, VFM_RW, pkgdir->name)) == NULL)
        return 0;

    DBGF("brandnew %s %d\n", pkgdir_idstr(pkgdir), brandnew);
    if (!compr)
        compr = COMPR_NONE;

    fprintf(vf->vf_stream, "%s %s%s%s", pdg->md,
            brandnew ? option_brandnew : "",
            compr && brandnew ? "," : "",
            compr ? compr : "");

    vfile_close(vf);

    return 1;
}


int pndir_digest_calc_pkgs(struct pndir_digest *pdg, tn_array *pkgs)
{
    tn_array *keys;
    char     key[512];
    int      i, klen;


    keys = n_array_new(n_array_size(pkgs), free, (tn_fn_cmp)strcmp);

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        klen = pndir_make_pkgkey(key, sizeof(key), pkg);
        n_array_push(keys, n_strdupl(key, klen));
    }

    i = pndir_digest_calc(pdg, keys);
    n_array_free(keys);
    return i;
}


int pndir_digest_calc(struct pndir_digest *pdg, tn_array *keys)
{
    unsigned char md[256];
    EVP_MD_CTX *ctx;
    unsigned n;
    int i;


    ctx = EVP_MD_CTX_create();
    EVP_DigestInit(ctx, EVP_sha1());
    EVP_DigestUpdate(ctx, "md", strlen("md"));

    if (keys && n_array_size(keys)) {
        n_array_sort(keys);

        for (i=0; i < n_array_size(keys); i++) {
            char *key = n_array_nth(keys, i);
            DBGF("key = %s\n", key);
            EVP_DigestUpdate(ctx, key, strlen(key));
        }
    }

    EVP_DigestFinal(ctx, md, &n);
    EVP_MD_CTX_destroy(ctx);

    if (n > (int)sizeof(pdg->md))
        return 0;

    DBGF("digest = %d, %d\n", n, (int)sizeof(pdg->md));

    bin2hex(pdg->md, sizeof(pdg->md), md, n);
    DBGF("digest = %s\n", pdg->md);

    return n;
}
