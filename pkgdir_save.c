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
#include "h2n.h"

static
int vaccum_difflist(const char *idxpath, const char *difftoc_path);


static
char *mkidx_pathname(char *dest, size_t size, const char *pathname,
                     const char *suffix) 
{
    char *ext, *bn = NULL;
    int suffix_len;

    suffix_len = strlen(suffix);
    
    if (strlen(pathname) + suffix_len + 1 > size)
        return NULL;
    
    bn = n_basenam(pathname);
    if ((ext = strrchr(bn, '.')) == NULL || strcmp(ext, ".dir") == 0) {
        snprintf(dest, size, "%s%s", pathname, suffix);
        
    } else {
        int len = ext - pathname + 1;
        n_assert(len + suffix_len + strlen(ext) + 1 < size);
        n_strncpy(dest, pathname, len);
        strcat(dest, suffix);
        
        if (strstr(suffix, ext) == NULL)
            strcat(dest, ext);
        dest[size - 1] = '\0';
    }

    return dest;
}

static
int fprintf_pkg_caps(const struct pkg *pkg, FILE *stream) 
{
    tn_array *arr;
    int i;

    arr = n_array_new(32, NULL, NULL);
    for (i=0; i<n_array_size(pkg->caps); i++) {
        struct capreq *cr = n_array_nth(pkg->caps, i);
        if (pkg_eq_capreq(pkg, cr))
            continue;
        n_array_push(arr, cr);
    }

    if (n_array_size(arr)) 
        i = capreq_arr_store(arr, stream, "P:\n");
    else
        i = 1;
    
    n_array_free(arr);
    return i;
}

static 
void store_pkg_fields(FILE *stream, uint32_t size, uint32_t fsize,
                      uint32_t btime, uint32_t groupid) 
{
    uint32_t nsize, nfsize, nbtime, ngroupid;
    uint8_t n = 4;

    nsize = hton32(size);
    nfsize = hton32(fsize);
    nbtime = hton32(btime);
    ngroupid = hton32(groupid);
    
    fwrite(&n, sizeof(n), 1, stream);
    fwrite(&nsize, sizeof(nsize), 1, stream);
    fwrite(&nfsize, sizeof(nfsize), 1, stream);
    fwrite(&nbtime, sizeof(nbtime), 1, stream);
    fwrite(&ngroupid, sizeof(ngroupid), 1, stream);
    
    fprintf(stream, "\n");
}

static void fprintf_pkg_fl(const struct pkg *pkg, FILE *stream, tn_array *depdirs) 
{
    tn_array *fl;
    void     *flmark;

    
    flmark = pkgflmodule_allocator_push_mark();
    fl = pkg_info_get_fl(pkg);

    if (fl && n_array_size(fl) == 0) {
        n_array_free(fl);
        fl = NULL;
    }
    	
    if (fl == NULL) {
        pkgflmodule_allocator_pop_mark(flmark);
        return;
    }
    
    pkgfl_array_store_order(fl);
        
    if (depdirs == NULL) {
        fprintf(stream, "l:\n");
        pkgfl_store_f(fl, stream, depdirs, PKGFL_ALL);
        
    } else {
        fprintf(stream, "L:\n");
        pkgfl_store_f(fl, stream, depdirs, PKGFL_DEPDIRS);
    
        fprintf(stream, "l:\n");
        pkgfl_store_f(fl, stream, depdirs, PKGFL_NOTDEPDIRS);
    }
    
    pkg_info_free_fl(pkg, fl);
    pkgflmodule_allocator_pop_mark(flmark);
}


static
int fprintf_pkg(const struct pkg *pkg, FILE *stream, tn_array *depdirs, int nodesc)
{
    fprintf(stream, "N: %s\n", pkg->name);
    if (pkg->epoch)
        fprintf(stream, "V: %d:%s-%s\n", pkg->epoch, pkg->ver, pkg->rel);
    else 
        fprintf(stream, "V: %s-%s\n", pkg->ver, pkg->rel);

    if (pkg->arch)
        fprintf(stream, "A: %s\n", pkg->arch);
    
    if (pkg->os)
        fprintf(stream, "O: %s\n", pkg->os);
    
    fprintf(stream, "F:\n");
    store_pkg_fields(stream, pkg->size, pkg->fsize, pkg->btime, pkg->groupid);

    if (pkg->caps && n_array_size(pkg->caps))
        fprintf_pkg_caps(pkg, stream);
    
    if (pkg->reqs && n_array_size(pkg->reqs)) 
        capreq_arr_store(pkg->reqs, stream, "R:\n");
    
    if (pkg->cnfls && n_array_size(pkg->cnfls)) 
        capreq_arr_store(pkg->cnfls, stream, "C:\n");

    //mem_info(-10, "before fl");
    fprintf_pkg_fl(pkg, stream, depdirs);
    //mem_info(-10, "after fl");
    if (nodesc == 0) {
        struct pkguinf *pkgu;
        
        //mem_info(-10, "before uinf");
        if ((pkgu = pkg_info(pkg))) {
            fprintf(stream, "U:\n");
            pkguinf_store(pkgu, stream);
            fprintf(stream, "\n");
            pkguinf_free(pkgu);
            //mem_info(-10, "after uinf");
        }
    }
    
    fprintf(stream, "\n");
    return 1;
}

static 
void put_fheader(FILE *stream, const char *name, struct pkgdir *pkgdir) 
{
    char datestr[128];
    
    strftime(datestr, sizeof(datestr),
             "%a, %d %b %Y %H:%M:%S GMT", gmtime(&pkgdir->ts));
    
    fprintf(stream,
            "# %s v%d.%d\n"
            "# This file was generated by poldek " VERSION " on %s.\n"
            "# PLEASE DO *NOT* EDIT or poldek will hate you.\n"
            "# Contains %d packages",
            name, FILEFMT_MAJOR, FILEFMT_MINOR,
            datestr, pkgdir->pkgs ? n_array_size(pkgdir->pkgs) : 0);
    
    if (pkgdir->flags & PKGDIR_DIFF) {
        strftime(datestr, sizeof(datestr),
             "%a, %d %b %Y %H:%M:%S GMT", gmtime(&pkgdir->ts_orig));
        fprintf(stream, ", %d removed (diff from %s)",
                pkgdir->removed_pkgs ? n_array_size(pkgdir->removed_pkgs) : 0,
                datestr);
    }

    fprintf(stream, "\n");
}


static int do_unlink(const char *path) 
{
    struct stat st;
    
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        return vf_localunlink(path);
        
    return 0;
}


int pkgdir_create_idx(struct pkgdir *pkgdir, const char *pathname,
                      unsigned flags)
{
    struct vfile     *vf = NULL, *vf_toc = NULL;
    char             tocpath[PATH_MAX], path[PATH_MAX], tmp[PATH_MAX], 
                     difftoc_path[PATH_MAX];
    char             suffix[64] = "", tocsuffix[64] = ".toc",
                     difftoc_suffix[PATH_MAX] = "";
    const char       *orig_pathname;
    int              i, with_toc = 1;

    
    if ((flags & PKGDIR_CREAT_asCACHE) == 0 && 
        (pkgdir->flags & (PKGDIR_DIFF | PKGDIR_UNIQED)) == 0) {
        n_assert(0);
        pkgdir_uniq(pkgdir);
    }

    if (pkgdir->ts == 0) 
        pkgdir->ts = time(0);

    if (pathname == NULL && pkgdir->vf) 
        pathname = vfile_localpath(pkgdir->vf);

    if (pathname == NULL && pkgdir->idxpath)
        pathname = pkgdir->idxpath;

    n_assert(pathname);
    orig_pathname = pathname;
    
    if ((pkgdir->flags & (PKGDIR_DIFF | PKGDIR_PATCHED)))
        flags |= PKGDIR_CREAT_woTOC;
    else
        flags |= PKGDIR_CREAT_wMD;
    
    if (pkgdir->flags & PKGDIR_DIFF) {
        char tstr[32];
        char *dn, *bn;

        strftime(tstr, sizeof(tstr), "%Y.%m.%d-%H.%M.%S", gmtime(&pkgdir->ts_orig));
        snprintf(suffix, sizeof(suffix), ".diff.%s", tstr);
        snprintf(tocsuffix, sizeof(tocsuffix), ".diff.%s.toc", tstr);
        
        snprintf(difftoc_suffix, sizeof(difftoc_suffix), "%s", pdir_difftoc_suffix);

        memcpy(tmp, pathname, sizeof(tmp));
        n_basedirnam(tmp, &dn, &bn);
        if (!mk_dir(dn, pdir_packages_incdir))
            return 0;

        snprintf(path, sizeof(path), "%s/%s/%s", dn, pdir_packages_incdir, bn);
        memcpy(tmp, path, sizeof(tmp));
        pathname = tmp;
    }


    if (flags & PKGDIR_CREAT_woTOC)
        with_toc = 0;
    
    if (with_toc)
        if (mkidx_pathname(tocpath, sizeof(tocpath), pathname, tocsuffix) == NULL) {
            logn(LOGERR, "cannot prepare tocpath");
            return 0;
        }

    if (mkidx_pathname(path, sizeof(path), pathname, suffix) == NULL) {
        logn(LOGERR, "cannot prepare idxpath");
        return 0;
    }

    msgn_tty(1, _("Writing %s..."), vf_url_slim_s(path, 0));
    msgn_f(1, _("Writing %s..."), path);
    
    if (with_toc) {
        if ((vf_toc = vfile_open(tocpath, VFT_STDIO, VFM_RW)) == NULL)
            return 0;
        put_fheader(vf_toc->vf_stream, pdir_poldeksindex_toc, pkgdir);
    }

    do_unlink(path);

    if ((vf = vfile_open(path, VFT_STDIO, VFM_RW)) == NULL) {
        if (vf_toc)
            vfile_close(vf_toc);
        return 0;
    }
    
    put_fheader(vf->vf_stream, pdir_poldeksindex, pkgdir);
    
    
    fprintf(vf->vf_stream, "%%%s%lu\n", pdir_tag_ts, pkgdir->ts);
    fprintf(vf->vf_stream, "%%%s\n", pdir_tag_endvarhdr);

    if (pkgdir->flags & PKGDIR_DIFF) {
        fprintf(vf->vf_stream, "%%%s%lu\n", pdir_tag_ts_orig, pkgdir->ts_orig);

        if (pkgdir->removed_pkgs) {
            fprintf(vf->vf_stream, "%%%s", pdir_tag_removed);
            for (i=0; i < n_array_size(pkgdir->removed_pkgs); i++) {
                struct pkg *pkg = n_array_nth(pkgdir->removed_pkgs, i);
            
                fprintf(vf->vf_stream, "%s-", pkg->name);
                if (pkg->epoch)
                    fprintf(vf->vf_stream, "%d:", pkg->epoch);
                fprintf(vf->vf_stream, "%s-%s ", pkg->ver, pkg->rel);
            }
            fprintf(vf->vf_stream, "\n");
        }
    }

    if (pkgdir->depdirs && n_array_size(pkgdir->depdirs)) {
        fprintf(vf->vf_stream, "%%%s", pdir_tag_depdirs);
        
        for (i=0; i<n_array_size(pkgdir->depdirs); i++) {
            fprintf(vf->vf_stream, "%s%c",
                    (char*)n_array_nth(pkgdir->depdirs, i),
                    i + 1 == n_array_size(pkgdir->depdirs) ? '\n':':');
        }
    }

    if (pkgdir->pkgroups) 
        pkgroup_idx_store(pkgdir->pkgroups, vf->vf_stream);

    fprintf(vf->vf_stream, "%%%s\n", pdir_tag_endhdr);


    if (pkgdir->pkgs == NULL)
        goto l_end;
    n_array_sort(pkgdir->pkgs);
    
    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
        fprintf_pkg(pkg, vf->vf_stream, pkgdir->depdirs,
                    flags & PKGDIR_CREAT_NODESC);
        if (with_toc)
            fprintf(vf_toc->vf_stream, "%s\n", pkg_snprintf_s(pkg));
#if 0                           /* debug stuff */
        if (i % 200 == 0) {
            printf("%d. ", i);
            mem_info(-10, "i");
        }
#endif        
    }

 l_end:
    vfile_close(vf);
    if (vf_toc)
        vfile_close(vf_toc);

    /* update *.diff list   */
    if (pkgdir->flags & PKGDIR_DIFF) {
        if (mkidx_pathname(difftoc_path, sizeof(difftoc_path),
                           pathname, difftoc_suffix) == NULL) {
            logn(LOGERR, "cannot prepare idxpath");
            return 0;
        }
        
        if ((vf = vfile_open(difftoc_path, VFT_STDIO, VFM_APPEND)) == NULL)
            return 0;
        fprintf(vf->vf_stream, "%s %lu %s %lu\n", 
                n_basenam(path), pkgdir->ts, pkgdir->mdd_orig, pkgdir->ts_orig);
        vfile_close(vf);
    }
    
    i = i_pkgdir_creat_digest(pkgdir, path, flags & PKGDIR_CREAT_wMD);
    if (i && (flags & PKGDIR_CREAT_wMD5))
        i = i_pkgdir_creat_md5(path);
    
    if (pkgdir->flags & PKGDIR_DIFF)
        vaccum_difflist(orig_pathname, difftoc_path);
    return i;
}


static
int vaccum_difflist(const char *idxpath, const char *difftoc_path) 
{
    tn_array     *lines; 
    char         linebuf[2048], *dn, *bn;
    char         tmp[PATH_MAX], difftoc_path_bak[PATH_MAX];
    struct stat  st_idx, st;
    struct vfile *vf;
    int          lineno, i;
    off_t        diffs_size;
    
    if (stat(idxpath, &st_idx) != 0) {
        logn(LOGERR, "vaccum diff: stat %s: %m", idxpath);
        return 0;
    }

    memcpy(tmp, difftoc_path, sizeof(tmp));
    n_basedirnam(tmp, &dn, &bn);

    
    if ((vf = vfile_open(difftoc_path, VFT_STDIO, VFM_RO)) == NULL)
        return 0;
    
    lines = n_array_new(128, NULL, NULL);
    while (fgets(linebuf, sizeof(linebuf), vf->vf_stream)) {
        char *l;
        int len;

        len = strlen(linebuf);
        l = alloca(len + 1);
        memcpy(l, linebuf, len + 1);
        n_array_push(lines, l);
        DBGF("l = [%s]\n", l);
    }
    
    if (n_array_size(lines)) {
        snprintf(difftoc_path_bak, sizeof(difftoc_path_bak), "%s-", difftoc_path);
        rename(difftoc_path, difftoc_path_bak);
    }
    vfile_close(vf);

    if ((vf = vfile_open(difftoc_path, VFT_STDIO, VFM_RW)) == NULL) {
        rename(difftoc_path_bak, difftoc_path);
        n_array_free(lines);
        return 0;
    }

    
    lineno = 0;
    diffs_size = 0;
    for (i = n_array_size(lines) - 1; i >= 0; i--) {
        char *p, *l, path[PATH_MAX];

        l = n_array_nth(lines, i);
        if ((p = strchr(l, ' ')) == NULL) {
            logn(LOGERR, _("%s: format error"), difftoc_path);
            *l = '\0';
            continue;
        }
        
        *p = '\0';
        /*                         "- 1" to save space for ".mdd" (to unlink mdd too) */
        snprintf(path, sizeof(path) - 1, "%s/%s", dn, l);

        *p = ' ';
        
        if (stat(path, &st) != 0) {
            if (errno != ENOENT)
                logn(LOGERR, "vaccum diff: stat %s: %m", l);
            *l = '\0';
            continue;
        }
        DBGF("path = %s %ld, %ld, %ld\n", path, st.st_size, diffs_size,
             st_idx.st_size);
        
        if (lineno) {
            if (vf_valid_path(path)) {
                char *p;
                
                msgn(1, _("Removing outdated %s"), n_basenam(path));
                unlink(path);
                if ((p = strrchr(path, '.')) && strcmp(p, ".gz") == 0) {
                    strcpy(p, ".mdd");
                    //msgn(1, _("Removing outdated MDD %s"), n_basenam(path));
                    unlink(path);
                }
            }
            
        } else {
            if (diffs_size + st.st_size > (st_idx.st_size * 0.9))
                lineno = i;
            else
                diffs_size += st.st_size;
        }
    }

    for (i = lineno; i < n_array_size(lines); i++) {
        char *l;
        
        l = n_array_nth(lines, i);
        if (*l)
            fprintf(vf->vf_stream, "%s", l);
    }

    vfile_close(vf);
    n_array_free(lines);
    return 1;
}

