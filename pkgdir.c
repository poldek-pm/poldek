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

#ifdef HAVE_GETLINE
# define _GNU_SOURCE 1
#else
# error "getline() is needed, sorry"
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

int pkgdir_v016compat = 0;      /* public */

#define PKGT_HAS_NAME     (1 << 0)
#define PKGT_HAS_EVR      (1 << 1)
#define PKGT_HAS_CAP      (1 << 3)
#define PKGT_HAS_REQ      (1 << 4)
#define PKGT_HAS_CNFL     (1 << 5)
#define PKGT_HAS_FILES    (1 << 6)
#define PKGT_HAS_ARCH     (1 << 7)
#define PKGT_HAS_OS       (1 << 8)
#define PKGT_HAS_SIZE     (1 << 9)
#define PKGT_HAS_FSIZE    (1 << 10)
#define PKGT_HAS_BTIME    (1 << 11)
#define PKGT_HAS_GROUPID  (1 << 12)

struct pkgtags_s {
    unsigned   flags;
    char       name[64];
    char       evr[64];
    char       arch[64];
    char       os[64];
    uint32_t   size;
    uint32_t   fsize;
    uint32_t   btime;
    uint32_t   groupid;
    tn_array   *caps;
    tn_array   *reqs;
    tn_array   *cnfls;
    tn_array   *pkgfl;
    off_t      other_files_offs; /* non dep files tag off_t */
    
    struct pkguinf *pkguinf;
    off_t      pkguinf_offs;
};

static
int add2pkgtags(struct pkgtags_s *pkgt, char tag, char *value,
                const char *pathname, off_t offs);
static
void pkgtags_clean(struct pkgtags_s *pkgt);

static
struct pkg *pkg_new_from_tags(struct pkgtags_s *pkgt);

static 
int restore_pkg_fields(FILE *stream, uint32_t *size, uint32_t *fsize,
                       uint32_t *btime, uint32_t *groupid);

static tn_array *parse_removed(char *str);

static int is_uptodate(const char *path, const struct pdigest *pdg_local,
                       struct pdigest *pdg_remote);


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

char *pkgdir_setup_pkgprefix(const char *path) 
{
    char *dn = NULL, *bn, *buf, *rpath = NULL;
    int len;

    len = strlen(path);
    buf = alloca(len + 1);
    memcpy(buf, path, len);
    buf[len] = '\0';
    
    n_basedirnam(buf, &dn, &bn);
    if (dn)
        rpath = strdup(dn);
    else
        rpath = strdup(".");

    return rpath;
}


static char *eat_zlib_ext(char *path) 
{
    char *p;
    
    if ((p = strrchr(n_basenam(path), '.')) != NULL) 
        if (strcmp(p, ".gz") == 0)
            *p = '\0';

    return path;
}


struct idx_s {
    struct vfile    *vf;
    char            idxpath[PATH_MAX];
    struct pdigest  *pdg;
};


static
int do_open_idx(struct idx_s *idx, char *path, int path_len, int vfmode)
{
    if ((idx->pdg = pdigest_new(path, vfmode, pkgdir_v016compat))) {
        if ((idx->vf = vfile_open(path, VFT_STDIO, vfmode)) == NULL) {
            if (path_len && strcmp(&path[path_len - 3], ".gz") == 0) {
                path[path_len - 3] = '\0'; /* trim *.gz */
                idx->vf = vfile_open(path, VFT_STDIO, vfmode);
            }
        }
        
        if (idx->vf) 
            snprintf(idx->idxpath, sizeof(idx->idxpath), "%s", path);
        else {
            pdigest_free(idx->pdg);
            idx->pdg = NULL;
        }
    }
    
    return idx->pdg != NULL;
}


static
int mk_idx_url(char *durl, int size, const char *url, int *added_bn)
{
    int n;
    
    *added_bn = 0;
    if (url[strlen(url) - 1] != '/')
        n = n_snprintf(durl, size, "%s", url);
        
    else {
        n = n_snprintf(durl, size, "%s%s.gz", url,
                       default_pkgidx_name);
        *added_bn = 1;
    }
    

    return n;
}
	

static
int open_idx(struct idx_s *idx, const char *path, int vfmode)
{
    char tmpath[PATH_MAX];
    int added_bn = 0, n;
    
    idx->vf = NULL;
    idx->pdg = NULL;
    idx->idxpath[0] = '\0';

    n = mk_idx_url(tmpath, sizeof(tmpath), path, &added_bn);
    if (added_bn)
        n = 0;
    
    return do_open_idx(idx, tmpath, n, vfmode);
}


static
void close_idx(struct idx_s *idx) 
{
    if (idx->vf)
        vfile_close(idx->vf);

    if (idx->pdg)
        pdigest_free(idx->pdg);
        

    idx->vf = NULL;
    idx->pdg = NULL;
    idx->idxpath[0] = '\0';
}


static int do_unlink(const char *path, int msg_fullpath) 
{
    struct stat st;
    
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
        msgn(1, _("Removing %s"), msg_fullpath ? path : n_basenam(path));
        return vf_localunlink(path);
    }
    
    return 0;
}

static
int rm_dir_files(const char *dirpath) 
{
    struct dirent  *ent;
    DIR            *dir;
    char           *sepchr = "/";
    int            msg_displayed = 0;
    
    if ((dir = opendir(dirpath)) == NULL) {
        if (verbose > 2)
            logn(LOGWARN, "opendir %s: %m", dirpath);
	return 1;
    }
    
    if (dirpath[strlen(dirpath) - 1] == '/')
        sepchr = "";
    
    while( (ent = readdir(dir)) ) {
        char path[PATH_MAX];
        
        if (*ent->d_name == '.') {
            if (ent->d_name[1] == '\0')
                continue;
            
            if (ent->d_name[1] == '.' && ent->d_name[2] == '\0')
                continue;

        }

        if (strncmp(ent->d_name, "ftp_", 4) == 0)
            continue;

        if (strncmp(ent->d_name, "http_", 5) == 0)
            continue;

        snprintf(path, sizeof(path), "%s%s%s", dirpath, sepchr, ent->d_name);

        if (msg_displayed == 0) {
            msgn(1, _("Cleaning up %s..."), dirpath);
            msg_displayed = 1;
        }
        
        do_unlink(path, 0);
    }
    
    closedir(dir);
    return 1;
}

	

int unlink_pkgdir_files(const char *path, int allfiles)
{
    int type, size, n;
    char tmpath[PATH_MAX], url[PATH_MAX], *p;

    if ((type = vf_url_type(path)) == VFURL_UNKNOWN)
        return 0;
    
    if (type & VFURL_LOCAL)
        return 1;

    mk_idx_url(url, sizeof(url), path, &n);
    size = sizeof(tmpath);
    
    if (allfiles) {
        if ((p = strrchr(url, '/'))) {
            *p = '\0';

            if (vf_localdirpath(tmpath, size, url) < size)
                rm_dir_files(tmpath);
            
            snprintf(p, sizeof(url) - (p - url), "/%s", pdir_packages_incdir);
            if (vf_localdirpath(tmpath, size, url) < size)
                rm_dir_files(tmpath);
        }

        
    } else {
        if (mkdigest_path(tmpath, size, url, pdigest_ext) < size) {
            char tmpmd[PATH_MAX];
            
            if (vf_localpath(tmpmd, sizeof(tmpmd), tmpath) < (int)sizeof(tmpmd))
                do_unlink(tmpmd, 1);
        }
        
        if (vf_localpath(tmpath, size, url) < size)
            do_unlink(tmpath, 1);
        
        if ((p = strrchr(tmpath, '.')))
            if (strcmp(p, ".gz") == 0 || strcmp(p, ".bz2") == 0) {
                *p = '\0';
                do_unlink(tmpath, 1);
            }
    
        if ((p = strrchr(url, '/'))) {
            *p = '\0';
            
            snprintf(p, sizeof(url) - (p - url), "/%s", pdir_packages_incdir);
            if (vf_localdirpath(tmpath, size, url) < size)
                rm_dir_files(tmpath);
        }
    }
    
    return 1;
}
    



#if 0
void pkgs_dump(tn_array *pkgs, const char *hdr) 
{
    int i;

    fprintf(stderr, "\nDUMP %d %s\n", n_array_size(pkgs), hdr);
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        fprintf(stderr, "P %s\n", pkg_snprintf_s(pkg));
        n_assert((int)pkg->reqs != 2);
    }
}
#endif

struct pkgdir *pkgdir_malloc(void)
{
    struct pkgdir *pkgdir;

    
    pkgdir = malloc(sizeof(*pkgdir));
    memset(pkgdir, 0, sizeof(*pkgdir));

    pkgdir->name = NULL;
    pkgdir->path = NULL;
    pkgdir->idxpath = NULL;
    pkgdir->pkgs = NULL;
    
    pkgdir->depdirs = NULL;
    pkgdir->foreign_depdirs = NULL;
    
    pkgdir->pkgroups = NULL;
    pkgdir->vf = NULL;
    pkgdir->flags = 0;
    pkgdir->ts = 0;

    pkgdir->removed_pkgs = NULL;
    pkgdir->ts_orig = 0;
    pkgdir->mdd_orig = NULL;
    
    pkgdir->pdg = NULL;
    return pkgdir;
}


static
int update_whole_idx(const char *path) 
{
    struct vfile *vf;
    int rc, try = 2;
    unsigned vf_flags = VFM_RO | VFM_NORM;

    while (try > 0) {
        struct idx_s idx;
        
        if (!open_idx(&idx, path, vf_flags))
            return 0;
        try--;

        vf = idx.vf;

        rc = pdigest_verify(idx.pdg, vf);
        if (rc) {
            if (vf->vf_flags & VF_FETCHED)
                i_pkgdir_creat_md5(vfile_localpath(vf));
            try = 0;
            
        } else if (!vfile_is_remote(vf)) {
            try = 0;
            logn(LOGERR, _("broken index; try remade it"));
            
        } else if (try) {
            logn(LOGWARN,
                 _("assuming index is not fully downloaded, retrying..."));
            
        } else {
            vf_flags |= VFM_NORMCACHE;
            try = 0;
        }
        
        close_idx(&idx);
    }

    return rc;
}


int update_whole_pkgdir(const char *path)
{
    unsigned int   vf_mode = VFM_RO | VFM_CACHE;
    struct idx_s   idx;
    int            rc = 0;
    
    if (!open_idx(&idx, path, vf_mode))
        return update_whole_idx(path);

    if (idx.vf->vf_flags & VF_FETCHED) {
        rc = pdigest_verify(idx.pdg, idx.vf);
        close_idx(&idx);
        return rc;
    }
    
    switch (is_uptodate(idx.idxpath, idx.pdg, NULL)) {
        case 1: {
            rc = pdigest_verify(idx.pdg, idx.vf);
            close_idx(&idx);
            if (rc)
                break;          /* else download whole index  */
                
        } /* no break */
            
        case -1:
        case 0:
            rc = update_whole_idx(path);
            break;
                
        default:
            n_assert(0);
    }

    close_idx(&idx);
    return rc;
}


int pkgdir_update(struct pkgdir *pkgdir, int *npatches) 
{
    char            idxpath[PATH_MAX], tmp[PATH_MAX], path[PATH_MAX], *dn, *bn;
    struct vfile    *vf;
    char            *linebuf = NULL;
    int             line_size = 0, nread, nerr = 0, rc;
    const char      *errmsg_broken_difftoc = _("%s: broken patch list");
    char            current_mdd[PDIGEST_SIZE + 1];
    struct pdigest  pdg_current;
    int             first_patch_found;


    n_assert(pkgdir_v016compat == 0);
    switch (is_uptodate(pkgdir->idxpath, pkgdir->pdg, &pdg_current)) {
        case 1:
            rc = 1;
            if ((pkgdir->flags & PKGDIR_VERIFIED) == 0)
                rc = pdigest_verify(pkgdir->pdg, pkgdir->vf);
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
    snprintf(tmp, sizeof(tmp), "%s", idxpath);
    n_basedirnam(tmp, &dn, &bn);
    snprintf(path, sizeof(path), "%s/%s/%s%s", dn,
             pdir_packages_incdir, bn, pdir_difftoc_suffix);
    
    if ((vf = vfile_open(path, VFT_STDIO, VFM_RO)) == NULL) 
        return update_whole_idx(pkgdir->idxpath);

    *npatches = 0;
    n_assert(strlen(pkgdir->pdg->mdd) == PDIGEST_SIZE);
    memcpy(current_mdd, pkgdir->pdg->mdd, PDIGEST_SIZE + 1);
    first_patch_found = 0;
    
    while ((nread = getline(&linebuf, &line_size, vf->vf_stream)) > 0) {
        struct pkgdir *diff;
        char *p, *mdd;
        time_t ts;

        p = linebuf;
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
        
        if (p - mdd != PDIGEST_SIZE) {
            logn(LOGERR, errmsg_broken_difftoc, path);
            nerr++;
            break;
        }
        
        if (!first_patch_found) {
            if (memcmp(mdd, current_mdd, PDIGEST_SIZE) == 0)
                first_patch_found = 1;
            else {
                if (verbose > 2) {
                    logn(LOGERR, "ts = %ld, %ld", pkgdir->ts, ts);
                    logn(LOGERR, "md dir  %s", pkgdir->pdg->mdd);
                    logn(LOGERR, "md last %s", mdd);
                    logn(LOGERR, "md curr %s", current_mdd);
                }
                logn(LOGERR, _("%s: no patches available"), pkgdir->idxpath);
                nerr++;
                break;
            }
        }
        
        snprintf(path, sizeof(path), "%s/%s/%s", dn, pdir_packages_incdir, linebuf);
        if ((diff = pkgdir_new("diff", path, NULL, 0)) == NULL) {
            nerr++;
            break;
        }
        
        if ((pkgdir->flags & PKGDIR_LOADED) == 0) {
            msgn(1, _("Loading %s..."), pkgdir->idxpath);
            if (!pkgdir_load(pkgdir, NULL, PKGDIR_LD_RAW)) {
                logn(LOGERR, _("%s: load failed"), pkgdir->idxpath);
                nerr++;
                break;
            }
        }
        msgn(1, _("Applying patch %s..."), n_basenam(diff->idxpath));
        pkgdir_load(diff, NULL, PKGDIR_LD_RAW);
        pkgdir_patch(pkgdir, diff);
        pkgdir_free(diff);
        (*npatches)++;
        
    }
    
    vfile_close(vf);
    if (linebuf)
        free(linebuf);

    if (*npatches == 0)         /* outdated and no patches */
        nerr++;

    if (nerr == 0)
        if (pkgdir_uniq(pkgdir) > 0) /* duplicates ? -> error */
            nerr++;
    
    if (nerr == 0) {
        pkgdir->mdd_orig = strdup(pdg_current.mdd); /* for verification during write */
        
    } else {
        logn(LOGWARN, _("%s: desynchronized index, try --update-whole"),
             pkgdir->idxpath);
        
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

    return major == FILEFMT_MAJOR && minor <= FILEFMT_MINOR;
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

struct pkgdir *pkgdir_new(const char *name, const char *path,
                          const char *pkg_prefix, unsigned flags)
{
    struct pkgdir        *pkgdir = NULL;
    struct vfile         *vf;
    char                 *linebuf;
    int                  line_size, nline;
    int                  nerr = 0, nread;
    struct pkgroup_idx   *pkgroups = NULL;
    time_t               ts = 0, ts_orig = 0;
    tn_array             *depdirs = NULL;
    tn_array             *removed_pkgs = NULL;
    const char           *errmsg_brokenidx = _("%s: broken index (empty %s tag)");
    struct idx_s         idx;
    unsigned             vfmode = VFM_RO | VFM_CACHE;
    unsigned             pkgdir_flags = PKGDIR_LDFROM_IDX;

    
    if (!open_idx(&idx, path, vfmode))
        return NULL;
    
    vf = idx.vf;
    
    nerr = 0;

    if (vf->vf_flags & VF_FRMCACHE)
        flags |= PKGDIR_NEW_VERIFY;
    
    if (vf->vf_flags & VF_FETCHED) {
        if (pdigest_verify(idx.pdg, vf)) {
            pkgdir_flags |= PKGDIR_VERIFIED;
            i_pkgdir_creat_md5(vfile_localpath(vf));
            
        } else
            nerr++;
        
        
    } else if (flags & PKGDIR_NEW_VERIFY) {
        const char *local_idxpath = vfile_localpath(vf);
        
        if (local_idxpath == NULL ||
            !i_pkgdir_verify_md5(idx.idxpath, local_idxpath)) {
            
            if (pdigest_verify(idx.pdg, vf)) {
                pkgdir_flags |= PKGDIR_VERIFIED;
                i_pkgdir_creat_md5(local_idxpath);
            }
            else 
                nerr++;
        }
    }

    if (nerr) {
        close_idx(&idx);
        return NULL;
    }
    
    
    line_size = 4096;
    linebuf = malloc(line_size);
    nline = 0;
    while ((nread = getline(&linebuf, &line_size, vf->vf_stream)) > 0) {
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

                    p++;
                    if ((q = strchr(p, '\n')))
                        *q = '\0';
                    if (!valid_version(p, path)) {
                        nerr++;
                        goto l_end;
                    }
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

            if (strncmp(linep, pkgroups_tag, strlen(pkgroups_tag)) == 0) {
                pkgroups = pkgroup_idx_restore(vf->vf_stream, 0);

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
                
                while ((dir = next_tokn(&p, ':', NULL)) != NULL) 
                    n_array_push(depdirs, strdup(dir));
                n_array_push(depdirs, strdup(p));
                
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
    
    free(linebuf);

#if 0
    if (depdirs == NULL && ts_orig == 0) {
        logn(LOGERR, _("%s: missing '%s' tag"),
            vf->vf_tmpath ? vf->vf_tmpath : path, depdirs_tag);
        nerr++;
        goto l_end;
    }
#endif    
    
    pkgdir = pkgdir_malloc();
    pkgdir->name = strdup(name);
    
    if (pkg_prefix) 
        pkgdir->path = strdup(pkg_prefix);
    else 
        pkgdir->path = pkgdir_setup_pkgprefix(idx.idxpath);
    
    pkgdir->idxpath = strdup(idx.idxpath);
    pkgdir->vf = idx.vf;
    pkgdir->pdg = idx.pdg;
    
    pkgdir->depdirs = depdirs;
    if (depdirs) {
        n_array_ctl(pkgdir->depdirs, TN_ARRAY_AUTOSORTED);
        n_array_sort(pkgdir->depdirs);
    }
    
    pkgdir->flags = pkgdir_flags;
    pkgdir->pkgs = pkgs_array_new(1024);
    pkgdir->pkgroups = pkgroups;
    pkgdir->ts = ts;
    pkgdir->ts_orig = ts_orig;
    pkgdir->removed_pkgs = removed_pkgs;
    if (ts_orig)
        pkgdir->flags |= PKGDIR_DIFF;

 l_end:

    if (nerr) {
        vfile_close(vf);
        if (depdirs)
            n_array_free(depdirs);
    }
    
    return pkgdir;
}


void pkgdir_free(struct pkgdir *pkgdir) 
{
    if (pkgdir->name) {
        free(pkgdir->name);
        pkgdir->name = NULL;
    }
    
    if (pkgdir->path) {
        free(pkgdir->path);
        pkgdir->path = NULL;
    }

    if (pkgdir->idxpath) {
        free(pkgdir->idxpath);
        pkgdir->idxpath = NULL;
    }

    if (pkgdir->depdirs) {
        n_array_free(pkgdir->depdirs);
        pkgdir->depdirs = NULL;
    }

    if (pkgdir->foreign_depdirs) {
        n_array_free(pkgdir->foreign_depdirs);
        pkgdir->foreign_depdirs = NULL;
    }


    if (pkgdir->pkgs) {
        n_array_free(pkgdir->pkgs);
        pkgdir->pkgs = NULL;
    }

    if (pkgdir->pkgroups) {
        pkgroup_idx_free(pkgdir->pkgroups);
        pkgdir->pkgroups = NULL;
    }

    if (pkgdir->vf) {
        vfile_close(pkgdir->vf);
        pkgdir->vf = NULL;
    }

    if (pkgdir->pdg) {
        pdigest_free(pkgdir->pdg);
        pkgdir->pdg = NULL;
    }

    if (pkgdir->mdd_orig) {
        free(pkgdir->mdd_orig);
        pkgdir->mdd_orig = NULL;
    }

    pkgdir->flags = 0;
    free(pkgdir);
}


int pkgdir_load(struct pkgdir *pkgdir, tn_array *depdirs, unsigned ldflags)
{
    struct pkgtags_s   pkgt;
    off_t              offs;
    char               *linebuf;
    int                line_size;
    int                nerr = 0, nread, i;
    struct vfile       *vf;
    int                flag_skip_bastards = 0, flag_fullflist = 0;
    int                flag_lddesc = 0;
    tn_array           *only_dirs;

    const  char        *errmg_double_tag = "%s:%ld: double '%c' tag";
    const  char        *errmg_ldtag = "%s:%ld: load '%c' tag error";
    
#if 0
    if (depdirs) 
        for (i=0; i<n_array_size(depdirs); i++) {
            printf("DEP %s\n", n_array_nth(depdirs, i));
        }
#endif    

    if (ldflags & PKGDIR_LD_SKIPBASTS) 
        flag_skip_bastards = 1;

    if (ldflags & PKGDIR_LD_FULLFLIST)
        flag_fullflist = 1;

    if (ldflags & PKGDIR_LD_DESC)
        flag_lddesc = 1;

    only_dirs = NULL;

    if (flag_fullflist == 0 && depdirs) {
        only_dirs = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
        for (i=0; i<n_array_size(depdirs); i++) {
            char *dn = n_array_nth(depdirs, i);
            if (n_array_bsearch(pkgdir->depdirs, dn) == NULL) {
                DBGMSG_F("ONLYDIR for %s: %s\n", pkgdir->path, dn);
                if (*dn == '/' && *(dn + 1) != '\0') 
                    dn++;
                n_array_push(only_dirs, dn);
            }
        }
        
        if (n_array_size(only_dirs) == 0) {
            n_array_free(only_dirs);
            only_dirs = NULL;
        }
    }
    
    pkgdir->foreign_depdirs = only_dirs;
    vf = pkgdir->vf;
    memset(&pkgt, 0, sizeof(pkgt));
    line_size = 4096;
    linebuf = malloc(line_size);

    while ((nread = getline(&linebuf, &line_size, vf->vf_stream)) > 0) {
        char *p, *val, *line;
        
        offs = ftell(vf->vf_stream);
        line = linebuf;
        
        if (*line == '\n') {        /* empty line -> end of record */
            struct pkg *pkg;
            DBGMSG("\n\nEOR\n");
            pkg = pkg_new_from_tags(&pkgt);
            if (pkg) {
                pkg->pkgdir = pkgdir;
                n_array_push(pkgdir->pkgs, pkg);
                pkg = NULL;
            }
            pkgtags_clean(&pkgt);
            continue;
        }

        if (*line == ' ') {      /* continuation */
            logn(LOGERR, _("%s:%ld: syntax error"), pkgdir->path, offs);
            nerr++;
            goto l_end;
        }

            
        while (nread && line[nread - 1] == '\n')
            line[--nread] = '\0';
        
        p = val = line + 1;
        if (*line == '\0' || *p != ':') {
            logn(LOGERR, _("%s:%ld:%s ':' expected"), pkgdir->path, offs, line);
            nerr++;
            goto l_end;
        }
            
        *val++ = '\0';
        val = eatws(val);
        n_assert(*line && *(line + 1) == '\0');

        switch (*line) {
            case 'N':
            case 'V':
            case 'A':
            case 'O':
                if (!add2pkgtags(&pkgt, *line, val, pkgdir->path, offs)) {
                    nerr++;
                    goto l_end;
                }
                break;

            case 'S':
            case 'T':
                if (!add2pkgtags(&pkgt, *line, val, pkgdir->path, offs)) {
                    nerr++;
                    goto l_end;
                }
                break;
                

            case 'F':
                if (pkgt.flags & PKGT_HAS_SIZE) {
                    logn(LOGERR, _("%s:%ld: syntax error"), pkgdir->path, offs);
                    nerr++;
                    goto l_end;
                }
                restore_pkg_fields(vf->vf_stream, &pkgt.size, &pkgt.fsize,
                                   &pkgt.btime, &pkgt.groupid);
                pkgt.flags |= PKGT_HAS_SIZE | PKGT_HAS_FSIZE | PKGT_HAS_BTIME |
                    PKGT_HAS_GROUPID;
                break;
                

            case 'P':
                if (pkgt.flags & PKGT_HAS_CAP) {
                    logn(LOGERR, errmg_double_tag, pkgdir->path, offs, *line);
                    nerr++;
                    goto l_end;
                }
                    
                pkgt.caps = capreq_arr_restore(vf->vf_stream,
                                               flag_skip_bastards);
                pkgt.flags |= PKGT_HAS_CAP;
                break;
                    
            case 'R':
                if (pkgt.flags & PKGT_HAS_REQ) {
                    logn(LOGERR, errmg_double_tag, pkgdir->path, offs, *line);
                    nerr++;
                    goto l_end;
                }
                    
                pkgt.reqs = capreq_arr_restore(vf->vf_stream,
                                               flag_skip_bastards);
                if (pkgt.reqs == NULL) {
                    logn(LOGERR, errmg_ldtag, pkgdir->path, offs, *line);
                    nerr++;
                    goto l_end;
                }
                pkgt.flags |= PKGT_HAS_REQ;
                break;
                    
            case 'C':
                if (pkgt.flags & PKGT_HAS_CNFL) {
                    logn(LOGERR, _(errmg_double_tag), pkgdir->path, offs, *line);
                    nerr++;
                    goto l_end;
                }
                    
                pkgt.cnfls = capreq_arr_restore(vf->vf_stream,
                                                flag_skip_bastards);
                
                if (pkgt.cnfls == NULL) {
                    logn(LOGERR, errmg_ldtag, pkgdir->path, offs, *line);
                    nerr++;
                    goto l_end;
                    
                } else if (n_array_size(pkgt.cnfls) == 0) {
                    n_array_free(pkgt.cnfls);
                    pkgt.cnfls = NULL;
                }
                
                if (pkgt.cnfls)
                    pkgt.flags |= PKGT_HAS_CNFL;
                break;

            case 'L':
                pkgt.pkgfl = pkgfl_restore_f(vf->vf_stream, NULL, 0);
                if (pkgt.pkgfl == NULL) {
                    logn(LOGERR, errmg_ldtag, pkgdir->path, offs, *line);
                    nerr++;
                    goto l_end;
                }
                n_assert(pkgt.pkgfl);
                //printf("DUMP %p %d\n", pkgt.pkgfl, n_array_size(pkgt.pkgfl));
                //pkgfl_dump(pkgt.pkgfl);
                pkgt.flags |= PKGT_HAS_FILES;
                break;
                    
            case 'l':
                pkgt.other_files_offs = ftell(vf->vf_stream);
                
                if (flag_fullflist == 0 && only_dirs == NULL) {
                    pkgfl_skip_f(vf->vf_stream);
                        
                } else {
                    tn_array *fl;
                        
                    fl = pkgfl_restore_f(vf->vf_stream, only_dirs, 1);
                    if (fl == NULL) {
                        logn(LOGERR, errmg_ldtag, pkgdir->path, offs, *line);
                        nerr++;
                        goto l_end;
                    }
                    
                    if (pkgt.pkgfl == NULL) {
                        pkgt.pkgfl = fl;
                        pkgt.flags |= PKGT_HAS_FILES;
                            
                    } else {
                        while (n_array_size(fl)) 
                            n_array_push(pkgt.pkgfl, n_array_shift(fl));
                        n_array_free(fl);
                    }
                }
                break;

            case 'U':
                if (flag_lddesc) {
                    pkgt.pkguinf = pkguinf_restore(vf->vf_stream, 0);
                    if (pkgt.pkguinf == NULL) {
                        logn(LOGERR, errmg_ldtag, pkgdir->path, offs, *line);
                        nerr++;
                        goto l_end;
                    }
                    pkgt.pkguinf_offs = 0;
                    
                } else {
                    pkgt.pkguinf_offs = ftell(vf->vf_stream);
                    pkguinf_skip(vf->vf_stream);
                }
                break;

            default:
                logn(LOGERR, "%s:%ld: unknown tag '%c'", pkgdir->path, offs, *line);
                nerr++;
                goto l_end;
        }
    }
    

 l_end:
    
    pkgtags_clean(&pkgt);
    free(linebuf);

    if (n_array_size(pkgdir->pkgs)) {
        n_array_sort(pkgdir->pkgs);
        pkgdir->flags |= PKGDIR_LOADED;
        if ((ldflags & PKGDIR_LD_NOUNIQ) == 0)
            pkgdir_uniq(pkgdir);
    }
    
    return nerr == 0;
}


#define sizeof_pkgt(memb) (sizeof((pkgt)->memb) - 1)
static
int add2pkgtags(struct pkgtags_s *pkgt, char tag, char *value,
                const char *pathname, off_t offs) 
{
    int err = 0;
    const char *errmg_double_tag = "%s:%d: double '%c' tag";
    
    switch (tag) {
        case 'N':
            if (pkgt->flags & PKGT_HAS_NAME) {
                logn(LOGERR, errmg_double_tag, pathname, offs, tag);
                err++;
            } else {
                memcpy(pkgt->name, value, sizeof(pkgt->name)-1);
                pkgt->flags |= PKGT_HAS_NAME;
            }
            break;
            
        case 'V':
            if (pkgt->flags & PKGT_HAS_EVR) {
                logn(LOGERR, errmg_double_tag, pathname, offs, tag);
                err++;
            } else {
                memcpy(pkgt->evr, value, sizeof(pkgt->evr)-1);
                pkgt->evr[sizeof(pkgt->evr)-1] = '\0';
                pkgt->flags |= PKGT_HAS_EVR;
            }
            break;
            
        case 'A':
            if (pkgt->flags & PKGT_HAS_ARCH) {
                logn(LOGERR, errmg_double_tag, pathname, offs, tag);
                err++;
            } else {
                memcpy(pkgt->arch, value, sizeof(pkgt->arch)-1);
                pkgt->arch[sizeof(pkgt->arch)-1] = '\0';
                pkgt->flags |= PKGT_HAS_ARCH;
            }
            break;

        case 'O':
            if (pkgt->flags & PKGT_HAS_OS) {
                logn(LOGERR, errmg_double_tag, pathname, offs, tag);
                err++;
            } else {
                memcpy(pkgt->os, value, sizeof(pkgt->os) - 1);
                pkgt->os[ sizeof(pkgt->os) - 1 ] = '\0';
                pkgt->flags |= PKGT_HAS_OS;
            }
            break;
            
        case 'S':
            if (pkgt->flags & PKGT_HAS_SIZE) {
                logn(LOGERR, errmg_double_tag, pathname, offs, tag);
                err++;
            } else {
                pkgt->size = atoi(value);
                pkgt->flags |= PKGT_HAS_SIZE;
            }
            break;

        case 's':
            if (pkgt->flags & PKGT_HAS_FSIZE) {
                logn(LOGERR, errmg_double_tag, pathname, offs, tag);
                err++;
            } else {
                pkgt->size = atoi(value);
                pkgt->flags |= PKGT_HAS_FSIZE;
            }
            break;
            
        case 'T':
            if (pkgt->flags & PKGT_HAS_BTIME) {
                logn(LOGERR, errmg_double_tag, pathname, offs, tag);
                err++;
            } else {
                pkgt->btime = atoi(value);
                pkgt->flags |= PKGT_HAS_BTIME;
            }
            break;
            
        default:
            logn(LOGERR, "%s:%ld: unknown tag '%c'", pathname, offs, tag);
            n_assert(0);
    }
    
    return err == 0;
}

        
static void pkgtags_clean(struct pkgtags_s *pkgt) 
{
    if (pkgt->flags & PKGT_HAS_REQ)
        if (pkgt->reqs)
            n_array_free(pkgt->reqs);

    if (pkgt->flags & PKGT_HAS_CAP)
        if (pkgt->caps)
            n_array_free(pkgt->caps);

    if (pkgt->flags & PKGT_HAS_CNFL)
        if (pkgt->cnfls)
            n_array_free(pkgt->cnfls);

    if (pkgt->flags & PKGT_HAS_FILES)
        if (pkgt->pkgfl) 
            n_array_free(pkgt->pkgfl);
    
    if (pkgt->pkguinf) 
        pkguinf_free(pkgt->pkguinf);
        pkgt->pkguinf = 0;
    

    memset(pkgt, 0, sizeof(*pkgt));
    pkgt->caps = pkgt->reqs = pkgt->cnfls = pkgt->pkgfl = NULL;
    pkgt->pkguinf = NULL;
}
    

static
struct pkg *pkg_new_from_tags(struct pkgtags_s *pkgt) 
{
    struct pkg *pkg;
    char *version, *release, *arch = NULL, *os = NULL;
    int32_t epoch;
    
    if (!(pkgt->flags & (PKGT_HAS_NAME | PKGT_HAS_EVR)))
        return NULL;
    
    if (pkgt->flags & PKGT_HAS_OS) 
        os = pkgt->os;
    
    if (pkgt->flags & PKGT_HAS_ARCH) 
        arch = pkgt->arch;
    
    if (*pkgt->name == '\0' || *pkgt->evr == '\0' || *pkgt->arch == '\0') 
        return NULL;
    
    if (!parse_evr(pkgt->evr, &epoch, &version, &release))
        return NULL;
    
    if (version == NULL || release == NULL) {
        logn(LOGERR, _("%s: failed to parse evr string"), pkgt->name);
        return NULL;
    }

    pkg = pkg_new(pkgt->name, epoch, version, release, arch, os, 
                  pkgt->size, pkgt->fsize, pkgt->btime);
    pkg->groupid = pkgt->groupid;
    
    if (pkg == NULL) {
        logn(LOGERR, _("error reading %s's data"), pkgt->name);
        return NULL;
    }

    msg(10, " load  %s\n", pkg_snprintf_s(pkg));

    if (pkgt->flags & PKGT_HAS_CAP) {
        n_assert(pkgt->caps && n_array_size(pkgt->caps));
        pkg->caps = pkgt->caps;
        pkgt->caps = NULL;
    }
    
    if (pkgt->flags & PKGT_HAS_REQ) {
        n_assert(pkgt->reqs && n_array_size(pkgt->reqs));
        pkg->reqs = pkgt->reqs;
        pkgt->reqs = NULL;
    }

    if (pkgt->flags & PKGT_HAS_CNFL) {
        n_assert(pkgt->cnfls && n_array_size(pkgt->cnfls));
        n_array_sort(pkgt->cnfls);
        pkg->cnfls = pkgt->cnfls;
        pkgt->cnfls = NULL;
    }

    if (pkgt->flags & PKGT_HAS_FILES) {
        if (n_array_size(pkgt->pkgfl) == 0) {
            n_array_free(pkgt->pkgfl);
            pkgt->pkgfl = NULL;
        } else {
            pkg->fl = pkgt->pkgfl;
            n_array_sort(pkg->fl);
            //pkgfl_dump(pkg->fl);
            pkgt->pkgfl = NULL;
        }
    }

    pkg->other_files_offs = pkgt->other_files_offs;
    if (pkgt->pkguinf_offs) {
        n_assert(pkg_has_ldpkguinf(pkg) == 0);
        pkg->pkg_pkguinf_offs = pkgt->pkguinf_offs;
        
    } else if (pkgt->pkguinf != NULL) {
        pkg->pkg_pkguinf = pkgt->pkguinf;
        pkg_set_ldpkguinf(pkg);
        pkgt->pkguinf = NULL;
    }
    	
    return pkg;
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

        if (*p && parse_nevr(p, &name, &epoch, &ver, &rel)) {
            struct pkg *pkg = pkg_new(name, epoch, ver, rel, NULL, NULL,
                                      0, 0, 0);
            n_array_push(pkgs, pkg);
        }
    }
    
    if (n_array_size(pkgs) == 0) {
        n_array_free(pkgs);
        pkgs = NULL;
    }
    
    return pkgs;
}

static 
int restore_pkg_fields(FILE *stream, uint32_t *size, uint32_t *fsize,
                       uint32_t *btime, uint32_t *groupid) 
{
    uint32_t nsize = 0, nfsize = 0, nbtime = 0, nread = 0, ngroupid = 0;
    uint8_t n;
    

    nread += fread(&n, sizeof(n), 1, stream);
    nread += fread(&nsize, sizeof(nsize), 1, stream);
    nread += fread(&nfsize, sizeof(nfsize), 1, stream);
    nread += fread(&nbtime, sizeof(nbtime), 1, stream);
    nread += fread(&ngroupid, sizeof(ngroupid), 1, stream);

    *size    = ntoh32(nsize);
    *fsize   = ntoh32(nfsize);
    *btime   = ntoh32(nbtime);
    *groupid = ntoh32(ngroupid);
    getc(stream);               /* eat '\n' */
    return nread == 5;
}


int pkgdir_uniq(struct pkgdir *pkgdir) 
{
    int n = 0;

    pkgdir->flags |= PKGDIR_UNIQED;
    
    if (pkgdir->pkgs == NULL || n_array_size(pkgdir->pkgs) == 0)
        return 0;

    n = n_array_size(pkgdir->pkgs);
    n_array_isort_ex(pkgdir->pkgs, (tn_fn_cmp)pkg_deepcmp_name_evr_rev_verify);
    n_array_uniq_ex(pkgdir->pkgs, (tn_fn_cmp)pkg_cmp_uniq);
    n -= n_array_size(pkgdir->pkgs);
    
    if (n) {
        char m[1024], *name;
        
        snprintf(m, sizeof(m), ngettext("removed %d duplicate package",
                                        "removed %d duplicate packages", n), n);
        
        name = pkgdir->idxpath ? pkgdir->idxpath :
            pkgdir->path ? pkgdir->idxpath : pkgdir->name;
        
        if (name)
            logn(LOGWARN, "%s: %s", name, m);
        else 
            logn(LOGWARN, "%s", m);
    }
    return n;
}


static int is_uptodate(const char *path, const struct pdigest *pdg_local,
                       struct pdigest *pdg_remote)
{
    char                   mdpath[PATH_MAX], mdtmpath[PATH_MAX];
    struct pdigest         remote_pdg;
    int                    fd, n, rc = 0;
    const char             *ext = pdigest_ext;

    
    if (pdg_remote)
        pdigest_init(pdg_remote);
    
    pdigest_init(&remote_pdg);
    if (pkgdir_v016compat)
        remote_pdg.mode = PDIGEST_MODE_v016;
    
    if (vf_url_type(path) & VFURL_LOCAL)
        return 1;
    
    if (!(n = vf_mksubdir(mdtmpath, sizeof(mdtmpath), "tmpmd"))) {
        rc = -1;
        goto l_end;
    }

    if (pkgdir_v016compat)
        ext = pdigest_ext_v016;
    
    mkdigest_path(mdpath, sizeof(mdpath), path, ext);
    
    snprintf(&mdtmpath[n], sizeof(mdtmpath) - n, "/%s", n_basenam(mdpath));
    unlink(mdtmpath);
    mdtmpath[n] = '\0';

    if (!vfile_fetch(mdtmpath, mdpath, VFURL_UNKNOWN)) {
        rc = -1;
        goto l_end;
    }
    
    mdtmpath[n] = '/';
    
    if ((fd = open(mdtmpath, O_RDONLY)) < 0 ||
        !pdigest_readfd(&remote_pdg, fd, mdtmpath)) {
        
        close(fd);
        rc = -1;
        goto l_end;
    }
    close(fd);

    if (pkgdir_v016compat == 0) {
        rc = (memcmp(pdg_local->mdd, &remote_pdg.mdd, sizeof(remote_pdg.mdd)) == 0);
        
    } else {
        n_assert(pdg_local->md);
        n_assert(remote_pdg.md);
        rc = (strcmp(pdg_local->md, remote_pdg.md) == 0);
    }
    
    if (!rc && pdg_remote)
        memcpy(pdg_remote, &remote_pdg, sizeof(remote_pdg));
    
 l_end:
    pdigest_destroy(&remote_pdg);
    return rc;
}


int pkgdir_isremote(struct pkgdir *pkgdir)
{
    return vf_url_type(pkgdir->path) & VFURL_REMOTE;
}


