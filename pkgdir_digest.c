/* 
  Copyright (C) 2000 - 2002 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_GETLINE
# define _GNU_SOURCE 1
#else
# error "getline() is needed, sorry"
#endif


#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>
#include <fcntl.h>

#include <openssl/evp.h>
#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>

#include <vfile/vfile.h>

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "pkgdir.h"
#include "pkg.h"
#include "pkgroup.h"

const char *pdigest_ext = ".mdd";
const char *pdigest_ext_v016 = ".md";

static int pdigest_read(struct pdigest *pdg, struct vfile *vfmd);


struct pdigest *pdigest_new(const char *path, int vfmode, int v016compat) 
{
    struct pdigest    *pdg;
    struct vfile      *vf = NULL; 
    char              mdpath[PATH_MAX];
    unsigned          mode = 0;
    
    if (path != NULL) {
        int n;
        const char *ext = pdigest_ext;

        if (v016compat == 0) {
            mode |= PDIGEST_MODE_DEFAULT;
            
        } else {
            mode |= PDIGEST_MODE_v016;
            ext = pdigest_ext_v016;
        }
        
        n = mkdigest_path(mdpath, sizeof(mdpath), path, ext);
        vfmode |= VFM_NOEMPTY;
        if ((vf = vfile_open(mdpath, VFT_IO, vfmode)) == NULL) 
            return NULL;
    }
    
    pdg = malloc(sizeof(*pdg));
    memset(pdg, 0, sizeof(*pdg));

    pdg->mode = mode;
    pdg->vf = vf;
    pdg->md = NULL;

    if (vf) 
        if (!pdigest_read(pdg, NULL)) {
            pdigest_free(pdg);
            pdg = NULL;
        }
    
    return pdg;
}


int pdigest_fill(struct pdigest *pdg, char *mdbuf, int size) 
{
    int req_size;

    n_assert(pdg->md == NULL);
    n_assert(*pdg->mdd == '\0');
    n_assert(*pdg->mdh == '\0');
    
    req_size = PDIGEST_SIZEx2;
    if (pdg->mode & PDIGEST_MODE_v016)
        req_size = PDIGEST_SIZE;
    
    if (size < req_size)
        return 0;

    if (pdg->mode & PDIGEST_MODE_v016) {
        pdg->md = malloc(PDIGEST_SIZE + 1);
        memcpy(pdg->md, mdbuf, PDIGEST_SIZE);
        pdg->md[PDIGEST_SIZE] = '\0';
        
    } else {
        char *p = &mdbuf[PDIGEST_SIZE];
        memcpy(pdg->mdd, mdbuf, PDIGEST_SIZE);
        pdg->mdd[PDIGEST_SIZE] = '\0';
        memcpy(pdg->mdh, p, PDIGEST_SIZE);
        pdg->mdh[PDIGEST_SIZE] = '\0';
    }
    
    return 1;
}

void pdigest_init(struct pdigest *pdg) 
{
    memset(pdg, 0, sizeof(*pdg));
    pdg->vf = NULL;
    pdg->mode = 0;
    pdg->md = NULL;
}

    
void pdigest_destroy(struct pdigest *pdg) 
{
    if (pdg->md) {
        free(pdg->md);
        pdg->md = NULL;
    }

    if (pdg->vf) {
        vfile_close(pdg->vf);
        pdg->vf = NULL;
    }
}



void pdigest_free(struct pdigest *pdg) 
{
    pdigest_destroy(pdg);
    memset(pdg, 0, sizeof(*pdg));
    free(pdg);
}


int pdigest_readfd(struct pdigest *pdg, int fd, const char *path) 
{
    char buf[PDIGEST_SIZEx2];
    int md_size, req_size;
    
    if (lseek(fd, 0L, SEEK_SET) != 0) {
        logn(LOGERR, "%s: lseek(0): %m", path);
        return 0;
    }
    
    md_size = read(fd, buf, sizeof(buf));

    req_size = PDIGEST_SIZEx2;
    if (pdg->mode & PDIGEST_MODE_v016)
        req_size = PDIGEST_SIZE;
    
    if (md_size < req_size) {
        logn(LOGERR, _("%s: broken digest file (%d)"), path, md_size);
        return 0;
    }
    
    return pdigest_fill(pdg, buf, md_size);
}


static
int pdigest_read(struct pdigest *pdg, struct vfile *vfmd) 
{
    if (vfmd == NULL)
        vfmd = pdg->vf;
    
    if (vfmd == NULL)
        return 0;
    
    return pdigest_readfd(pdg, vfmd->vf_fd, vfmd->vf_path);
}


static
int hdr_digest(FILE *stream, unsigned char *md, int *md_size, EVP_MD_CTX *_ctx)
{
    int             line_size = 0, nread, len, endvhdr_found = 0;
    unsigned char   buf[256];
    char            *linebuf = NULL;
    EVP_MD_CTX      ctx;
    int             n;

    
    n_assert(md_size && *md_size);
    EVP_DigestInit(&ctx, EVP_sha1());

    len = strlen(pdir_tag_endvarhdr);
    n = 0;

    while ((nread = getline(&linebuf, &line_size, stream)) > 0) {
        char *p = linebuf;

        EVP_DigestUpdate(&ctx, linebuf, nread);
        if (_ctx)
            EVP_DigestUpdate(_ctx, linebuf, nread);
        n++;
        
        if (*p == '#')
            continue;

        if (*p == '%')
            p++;
        
        if (strncmp(p, pdir_tag_endvarhdr, len) == 0) {
            endvhdr_found = 1;
            break;
        }

        if (n > 100)            /* no more than 100 line long hdr */
            break;
    }

    if (linebuf)
        free(linebuf);

    EVP_DigestFinal(&ctx, buf, &n);
    
    if (!endvhdr_found) {
        logn(LOGERR, _("broken index"));
        return 0;
    }
    
    if (n > *md_size) {
        *md_size = 0;
        *md = '\0';
        
    } else {
        memcpy(md, buf, n);
        *md_size = n;
    }
    
    return *md_size;
}

static
int digest(FILE *stream, unsigned char *md, int *md_size, EVP_MD_CTX *_ctx)
{
    unsigned char buf[16*1024];
    EVP_MD_CTX ctx;
    int n, nn = 0;


    n_assert(md_size && *md_size);

    EVP_DigestInit(&ctx, EVP_sha1());

    while ((n = fread(buf, 1, sizeof(buf), stream)) > 0) {
        EVP_DigestUpdate(&ctx, buf, n);
        if (_ctx)
            EVP_DigestUpdate(_ctx, buf, n);
        nn += n; 
    }
    
    EVP_DigestFinal(&ctx, buf, &n);

    if (n > *md_size) {
        *md = '\0';
        *md_size = 0;
    } else {
        memcpy(md, buf, n);
        *md_size = n;
    }
    
    return *md_size;
}

#define CALC_MDD (1 << 0)       /* >= 0.17   */
#define CALC_MD  (1 << 1)       /* <= 0.16.x */
#define CALC_DEFAULT CALC_MD | CALC_MDD
static 
int pdigest_calc(struct pdigest *pdg, FILE *stream, unsigned flags) 
{
    unsigned char   mdh[64], mdd[64], md[64], mdhex[64];
    int             mdh_size = sizeof(mdh), mdd_size = sizeof(mdd),
                    md_size = sizeof(md);
    EVP_MD_CTX      ctx, *ctxp;
    int             is_err = 0, n;

    
    *mdh = '\0';
    *mdd = '\0';
    *md = '\0';
    
    n_assert(flags & (CALC_MD | CALC_MDD));
    
    ctxp = NULL;
    if (flags & CALC_MD) {
        EVP_DigestInit(&ctx, EVP_sha1());
        ctxp = &ctx;
    }
    
    if ((flags & CALC_MDD) == 0) { /* no separate header && body digests */
        if (!digest(stream, mdd, &mdd_size, ctxp)) {
            if (ctxp)
                EVP_DigestFinal(&ctx, md, &md_size);
            return 0;
        }
        
    } else {
        if (!hdr_digest(stream, mdh, &mdh_size, ctxp)) {
            if (ctxp)
                EVP_DigestFinal(&ctx, md, &md_size);
            return 0;
        }
        
        if (!digest(stream, mdd, &mdd_size, ctxp)) {
            if (ctxp)
                EVP_DigestFinal(&ctx, md, &md_size);
            return 0;
        }
    }
    
    if (flags & CALC_MDD) { /* separate header && body digests */
        n = bin2hex(pdg->mdh, sizeof(pdg->mdh), mdh, mdh_size);
        if (n != PDIGEST_SIZE)
            is_err = 1;
    
        n = bin2hex(pdg->mdd, sizeof(pdg->mdd), mdd, mdd_size);
        if (n != PDIGEST_SIZE)
            is_err = 1;
    }
    
    if (ctxp) {
        EVP_DigestFinal(&ctx, md, &md_size);
        n = bin2hex(mdhex, sizeof(mdhex), md, md_size);
        if (n != PDIGEST_SIZE)
            is_err = 1;
        else
            pdg->md = strdup(mdhex);
    }
    
    return is_err == 0;
}


int mkdigest_path(char *path, int size, const char *pathname, const char *ext)
{
    char *p; 
    int n;
    
    n = n_snprintf(path, size, "%s", pathname);
    if ((p = strrchr(n_basenam(path), '.')) == NULL)
        p = &path[n];

    /* don't touch .md[d] files */
    else if (strncmp(p, ".md", 3) == 0 || strncmp(p, ".mdd", 4) == 0) 
        return n;
    
    else if (strcmp(p, ".gz") != 0)
        p = &path[n];
    
    else
        n -= 3;
    
    n += n_snprintf(p, size - (p - path), "%s", ext);
    return n;
}


int pdigest_save(struct pdigest *pdg, const char *pathname) 
{
    char            path[PATH_MAX];
    struct vfile    *vf;
    int             n;
    

    n = mkdigest_path(path, sizeof(path), pathname, pdigest_ext);
    if (n <= 4) {
        logn(LOGERR, "%s: path too short", path);
        return 0;
    }
    
    if ((vf = vfile_open(path, VFT_STDIO, VFM_RW)) == NULL)
        return 0;
    
    fprintf(vf->vf_stream, "%s%s", pdg->mdd, pdg->mdh);
    vfile_close(vf);

    if (pdg->md) {
        path[n - 1] = '\0';             /* eat last 'd' */
        if ((vf = vfile_open(path, VFT_STDIO, VFM_RW)) == NULL)
            return 0;
        fprintf(vf->vf_stream, "%s", pdg->md);
        vfile_close(vf);
    }
    
    return 1;
}


int pdigest_verify(struct pdigest *pdg, struct vfile *vf)
{
    struct pdigest        pdg2;
    off_t                 offs;
    int                   rc = 0;
    unsigned              calcflags = 0;

    
    msg(0, _("Verifying %s..."), vf_url_slim_s(vf->vf_path, 0));
    offs = ftell(vf->vf_stream);
    if (fseek(vf->vf_stream, 0L, SEEK_SET) != 0) {
        logn(LOGERR, "%s: fseek(0): %ld -> 0: %m", vf->vf_path, offs);
        return 0;
    }

    pdigest_init(&pdg2);
    if (pdg->mode & PDIGEST_MODE_v016)
        calcflags |= CALC_MD;
    else
        calcflags |= CALC_DEFAULT;
    
    if (!pdigest_calc(&pdg2, vf->vf_stream, calcflags)) {
        rc = 0;
        goto l_end;
    }
    
    if (fseek(vf->vf_stream, offs, SEEK_SET) != 0) {
        logn(LOGERR, "%s: fseek(%ld): %m", vf->vf_path, offs);
        rc = 0;
        goto l_end;
    }

    if (pdg->mode & PDIGEST_MODE_v016) {
        n_assert(pdg->md);
        n_assert(pdg2.md);
        rc = (strcmp(pdg->md, pdg2.md) == 0);
    
    } else 
        rc = (memcmp(pdg->mdd, pdg2.mdd, sizeof(pdg->mdd)) == 0 &&
              memcmp(pdg->mdh, pdg2.mdh, sizeof(pdg->mdh)) == 0);
    
    msg(0, "_ %s\n", rc ? "OK" : _("BROKEN"));
    
 l_end:
    pdigest_destroy(&pdg2);
    return rc;
}


int i_pkgdir_creat_digest(struct pkgdir *pkgdir, const char *pathname,
                          int with_md)
{
    char            path[PATH_MAX];
    struct vfile    *vf;
    int             rc = 1;
    unsigned        calcflags = CALC_MDD;

    
    if ((vf = vfile_open(pathname, VFT_STDIO, VFM_RO)) == NULL)
        return 0;

    if (pkgdir->pdg == NULL)
        pkgdir->pdg = pdigest_new(NULL, 0, 0);
    else 
        pdigest_destroy(pkgdir->pdg);
        
    mkdigest_path(path, sizeof(path), pathname, pdigest_ext);
    msgn(1, _("Writing digest %s..."), path);

    if (with_md && (pkgdir->flags & PKGDIR_DIFF) == 0)
        calcflags |= CALC_MD;
    
    if ((rc = pdigest_calc(pkgdir->pdg, vf->vf_stream, calcflags))) {
        pdigest_save(pkgdir->pdg, pathname);
        
        
        if (pkgdir->flags & PKGDIR_PATCHED) {
            n_assert(pkgdir->mdd_orig);
            if (strcmp(pkgdir->mdd_orig, pkgdir->pdg->mdd) == 0) {
                i_pkgdir_creat_md5(pathname);
                
            } else {
                rc = 0;
                logn(LOGERR, "%s, %s", pkgdir->pdg->mdd, pkgdir->mdd_orig);
                logn(LOGERR, _("%s: desynchronized index"), vf_url_slim_s(pkgdir->path, 0));
            }
        }
    }
    
    vfile_close(vf);
    return rc; 
}


int i_pkgdir_creat_md5(const char *pathname) 
{
    FILE            *stream;
    unsigned char   md[128];
    char            path[PATH_MAX];
    int             md_size = sizeof(md);

    
    if ((stream = fopen(pathname, "r")) == NULL) {
        //log(LOGERR, "%s: %m\n", pathname);
        return 0;
    }
    	
    snprintf(path, sizeof(path), "%s.md5", pathname);
    msg(2, "Writing md5 file...\n");
    
    mhexdigest(stream, md, &md_size, DIGEST_MD5);
    fclose(stream);
    
    if (md_size) {
        FILE *f;
        
        if ((f = fopen(path, "w")) == NULL)
            return 0;
        fprintf(f, "%s", md);
        fclose(f);
    }

    return md_size;
}


int i_pkgdir_verify_md5(const char *title, const char *pathname) 
{
    FILE            *stream;
    unsigned char   md1[DIGEST_SIZE_MD5 + 1], md2[DIGEST_SIZE_MD5 + 1];
    int             fd, md1_size, md2_size, rc;
    char            path[PATH_MAX];

    snprintf(path, sizeof(path), "%s.md5", pathname);
    if ((fd = open(path, O_RDONLY)) < 0)
        return 0;
    
    md2_size = read(fd, md2, sizeof(md2));
    close(fd);

    if ((stream = fopen(pathname, "r")) == NULL)
        return 0;
    
    if (md2_size != DIGEST_SIZE_MD5)
        return 0;
    
    
    if (title == NULL || *title == '\0')
        title = pathname;
    
    msgn(3, _("Verifying integrity of %s..."), vf_url_slim_s(title, 0));
    md1_size = sizeof(md1);
    mhexdigest(stream, md1, &md1_size, DIGEST_MD5);
    fclose(stream);
    
    rc = md1_size == DIGEST_SIZE_MD5 && md1_size == md2_size &&
        memcmp(md1, md2, DIGEST_SIZE_MD5) == 0;
    return rc;
}


