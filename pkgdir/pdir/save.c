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
#include <unistd.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>
#include <trurl/nstream.h>
#include <trurl/n_snprintf.h>

#include <vfile/vfile.h>

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "pkgdir.h"
#include "pkg.h"
#include "pkgroup.h"
#include "pdir.h"

static
int pdir_difftoc_vacuum(const char *idxpath, const char *diffpath,
                        const char *suffix);

static
int pdir_difftoc_update(const char *diffpath, const char *suffix,
                        const char *line, int line_len);


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
void put_fheader(tn_stream *st, const char *name, struct pkgdir *pkgdir) 
{
    char datestr[128];
    
    strftime(datestr, sizeof(datestr),
             "%a, %d %b %Y %H:%M:%S GMT", gmtime(&pkgdir->ts));
    
    n_stream_printf(st,
            "# %s v%d.%d\n"
            "# This file was generated by poldek " VERSION " on %s.\n"
            "# PLEASE DO *NOT* EDIT or poldek will hate you.\n"
            "# Contains %d packages",
            name, FILEFMT_MAJOR, FILEFMT_MINOR,
            datestr, pkgdir->pkgs ? n_array_size(pkgdir->pkgs) : 0);
    
    if (pkgdir->flags & PKGDIR_DIFF) {
        strftime(datestr, sizeof(datestr),
                 "%a, %d %b %Y %H:%M:%S GMT", gmtime(&pkgdir->orig_ts));
        n_stream_printf(st, ", %d removed (diff from %s)",
                        pkgdir->removed_pkgs ?
                        n_array_size(pkgdir->removed_pkgs) : 0,
                        datestr);
    }

    n_stream_printf(st, "\n");
}


static int do_unlink(const char *path) 
{
    struct stat st;
    
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        return vf_localunlink(path);
        
    return 0;
}

/* moved from pkg.c -- pdir rely on packages order strictly */
static __inline__
int do_pkg_deepcmp(const struct pkg *p1, const struct pkg *p2) 
{
    register int rc;
    
    if ((rc = p1->btime - p2->btime))
        return rc;

    if ((rc = p1->size - p2->size))
        return rc;

    if ((rc = p1->fsize - p2->fsize))
        return rc;

    if (p1->arch && p2->arch == NULL)
        return 1;

    if (p1->arch == NULL && p2->arch)
        return -1;

    if ((rc = strcmp(p1->arch, p2->arch)))
        return rc;
    
    if (p1->os && p2->os == NULL)
        return 1;
    
    if (p1->os == NULL && p2->os)
        return -1;

    return strcmp(p1->os, p2->os);
}


static
int pdir_pkg_cmp(const struct pkg *p1, const struct pkg *p2)
{
    register int rc;

    if ((rc = pkg_cmp_name(p1, p2)))
        return rc;

    if ((rc = p1->epoch - p2->epoch))
        return rc;

    n_assert(p1->ver && p2->ver && p1->rel && p2->rel);

    if ((rc = strcmp(p1->ver, p2->ver)))
        return rc;
    
    if ((rc = strcmp(p1->rel, p2->rel)))
        return rc;

    return do_pkg_deepcmp(p1, p2);
}

int pdir_create(struct pkgdir *pkgdir, const char *pathname,
                unsigned flags)
{
    struct vfile     *vf = NULL;
    char             path[PATH_MAX], tmp[PATH_MAX];
    char             suffix[64] = "", difftoc_suffix[256] = "";
    const char       *orig_pathname;
    int              i, nerr = 0;
    struct pdir      *idx;
    
    if ((pkgdir->flags & PKGDIR_DIFF) && (pkgdir->flags & PKGDIR_UNIQED) == 0) {
        n_assert((flags & PKGDIR_CREAT_NOUNIQ) == 0);
        pkgdir__uniq(pkgdir);
    }

    idx = pkgdir->mod_data;
    //printf("idx0 = %p\n", idx);
    if (pkgdir->ts == 0) 
        pkgdir->ts = time(0);

    if (pathname == NULL) {
        if (pkgdir->flags & PKGDIR_DIFF)
            pathname = pkgdir->orig_idxpath;
        else
            pathname = pdir_localidxpath(pkgdir);
    }
    
    n_assert(pathname);
    orig_pathname = pathname;
    
    //if ((pkgdir->flags & (PKGDIR_DIFF | PKGDIR_PATCHED)) == 0)
    //    flags |= PKGDIR_CREAT_wMD;

    
    if (pkgdir->flags & PKGDIR_DIFF) {
        char tstr[32];
        char *dn, *bn;

        strftime(tstr, sizeof(tstr), "%Y.%m.%d-%H.%M.%S",
                 gmtime(&pkgdir->orig_ts));
        
        snprintf(suffix, sizeof(suffix), ".diff.%s", tstr);
        snprintf(difftoc_suffix, sizeof(difftoc_suffix), "%s",
                 pdir_difftoc_suffix);

        memcpy(tmp, pathname, sizeof(tmp));
        n_basedirnam(tmp, &dn, &bn);
        if (!mk_dir(dn, pdir_packages_incdir)) {
			nerr++;
            goto l_end;
		}
		

        snprintf(path, sizeof(path), "%s/%s/%s", dn, pdir_packages_incdir, bn);
        memcpy(tmp, path, sizeof(tmp));
        pathname = tmp;
    }


    if (mkidx_pathname(path, sizeof(path), pathname, suffix) == NULL) {
        nerr++;
		goto l_end;
    }

    msgn_tty(1, _("Writing %s..."), vf_url_slim_s(path, 0));
    msgn_f(1, _("Writing %s..."), path);
    

    do_unlink(path);
    if ((vf = vfile_open(path, VFT_TRURLIO, VFM_RW)) == NULL) {
		nerr++;
		goto l_end;
    }
    
    put_fheader(vf->vf_tnstream, pdir_poldeksindex, pkgdir);
    
    
    n_stream_printf(vf->vf_tnstream, "%%%s%lu\n", pdir_tag_ts, pkgdir->ts);
    n_stream_printf(vf->vf_tnstream, "%%%s\n", pdir_tag_endvarhdr);

    if (pkgdir->flags & PKGDIR_DIFF) {
        n_stream_printf(vf->vf_tnstream, "%%%s%lu\n", pdir_tag_ts_orig,
                        pkgdir->orig_ts);

        if (pkgdir->removed_pkgs) {
            n_stream_printf(vf->vf_tnstream, "%%%s", pdir_tag_removed);
            for (i=0; i < n_array_size(pkgdir->removed_pkgs); i++) {
                struct pkg *pkg = n_array_nth(pkgdir->removed_pkgs, i);
            
                n_stream_printf(vf->vf_tnstream, "%s-", pkg->name);
                if (pkg->epoch)
                    n_stream_printf(vf->vf_tnstream, "%d:", pkg->epoch);
                n_stream_printf(vf->vf_tnstream, "%s-%s ", pkg->ver, pkg->rel);
            }
            n_stream_printf(vf->vf_tnstream, "\n");
        }
    }

    if (pkgdir->depdirs && n_array_size(pkgdir->depdirs)) {
        n_stream_printf(vf->vf_tnstream, "%%%s", pdir_tag_depdirs);
        
        for (i=0; i<n_array_size(pkgdir->depdirs); i++) {
            n_stream_printf(vf->vf_tnstream, "%s%c",
                    (char*)n_array_nth(pkgdir->depdirs, i),
                    i + 1 == n_array_size(pkgdir->depdirs) ? '\n':':');
        }
    }

    if (pkgdir->pkgroups) {
        tn_buf *nbuf = n_buf_new(8192);
        
        n_stream_printf(vf->vf_tnstream, "%%%s\n", pdir_tag_pkgroups);
        pkgroup_idx_store(pkgdir->pkgroups, nbuf);
        n_stream_write(vf->vf_tnstream, n_buf_ptr(nbuf), n_buf_size(nbuf));
        n_stream_printf(vf->vf_tnstream, "\n");
        n_buf_free(nbuf);
        //pkgroup_idx_store_st(pkgdir->pkgroups, vf->vf_tnstream);
    }
        
    n_stream_printf(vf->vf_tnstream, "%%%s\n", pdir_tag_endhdr);

    if (pkgdir->pkgs == NULL)
        goto l_close;

    //flags |= PKGDIR_CREAT_PKG_Fv017;
    //n_array_sort(pkgdir->pkgs);
    n_array_isort_ex(pkgdir->pkgs, (tn_fn_cmp)pdir_pkg_cmp);
    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
        pdir_pkg_store(pkg, vf->vf_tnstream, pkgdir->depdirs, flags);
#if 0                           /* debug stuff */
        if (i % 200 == 0) {
            printf("%d. ", i);
            mem_info(-10, "i");
        }
#endif        
    }

 l_close:
	if (vf) {
		vfile_close(vf);
		vf = NULL;
	}
	
    /* update *.diff list   */
    if (pkgdir->flags & PKGDIR_DIFF) {
        char line[1024];
        int  line_len;
        
        line_len = n_snprintf(line, sizeof(line), "%s %lu %s %lu\n", 
                              n_basenam(path), pkgdir->ts,
                              idx->mdd_orig, pkgdir->orig_ts);
        
        if (!pdir_difftoc_update(pathname, difftoc_suffix, line, line_len)) {
            nerr++;
            goto l_end;
        }
    }
    
    i = pdir_digest_create(pkgdir, path, 0); // skip .md creation,
                                             // old, old poldek  

	if (!i)
		nerr++;
    
    if (pkgdir->flags & PKGDIR_DIFF)
        pdir_difftoc_vacuum(orig_pathname, pathname, difftoc_suffix);
	
 l_end:
    return nerr == 0;
}

static
int pdir_difftoc_update(const char *diffpath, const char *suffix,
                        const char *line, int line_len)
{
    char path[PATH_MAX];
    struct vfile *vf;
    int rc = 0;
        
    if (mkidx_pathname(path, sizeof(path), diffpath, suffix) == NULL)
        return 0;
    
    if ((vf = vfile_open(path, VFT_TRURLIO, VFM_APPEND))) {
        rc = (n_stream_write(vf->vf_tnstream, line, line_len) == line_len);
        vfile_close(vf);
    }
    DBGF("%s: added %s: [%s]\n", path, line, rc ? "OK" : "FAILED");
    return rc;
}


static
int pdir_difftoc_vacuum(const char *idxpath, const char *diffpath,
                        const char *suffix)
{
    tn_array     *lines; 
    char         line[2048], *dn, *bn, tmp[PATH_MAX];
    char         difftoc_path[PATH_MAX], difftoc_path_bak[PATH_MAX];
    struct stat  st_idx, st;
    struct vfile *vf;
    int          lineno, i, len;
    off_t        diffs_size;
    
    if (stat(idxpath, &st_idx) != 0) {
        logn(LOGERR, "vaccum diff: stat %s: %m", idxpath);
        return 0;
    }

    if (!mkidx_pathname(difftoc_path, sizeof(difftoc_path), diffpath, suffix))
        return 0;
    
    n_strncpy(tmp, difftoc_path, sizeof(tmp));
    n_basedirnam(tmp, &dn, &bn);

    
    if ((vf = vfile_open(difftoc_path, VFT_TRURLIO, VFM_RO)) == NULL)
        return 0;
    
    lines = n_array_new(128, NULL, NULL);
    while ((len = n_stream_gets(vf->vf_tnstream, line, sizeof(line))) > 0) {
        char *l;

        l = alloca(len + 1);
        memcpy(l, line, len + 1);
        n_array_push(lines, l);
        DBGF("l = [%s]\n", l);
    }
    
    if (n_array_size(lines)) {
        snprintf(difftoc_path_bak, sizeof(difftoc_path_bak), "%s-",
                 difftoc_path);
        rename(difftoc_path, difftoc_path_bak);
    }
    vfile_close(vf);

    if ((vf = vfile_open(difftoc_path, VFT_TRURLIO, VFM_RW)) == NULL) {
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
            logn(LOGERR, _("%s: format error"), path);
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
        DBGF("path = (%s) %ld, %ld, %ld\n", path, st.st_size, diffs_size,
             st_idx.st_size);
        
        if (lineno) {
            if (vf_valid_path(path)) {
                char *p;
                
                msgn(1, _("Removing outdated diff %s"), n_basenam(path));
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
            n_stream_printf(vf->vf_tnstream, "%s", l);
    }

    vfile_close(vf);
    n_array_free(lines);
    return 1;
}

