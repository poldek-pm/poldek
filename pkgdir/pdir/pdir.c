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

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pkgdir.h"
#include "pdir.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgfl.h"
#include "pkgmisc.h"
#include "pkgroup.h"

int pdir_v016compat = 0;              /* public */
struct pkg_data {
    off_t          nodep_files_offs;  /* no dep files offset in index */
    off_t          pkguinf_offs;
    struct vfile   *vf;
};

static tn_array *parse_removed(char *str);
static int is_uptodate(const char *path, const struct pdir_digest *pdg_local,
                       struct pdir_digest *pdg_remote, const char *pdir_name);

static int do_open(struct pkgdir *pkgdir, unsigned flags);
static int do_load(struct pkgdir *pkgdir, unsigned ldflags);
static int do_update(struct pkgdir *pkgdir, int *npatches);
static int do_update_a(const struct source *src, const char *idxpath);
//static int do_unlink(const char *path, unsigned flags);
static void do_free(struct pkgdir *pkgdir);

const char *pdir_localidxpath(struct pkgdir *pkgdir);

static
int posthook_diff(struct pkgdir *pd1, struct pkgdir* pd2, struct pkgdir *diff);

static char *aliases[] = { "pidx", NULL };

struct pkgdir_module pkgdir_module_pdir = {
    NULL, 
    PKGDIR_CAP_UPDATEABLE_INC | PKGDIR_CAP_UPDATEABLE, 
    "pdir",
    (char **)aliases,
    "Native poldek's index format prior to 0.20 version",
    
    "packages.dir",
    "gz",
    
    do_open,
    do_load,
    pdir_create,
    do_update, 
    do_update_a,
    NULL,
    do_free,
    pdir_localidxpath,
    posthook_diff, 
};

const char *pdir_localidxpath(struct pkgdir *pkgdir)
{
    struct pdir *idx = pkgdir->mod_data;
    
    if (idx && idx->vf) 
        return vfile_localpath(idx->vf);
    return pkgdir->idxpath;
}


static
int posthook_diff(struct pkgdir *pd1, struct pkgdir *pd2, struct pkgdir *diff)
{
	struct pdir *idx, *idx2;

    pd2 = pd2;                  /* unused */
	if ((idx2 = pd1->mod_data) == NULL)
		return 0;
	
	idx = diff->mod_data;
	
	if (idx == NULL) {
        idx = n_malloc(sizeof(*idx));
        pdir_init(idx);
        diff->mod_data = idx;
	}
	
	idx->mdd_orig = n_strdup(idx2->pdg->mdd);
    
    
	return 1;
}

inline static char *eatws(char *str) 
{
    while (isspace(*str))
        str++;
    return str;
}


inline static char *next_tokn(char **str, char delim, int *toklen) 
{
    char *p, *token;

    
    if ((p = strchr(*str, delim)) == NULL) 
        token = NULL;
    else {
        *p = '\0';
        
        if (toklen)
            *toklen = p - *str;
        p++;
        while(isspace(*p))
            p++;
        token = *str;
        *str = p;
    }
    
    return token;
}

void pdir_init(struct pdir *idx) 
{
	idx->vf = NULL;
    idx->pdg = NULL;
    idx->idxpath[0] = '\0';
	idx->mdd_orig = NULL;
}

static
int pdir_open(struct pdir *idx, const char *path, int vfmode,
              const char *pdir_name)
{
	pdir_init(idx);
    if ((idx->pdg = pdir_digest_new(path, vfmode, pdir_v016compat, pdir_name))) {
        idx->vf = vfile_open_ul(path, VFT_TRURLIO, vfmode, pdir_name);
        if (idx->vf) 
            snprintf(idx->idxpath, sizeof(idx->idxpath), "%s", path);
        else {
            pdir_digest_free(idx->pdg);
            idx->pdg = NULL;
        }
    }

    return idx->pdg != NULL;
}

static
void pdir_close(struct pdir *idx) 
{
    if (idx->vf)
        vfile_close(idx->vf);

    if (idx->pdg)
        pdir_digest_free(idx->pdg);

	if (idx->mdd_orig) {
		free(idx->mdd_orig);
		idx->mdd_orig = NULL;
	}
	
    idx->vf = NULL;
    idx->pdg = NULL;
    idx->idxpath[0] = '\0';
}

void pdir_destroy(struct pdir *idx)
{
	pdir_close(idx);
}

static
int update_whole_idx(const char *path, const char *pdir_name) 
{
    struct vfile *vf;
    int rc, try = 2;
    unsigned vf_flags = VFM_RO | VFM_NORM | VFM_NOEMPTY;


    while (try > 0) {
        struct pdir idx;
        if (!pdir_open(&idx, path, vf_flags, pdir_name))
            return 0;
        try--;

        vf = idx.vf;

        rc = pdir_digest_verify(idx.pdg, vf);
        if (rc) {
            if (vf->vf_flags & VF_FETCHED)
                pdir_creat_md5(vfile_localpath(vf));
            try = 0;
            
        } else if (!vfile_is_remote(vf)) {
            try = 0;
            logn(LOGERR, _("broken index; try remade it"));
            
        } else if (try) {
            logn(LOGWARN,
                 _("assuming index is not fully downloaded, retrying..."));
            
        } else {
            /* vf_flags |= VFM_NORMCACHE; TO INVASIGATE */
            try = 0;
        }
        
        pdir_close(&idx);
    }

    return rc;
}

static int do_update_a(const struct source *src, const char *idxpath)
{
    unsigned int   vf_mode = VFM_RO | VFM_CACHE;
    struct pdir    idx;
    int            rc = 0;

    if (!pdir_open(&idx, idxpath, vf_mode, src->name))
        return update_whole_idx(idxpath, src->name);

    if (idx.vf->vf_flags & VF_FETCHED) {
        rc = pdir_digest_verify(idx.pdg, idx.vf);
        pdir_close(&idx);
        return rc;
    }
    
    switch (is_uptodate(idx.idxpath, idx.pdg, NULL, src->name)) {
        case 1: {
            rc = pdir_digest_verify(idx.pdg, idx.vf);
            pdir_close(&idx);
            if (rc)
                break;          /* else download whole index  */
                
        } /* no break */
            
        case -1:
        case 0:
            rc = update_whole_idx(idxpath, src->name);
            break;
                
        default:
            n_assert(0);
    }

    pdir_close(&idx);
    return rc;
}

static char *eat_zlib_ext(char *path) 
{
    char *p;
    
    if ((p = strrchr(n_basenam(path), '.')) != NULL) 
        if (strcmp(p, ".gz") == 0)
            *p = '\0';

    return path;
}

static
int do_update(struct pkgdir *pkgdir, enum pkgdir_uprc *uprc) 
{
    char            idxpath[PATH_MAX], tmpath[PATH_MAX], path[PATH_MAX];
    char            *dn, *bn;
    struct vfile    *vf;
    char            line[1024];
    int             nread, nerr = 0, rc;
    const char      *errmsg_broken_difftoc = _("%s: broken patch list");
    char            current_mdd[PDIR_DIGEST_SIZE + 1];
    struct pdir_digest  pdg_current;
    int             first_patch_found, npatches;
    struct pdir     *idx;

    idx = pkgdir->mod_data;

    if (idx->vf->vf_flags & VF_FETCHED)
        return 1;
    
    n_assert(pdir_v016compat == 0);
    switch (is_uptodate(pkgdir->idxpath, idx->pdg, &pdg_current, pkgdir->name)) {
        case 1:
            *uprc = PKGDIR_UPRC_UPTODATE;
            rc = 1;
            if ((pkgdir->flags & PKGDIR_VERIFIED) == 0)
                rc = pdir_digest_verify(idx->pdg, idx->vf);
            return rc;
            break;
            
        case -1:
            *uprc = PKGDIR_UPRC_ERR_UNKNOWN;
            return 0;
            
        case 0:
            break;

        default:
            n_assert(0);
    }
    
    *uprc = PKGDIR_UPRC_ERR_UNKNOWN;
    /* open diff toc */
    snprintf(idxpath, sizeof(idxpath), "%s", pkgdir->idxpath);
    eat_zlib_ext(idxpath);
    snprintf(tmpath, sizeof(tmpath), "%s", idxpath);
    n_basedirnam(tmpath, &dn, &bn);
    snprintf(path, sizeof(path), "%s/%s/%s%s", dn,
             pdir_packages_incdir, bn, pdir_difftoc_suffix);
    
    if ((vf = vfile_open_ul(path, VFT_TRURLIO, VFM_RO, pkgdir->name)) == NULL)
        return 0;

    
    n_assert(strlen(idx->pdg->mdd) == PDIR_DIGEST_SIZE);
    memcpy(current_mdd, idx->pdg->mdd, PDIR_DIGEST_SIZE + 1);

    npatches = 0;
    first_patch_found = 0;

    while ((nread = n_stream_gets(vf->vf_tnstream, line, sizeof(line))) > 0) {
        struct pkgdir *diff;
        char *p, *mdd;
        time_t ts;
		
        p = line;
        while (*p && isspace(*p))
            p++;
        
        if (*p == '#')
            continue;

        if ((p = strchr(p, ' ')) == NULL) {
            logn(LOGERR, errmsg_broken_difftoc, path);
            nerr++;
            break;
        }
        
        while (*p && isspace(*p))
            *p++ = '\0';
        
        if (sscanf(p, "%lu", &ts) != 1) { /* read ts */
            logn(LOGERR, errmsg_broken_difftoc, path);
            nerr++;
            break;
        }

        if (ts <= pkgdir->ts)
            continue;
        
        if ((p = strchr(p, ' ')) == NULL) {
            logn(LOGERR, errmsg_broken_difftoc, path);
            nerr++;
            break;
        }
        
        while (*p && isspace(*p))
            *p++ = '\0';
        
        mdd = p;                 /* read orig mdd */
        if ((p = strchr(p, ' ')) == NULL) {
            logn(LOGERR, errmsg_broken_difftoc, path);
            nerr++;
            break;
        }
        *p = '\0';
        
        if (p - mdd != PDIR_DIGEST_SIZE) {
            logn(LOGERR, errmsg_broken_difftoc, path);
            nerr++;
            break;
        }
        
        if (!first_patch_found) {
            if (memcmp(mdd, current_mdd, PDIR_DIGEST_SIZE) == 0)
                first_patch_found = 1;
            else {
                if (poldek_VERBOSE > 2) {
                    logn(LOGERR, "ts = %ld, %ld", pkgdir->ts, ts);
                    logn(LOGERR, "md dir  %s", idx->pdg->mdd);
                    logn(LOGERR, "md last %s", mdd);
                    logn(LOGERR, "md curr %s", current_mdd);
                }
                logn(LOGERR, _("%s: no patches available"),
                     pkgdir_pr_idxpath(pkgdir));
                nerr++;
                break;
            }
        }
        
        snprintf(path, sizeof(path), "%s/%s/%s", dn, pdir_packages_incdir, line);
        
        if ((diff = pkgdir_open(path, NULL, pkgdir->type, pkgdir->name)) == NULL) {
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
        
        npatches++;
    }
    
    vfile_close(vf);
    
    if (npatches == 0) {        /* outdated and no patches */
        *uprc = PKGDIR_UPRC_ERR_DESYNCHRONIZED;
        nerr++;
    }

    if (nerr == 0)
        if (pkgdir__uniq(pkgdir) > 0) { /* duplicates? -> error */
            *uprc = PKGDIR_UPRC_ERR_UNKNOWN;
            nerr++;
        }
    
    if (nerr == 0) {
        *uprc = PKGDIR_UPRC_UPDATED;
        idx->mdd_orig = n_strdup(pdg_current.mdd); /* for verification during write */

        snprintf(path, sizeof(path), "%s/%s", dn, pdir_packages_incdir);
        if (vf_localdirpath(tmpath, sizeof(tmpath), path) < (int)sizeof(tmpath)) {
            int v = poldek_set_verbose(-1);
            pkgdir__rmf(tmpath, NULL);
            poldek_set_verbose(v);
        }
        
        msg(1, "_\n");
    } 
    
    return nerr == 0;
}


static int valid_version(const char *ver, const char *path) 
{
    int major, minor;
    
    if (sscanf(ver, "%u.%u", &major, &minor) != 2) {
        logn(LOGERR, _("%s: invalid version string %s"), path, ver);
        return 0;
    }
    
    if (major != FILEFMT_MAJOR) 
        logn(LOGERR, _("%s: unsupported version %s (%d.x is required)"),
            path, ver, FILEFMT_MAJOR);
    
    else if (minor > FILEFMT_MINOR) 
        logn(LOGERR, _("%s: unsupported version %s (upgrade the poldek)"),
            path, ver);

    if (major == FILEFMT_MAJOR && minor <= FILEFMT_MINOR) {
        return (major * 10) + minor;
    }
    return 0;
}

static char *is_tag(char *s, const char *tag)
{
    int len;
    char *p = NULL;
    
    len = strlen(tag);
    
    if (strncmp(s, tag, len) == 0)
        p = s + len;
    
    return p;    
}

static int pdir_open_verify(struct pdir *idx, const char *path, int vfmode,
                            const char *pkgdir_name)
{

    struct vfile *vf;
    int nerr, idxok = 0;
    const char *local_idxpath;
    
    if (!pdir_open(idx, path, vfmode, pkgdir_name))
        return 0;

    
    vf = idx->vf;
    nerr = 0;
    idxok = 0;

    local_idxpath = vfile_localpath(vf);

    if ((vf->vf_flags & VF_FETCHED) == 0)
        if (pdir_verify_md5(idx->idxpath, local_idxpath))
            idxok = 1;
    
    if (!idxok)
        if (pdir_digest_verify(idx->pdg, vf)) {
            pdir_creat_md5(local_idxpath);
            idxok = 1;
        }

    if (!idxok && (vf->vf_flags & VF_FRMCACHE)) { /* not fully downloaded? */
        n_assert(vfmode & VFM_CACHE);
        vfmode &= ~VFM_CACHE;
        vfmode |= VFM_NODEL;
        pdir_close(idx);
        return pdir_open_verify(idx, path, vfmode, pkgdir_name);
    }

    return idxok;
}


static
int do_open(struct pkgdir *pkgdir, unsigned flags)
{
    struct vfile         *vf;
    char                 linebuf[1024 * 256];
    int                  nline;
    int                  nerr = 0, nread;
    struct pkgroup_idx   *pkgroups = NULL;
    time_t               ts = 0, ts_orig = 0;
    tn_array             *depdirs = NULL;
    tn_array             *removed_pkgs = NULL;
    const char           *errmsg_brokenidx = _("%s: broken index (empty %s tag)");
    struct pdir          idx;
    unsigned             vfmode = VFM_RO | VFM_CACHE | VFM_NOEMPTY;
    unsigned             pkgdir_flags = 0;
    char                 *path = pkgdir->path;

    
    flags = flags;              /* unused */

    if (!pdir_open_verify(&idx, pkgdir->idxpath, vfmode, pkgdir->name))
        return 0;
    
    vf = idx.vf;
    nerr = 0;
    nline = 0;
    while ((nread = n_stream_gets(vf->vf_tnstream, linebuf,
                                  sizeof(linebuf))) > 0) {
        char *p, *linep;

        linep = linebuf;
        nline++;
        if (nline == 1) {
            char *p;
            int lnerr = 0;
                
            if (*linep != '#')
                lnerr++;
            
            else if ((p = strstr(linep, pdir_poldeksindex)) == NULL) 
                lnerr++;
                    
            else {
                p += strlen(pdir_poldeksindex);
                p = eatws(p);
                
                if (*p != 'v') {
                    lnerr++;
                } else {
                    char *q;
                    int version;

                    p++;
                    if ((q = strchr(p, '\n')))
                        *q = '\0';
                    
                    if (!(version = valid_version(p, path))) {
                        nerr++;
                        goto l_end;
                    }
                    pkgdir->_idx_version = version;
                }
            }

            if (lnerr) {
                logn(LOGERR, _("%s: not a poldek index file"), path);
                nerr++;
                goto l_end;
            }
            continue;
        }
        
        if (*linep != '#' && *linep != '%')
            break;
        
        if (*linep == '%') {
            while (nread && linep[nread - 1] == '\n')
                linep[--nread] = '\0';
            linep++;

            if (strncmp(linep, pdir_tag_pkgroups, strlen(pdir_tag_pkgroups)) == 0) {
                dbgf_("LOAD %s\n", pkgdir->idxpath);
                pkgroups = pkgroup_idx_restore_st(vf->vf_tnstream, 0);

            } else if ((p = is_tag(linep, pdir_tag_removed))) {
                if (*p == '\0') {
                    logn(LOGERR, errmsg_brokenidx, path, pdir_tag_removed);
                    nerr++;
                    goto l_end;
                }
                removed_pkgs = parse_removed(p);
                
            } else if ((p = is_tag(linep, pdir_tag_depdirs))) {
                char *dir;
                n_assert(depdirs == NULL);

                depdirs = n_array_new(16, free, (tn_fn_cmp)strcmp);
                p = eatws(p);
                
                while ((dir = next_tokn(&p, ':', NULL)) != NULL) {
                    DBGF("depdir %s\n", dir);
                    n_array_push(depdirs, n_strdup(dir));
                }
                
                n_array_push(depdirs, n_strdup(p));
                
                if (n_array_size(depdirs)) 
                    n_array_sort(depdirs);
                
            } else if ((p = is_tag(linep, pdir_tag_ts))) {
                if (sscanf(p, "%lu", &ts) != 1) {
                    logn(LOGERR, errmsg_brokenidx, path, pdir_tag_ts);
                    nerr++;
                    goto l_end;
                }

            } else if (((p = is_tag(linep, pdir_tag_ts_orig)))) {
                if (sscanf(p, "%lu", &ts_orig) != 1) {
                    logn(LOGERR, errmsg_brokenidx, path, pdir_tag_ts_orig);
                    nerr++;
                    goto l_end;
                }
                
            } else if (is_tag(linep, pdir_tag_endhdr)) 
                break;              /* finish at %ENDH */
        }
    }
    
#if 0
    if (depdirs == NULL && ts_orig == 0) {
        logn(LOGERR, _("%s: missing '%s' tag"),
            vf->vf_tmpath ? vf->vf_tmpath : path, depdirs_tag);
        nerr++;
        goto l_end;
    }
#endif    

    pkgdir->mod_data = n_malloc(sizeof(idx));
    memcpy(pkgdir->mod_data, &idx, sizeof(idx));
    pkgdir->depdirs = depdirs;
    pkgdir->flags |= pkgdir_flags;
    pkgdir->pkgroups = pkgroups;
    pkgdir->ts = ts;
    pkgdir->orig_ts = ts_orig;
    pkgdir->removed_pkgs = removed_pkgs;
    if (ts_orig)
        pkgdir->flags |= PKGDIR_DIFF;

 l_end:

    if (nerr) {
        pdir_close(&idx);
        if (depdirs)
            n_array_free(depdirs);

        if (removed_pkgs)
            n_array_free(removed_pkgs);
    }
    
    return nerr == 0;
}

static
void do_free(struct pkgdir *pkgdir) 
{
    if (pkgdir->mod_data) {
        struct pdir *idx = pkgdir->mod_data;
        pdir_close(idx);
        if (idx->mdd_orig)
            free(idx->mdd_orig);
        free(idx);
        pkgdir->mod_data = NULL;
    }
}

static
void pkg_data_free(tn_alloc *na, void *ptr) 
{
    struct pkg_data *pd = ptr;

    if (pd->vf) {
        vfile_close(pd->vf);
        pd->vf = NULL;
    }
    na->na_free(na, pd);
}

static 
struct pkguinf *pdir_load_pkguinf(tn_alloc *na, const struct pkg *pkg,
                                  void *ptr, tn_array *langs)
{
    struct pkg_data *pd = ptr;
    struct pkguinf *pkgu = NULL;

    langs = langs;              /* ignored, no support */
    pkg = pkg;                  /* unused */
    if (pd->vf && pd->pkguinf_offs > 0)
        pkgu = pkguinf_restore_rpmhdr_st(na,
                                         pd->vf->vf_tnstream,
                                         pd->pkguinf_offs);

    return pkgu;
}

static 
tn_tuple *pdir_load_nodep_fl(tn_alloc *na, const struct pkg *pkg, void *ptr,
                             tn_array *foreign_depdirs)
{
    struct pkg_data *pd = ptr;
    tn_tuple *fl = NULL;

    pkg = pkg;
    if (pd->vf && pd->nodep_files_offs > 0) {
        n_stream_seek(pd->vf->vf_tnstream, pd->nodep_files_offs, SEEK_SET);
        pkgfl_restore_st(na, &fl, pd->vf->vf_tnstream, foreign_depdirs, 0);
    }
    
    return fl;
}


static
int do_load(struct pkgdir *pkgdir, unsigned ldflags)
{
    struct pdir        *idx;
    struct pkg         *pkg;
    struct pkg_offs    pkgo;
    struct pkg_data    *pkgd;
    
#if 0
    if (depdirs) {
        int i;
        for (i=0; i<n_array_size(depdirs); i++) {
            printf("DEP %s\n", n_array_nth(depdirs, i));
        }
#endif    

    idx = pkgdir->mod_data;
    
    while ((pkg = pdir_pkg_restore(pkgdir->na, idx->vf->vf_tnstream, NULL,
                                   pkgdir->foreign_depdirs,
                                   ldflags, &pkgo, pkgdir->path))) {
        pkg->pkgdir = pkgdir;

        pkgd = pkgdir->na->na_malloc(pkgdir->na, sizeof(*pkgd));
        pkgd->nodep_files_offs = pkgo.nodep_files_offs;
        pkgd->pkguinf_offs = pkgo.pkguinf_offs;
        pkgd->vf = vfile_incref(idx->vf);
        
        pkg->pkgdir_data = pkgd;
        pkg->pkgdir_data_free = pkg_data_free;
        pkg->load_pkguinf = pdir_load_pkguinf;
        pkg->load_nodep_fl = pdir_load_nodep_fl;
        n_array_push(pkgdir->pkgs, pkg);
    }

    return n_array_size(pkgdir->pkgs);
}


static tn_array *parse_removed(char *str) 
{
    char *p, *q;
    tn_array *pkgs;

    pkgs = pkgs_array_new(64);
    p = q = eatws(str);
    while ((p = next_tokn(&q, ' ', NULL)) != NULL) {
        const char   *name = NULL, *ver = NULL, *rel = NULL;
        int32_t      epoch = 0;

        if (*p && poldek_util_parse_nevr(p, &name, &epoch, &ver, &rel)) {
            struct pkg *pkg = pkg_new(name, epoch, ver, rel, NULL, NULL);
            n_array_push(pkgs, pkg);
        }
    }
    
    if (n_array_size(pkgs) == 0) {
        n_array_free(pkgs);
        pkgs = NULL;
    }
    
    return pkgs;
}


static int is_uptodate(const char *path, const struct pdir_digest *pdg_local,
                       struct pdir_digest *pdg_remote, const char *pdir_name)
{
    char                   mdpath[PATH_MAX], mdtmpath[PATH_MAX];
    struct pdir_digest         remote_pdg;
    int                    fd, n, rc = 0;
    const char             *ext = pdir_digest_ext;

    
    if (pdg_remote)
        pdir_digest_init(pdg_remote);
    
    pdir_digest_init(&remote_pdg);
    if (pdir_v016compat)
        remote_pdg.mode = PDIR_DIGEST_MODE_v016;
    
    if (vf_url_type(path) & VFURL_LOCAL)
        return 1;
    
    if (!(n = vf_mksubdir(mdtmpath, sizeof(mdtmpath), "tmpmd"))) {
        rc = -1;
        goto l_end;
    }

    if (pdir_v016compat)
        ext = pdir_digest_ext_v016;
    
    pdir_mkdigest_path(mdpath, sizeof(mdpath), path, ext);
    
    snprintf(&mdtmpath[n], sizeof(mdtmpath) - n, "/%s", n_basenam(mdpath));
    unlink(mdtmpath);
    mdtmpath[n] = '\0';

    if (!vf_fetch(mdpath, mdtmpath, 0, pdir_name)) {
        rc = -1;
        goto l_end;
    }
    
    mdtmpath[n] = '/';
    
    if ((fd = open(mdtmpath, O_RDONLY)) < 0 ||
        !pdir_digest_readfd(&remote_pdg, fd, mdtmpath)) {
        
        close(fd);
        rc = -1;
        goto l_end;
    }
    close(fd);

    if (pdir_v016compat == 0) {
        rc = (memcmp(pdg_local->mdd, &remote_pdg.mdd, sizeof(remote_pdg.mdd)) == 0);
        
    } else {
        n_assert(pdg_local->md);
        n_assert(remote_pdg.md);
        rc = (strcmp(pdg_local->md, remote_pdg.md) == 0);
    }
    
    if (!rc && pdg_remote)
        memcpy(pdg_remote, &remote_pdg, sizeof(remote_pdg));
    
 l_end:
    pdir_digest_destroy(&remote_pdg);
    return rc;
}
 
#if 0
DUPA
static
int do_unlink(const char *path, unsigned flags)
{
    int size;
    char tmpath[PATH_MAX], incpath[PATH_MAX], *p;

    flags = flags;
    size = sizeof(tmpath);
    if (pdir_mkdigest_path(tmpath, size, path, pdir_digest_ext) < size) {
        char tmpmd[PATH_MAX];
        
        if (vf_localpath(tmpmd, sizeof(tmpmd), tmpath) < (int)sizeof(tmpmd))
            pkgdir_rmf(tmpmd, NULL);
    }
    
    if (vf_localpath(tmpath, size, path) < size)
        pkgdir_rmf(tmpath, NULL);
        
    if ((p = strrchr(tmpath, '.')))
        if (strcmp(p, ".gz") == 0 || strcmp(p, ".bz2") == 0) {
            *p = '\0';
            pkgdir_rmf(tmpath, NULL);
        }

    
    if ((p = strrchr(tmpath, '/'))) {
        *p = '\0';
        
        snprintf(p, sizeof(tmpath) - (p - tmpath), "/%s", pdir_packages_incdir);
        if (vf_localdirpath(incpath, sizeof(incpath), path) < size)
            pkgdir_rmf(incpath, NULL);
    }
    
    return 1;
}
#endif

