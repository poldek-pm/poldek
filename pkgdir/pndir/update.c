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

#include <vfile/vfile.h>

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "pkgdir.h"
#include "pkg.h"
#include "pndir.h"

static char *eat_zlib_ext(char *path) 
{
    char *p;
    
    if ((p = strrchr(n_basenam(path), '.')) != NULL) 
        if (strcmp(p, ".gz") == 0)
            *p = '\0';

    return path;
}


static
int is_uptodate(const char *path, const struct pndir_digest *dg_local,
                struct pndir_digest *dg_remote)
{
    char                   mdpath[PATH_MAX], mdtmpath[PATH_MAX];
    struct pndir_digest    remote_dg;
    int                    fd, n, rc = 0;
    const char             *ext = pndir_digest_ext;

    
    if (dg_remote)
        pndir_digest_init(dg_remote);
    
    pndir_digest_init(&remote_dg);
    
    if (vf_url_type(path) & VFURL_LOCAL)
        return 1;
    
    if (!(n = vf_mksubdir(mdtmpath, sizeof(mdtmpath), "tmpmd"))) {
        rc = -1;
        goto l_end;
    }

    pndir_mkdigest_path(mdpath, sizeof(mdpath), path, ext);
    
    snprintf(&mdtmpath[n], sizeof(mdtmpath) - n, "/%s", n_basenam(mdpath));
    unlink(mdtmpath);
    mdtmpath[n] = '\0';

    if (!vf_fetch(mdpath, mdtmpath)) {
        rc = -1;
        goto l_end;
    }
    
    mdtmpath[n] = '/';
    
    if ((fd = open(mdtmpath, O_RDONLY)) < 0 ||
        !pndir_digest_readfd(&remote_dg, fd, mdtmpath)) {
        
        close(fd);
        rc = -1;
        goto l_end;
    }
    close(fd);

    rc = (memcmp(dg_local->md, &remote_dg.md, sizeof(remote_dg.md)) == 0);
    
    if (!rc && dg_remote)
        memcpy(dg_remote, &remote_dg, sizeof(remote_dg));
    
 l_end:
    pndir_digest_destroy(&remote_dg);
    return rc;
}



static
int update_whole_idx(const struct source *src) 
{
    struct pkgdir *pkgdir;
    int rc = 0;
    
    if ((pkgdir = pkgdir_srcopen(src, PKGDIR_OPEN_REFRESH))) {
        pkgdir_free(pkgdir);
        rc = 1;
    }
    
    return rc;
}


int pndir_m_update_a(const struct source *src, const char *idxpath)
{
    struct pndir   *idx;
    struct pkgdir  *pkgdir;
    int            rc = 0;

    idxpath = idxpath;          /* unused */
    pkgdir = pkgdir_srcopen(src, 0);
    
    if (pkgdir == NULL)
        return update_whole_idx(src);
    idx = pkgdir->mod_data;
    
    if (idx->_vf->vf_flags & VF_FETCHED) {
        pkgdir_free(pkgdir);
        return 1;
    }
    
    switch (is_uptodate(idx->idxpath, idx->dg, NULL)) {
        case 1:
            rc = 1;
            break;
            
        case -1:
        case 0:
            rc = update_whole_idx(src);
            break;
                
        default:
            n_assert(0);
    }

    pkgdir_free(pkgdir);
    return rc;
}

static int parse_toc_line(char *line, time_t *tsp, char **mdp) 
{
    char *p, *md;
    time_t ts;

    p = line;

    *tsp = 0;
    *mdp = NULL;
    
    while (*p && isspace(*p))
        p++;
        
    if (*p == '#')
        return 1;
    

    if ((p = strchr(p, ' ')) == NULL)
        return 0;
    
    while (*p && isspace(*p))
        *p++ = '\0';
        
    if (sscanf(p, "%lu", &ts) != 1) /* read ts */
        return 0;
    
    if ((p = strchr(p, ' ')) == NULL)
        return 0;

    while (*p && isspace(*p))
        *p++ = '\0';
        
    md = p;                 /* read orig md */
    if ((p = strchr(p, ' ')) == NULL)
        return 0;
    
    *p = '\0';
        
    if (p - md != TNIDX_DIGEST_SIZE)
        return 0;

    return 0;
}


int pndir_m_update(struct pkgdir *pkgdir, int *npatches) 
{
    char                idxpath[PATH_MAX], tmpath[PATH_MAX], path[PATH_MAX];
    struct vfile        *vf;
    struct pndir_digest dg_remote;
    struct pndir        *idx;
    char                line[1024], *dn, *bn;
    int                 nread, nerr = 0, rc, npatch, first_patch_found;
    const char          *errmsg_broken_difftoc = _("%s: broken patch list");
    char                current_md[TNIDX_DIGEST_SIZE + 1];

    idx = pkgdir->mod_data;
    if (idx->_vf->vf_flags & VF_FETCHED)
        return 1;
    
    switch (is_uptodate(pkgdir->idxpath, idx->dg, &dg_remote)) {
        case 1:
            rc = 1;
            //if ((pkgdir->flags & PKGDIR_VERIFIED) == 0)
            //    rc = pndir_digest_verify(idx->dg, idx->vf);
            return rc;
            break;
            
        case -1:
            return 0;
            
        case 0:
            break;

        default:
            n_assert(0);
    }

    /* open diff toc */
    snprintf(idxpath, sizeof(idxpath), "%s", pkgdir->idxpath);
    eat_zlib_ext(idxpath);
    snprintf(tmpath, sizeof(tmpath), "%s", idxpath);
    n_basedirnam(tmpath, &dn, &bn);
    snprintf(path, sizeof(path), "%s/%s/%s%s", dn,
             pndir_packages_incdir, bn, pndir_difftoc_suffix);
    
    vf = vfile_open_sl(path, VFT_TRURLIO, VFM_RO, pkgdir_idstr(pkgdir));
    if (vf == NULL)
        return 0;

    if (npatches)
        *npatches = 0;
    
    n_assert(strlen(idx->dg->md) == TNIDX_DIGEST_SIZE);
    memcpy(current_md, idx->dg->md, TNIDX_DIGEST_SIZE + 1);

    first_patch_found = 0;
    npatch = 0;
    while ((nread = n_stream_gets(vf->vf_tnstream, line, sizeof(line))) > 0) {
        struct pkgdir *diff;
        char *md;
        time_t ts;

        if (!parse_toc_line(line, &ts, &md)) {
            logn(LOGERR, errmsg_broken_difftoc, path);
            nerr++;
            break;
        }

        if (md == NULL) /* i.e comment */
            continue;

        if (ts <= pkgdir->ts)
            continue;
        
        if (!first_patch_found) {
            if (memcmp(md, current_md, TNIDX_DIGEST_SIZE) == 0)
                first_patch_found = 1;
            else {
                if (verbose > 2) {
                    logn(LOGERR, "ts = %ld, %ld", pkgdir->ts, ts);
                    logn(LOGERR, "md dir  %s", idx->dg->md);
                    logn(LOGERR, "md last %s", md);
                    logn(LOGERR, "md curr %s", current_md);
                }
                logn(LOGERR, _("%s: no patches available"), pkgdir_pr_idxpath(pkgdir));
                nerr++;
                break;
            }
        }
        
        msg(1, "_\n");
        snprintf(path, sizeof(path), "%s/%s/%s", dn, pndir_packages_incdir, line);
        diff = pkgdir_open_ext(path, NULL, pkgdir->type, "diff", NULL,
                               PKGDIR_OPEN_DIFF, pkgdir->lc_lang);
        if (diff == NULL) {
            nerr++;
            break;
        }
        
        if ((pkgdir->flags & PKGDIR_LOADED) == 0) {
            if (!pkgdir_load(pkgdir, NULL, 0)) {
                logn(LOGERR, _("%s: load failed"), pkgdir->idxpath);
                nerr++;
                break;
            }
        }
        msgn(1, _("Applying %s..."), n_basenam(diff->idxpath));
        pkgdir_load(diff, NULL, 0);
        pkgdir_patch(pkgdir, diff);
        pkgdir_free(diff);

        npatch++;
    }
    
    vfile_close(vf);
    //nerr++;
    /* outdated and no patches || package duplicates */
    if (nerr || npatch == 0 || pkgdir__uniq(pkgdir) > 0) {
        nerr++;
        
    } else {
        struct pndir_digest dg;

        pndir_digest_calc_pkgs(&dg, pkgdir->pkgs);
        if (memcmp(dg.md, dg_remote.md, sizeof(dg.md)) != 0) {
            logn(LOGWARN, _("%s: desynchronized index, try --upa"),
                 pkgdir_pr_idxpath(pkgdir));
            nerr++;
        }
    }
    
    
    if (nerr == 0) {
        snprintf(path, sizeof(path), "%s/%s", dn, pndir_packages_incdir);
        if (vf_localdirpath(tmpath, sizeof(tmpath), path) < (int)sizeof(tmpath)) {
            verbose--; /* verbosity need to be reorganized... */
            pkgdir__rmf(tmpath, NULL);
            verbose++;
        }
        msg(1, "_\n");
    }
    
    if (npatches)
        *npatches = npatch;

    return nerr == 0;
}
