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

#include <limits.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/nstr.h>
#include <trurl/nmalloc.h>
#include <trurl/n_snprintf.h>
#include <vfile/vfile.h>

#include "i18n.h"
#include "log.h"
#include "arg_packages.h"
#include "misc.h"
#include "pkgmisc.h"
#include "pkgset.h"
#include "pm/pm.h"

#define ARG_PACKAGES_SETUPDONE    (1 << 0)

/* @VIRTUAL [DEFAULT_MASK [EVR]] */
struct pset_virtual_ent {
    char *virtname;
    char *mask;
    char _buf[0];
};
    

struct arg_packages {
    unsigned  flags;
    tn_array  *packages;        /* struct *pkg[] */
    tn_array  *package_masks;   /* [@]foo(#|-)[VERSION[-RELEASE]] || foo.rpm   */
    tn_array  *package_lists;   /* --pset FILE */
    tn_array  *package_files;   /* list of *.rpm */
    tn_hash   *pset_virtuals;   /* @virt name */
    tn_hash   *resolved_caps;
    tn_array  *resolved_pkgs;
    tn_alloc  *na;
};

static int make_mask(char *mask, int msize, const char *name,
                     const char *version, const char *release)
{
    int n = 0;
    
    n = n_snprintf(mask, msize, "%s", name);
    if (version) {
        n += n_snprintf(&mask[n], msize - n, "-%s", version);
        if (release)
            n += n_snprintf(&mask[n], msize - n, "-%s", release);
        else 
            n += n_snprintf(&mask[n], msize - n, "-*");
    }
    return n;
}


static 
int prepare_file_pkgmask(struct arg_packages *aps,
                         const char *maskstr, const char *fpath, int nline)
{
    char               *p, *s[1024], *buf, mask[1024];
    const char         **tl, **tl_save;
    const char         *evrstr = NULL, *name = NULL, *virtname = NULL;
    const char         *version = NULL, *release = NULL;
    int32_t            epoch = 0;
    int                is_virtual;
    
    n_strdupap(maskstr, &buf);
    s[0] = NULL;
    p = n_str_strip_ws(buf);
        
    if (*p == '\0' || *p == '#')
        return 0;

    is_virtual = 0;
    while (*p && !isalnum(*p)) {
        switch (*p) {
            case '!':           /* for backward compatybility */
            case '~':
                if (poldek_VERBOSE > 1)
                    logn(LOGNOTICE, "%s: skipped optional item", p + 1);
                return 0;
                break;
                
            case  '@':          /* virtual */
                is_virtual = 1;
                break;
        }
        p++;
    }
    
    if (!isalnum(*p)) {
        if (nline > 0)
            logn(LOGERR, _("%s:%d: syntax error"), fpath, nline);
        else 
            logn(LOGERR, _("syntax error in package specification"));
        return 0;
    }

    tl = tl_save = n_str_tokl(p, "#\t ");
    
    if (is_virtual) {
        virtname = tl[0];
        if (virtname) 
            name = tl[1];
        
        if (name) 
            evrstr = tl[2];
        
    } else {
        virtname = NULL;
        name = tl[0];
        evrstr = tl[1];
    }
    
    DBGF("virtname = %s, name = %s, evrstr = %s\n",
         virtname, name, evrstr);
    
    if (evrstr) 
        poldek_util_parse_evr((char*)evrstr, &epoch, &version, &release);
        
                
    if (virtname == NULL) {
        if (name && make_mask(mask, sizeof(mask), name, version, release))
            arg_packages_add_pkgmask(aps, mask);
        
    } else {
        struct pset_virtual_ent *ent;
        int n, len = strlen(virtname) + 1;

        if (name) {
            len += strlen(name) + 1;
            if (evrstr)
                len += strlen(evrstr) + 1;
        }
        ent = n_malloc(sizeof(*ent) + len + 1);
        n = n_snprintf(ent->_buf, len, "%s", virtname);
        ent->virtname = ent->_buf;
        ent->mask = NULL;
        if (name && make_mask(mask, sizeof(mask), name, version, release)) {
            n++;                /* skip '\0' */
            n_snprintf(&ent->_buf[n], len - n, "%s", mask);
            ent->mask = &ent->_buf[n];
        }

        if (aps->pset_virtuals == NULL)
            aps->pset_virtuals = n_hash_new(16, free);

        n_hash_replace(aps->pset_virtuals, ent->virtname, ent);
    }
    
    n_str_tokl_free(tl_save);
    return 1;
}


struct arg_packages *arg_packages_new(void) 
{
    struct arg_packages *aps;

    aps = n_malloc(sizeof(*aps));
    memset(aps, 0, sizeof(*aps));
    aps->packages = pkgs_array_new(64);
    aps->package_masks = n_array_new(64, free, (tn_fn_cmp)strcmp);
    aps->package_lists = n_array_new(64, free, (tn_fn_cmp)strcmp);
    aps->package_files = n_array_new(4, free, (tn_fn_cmp)strcmp); 
    aps->resolved_caps = n_hash_new(21, (tn_fn_free)n_array_free);
    aps->resolved_pkgs = pkgs_array_new_ex(128, pkg_cmp_name_evr_rev);
    return aps;
}

void arg_packages_free(struct arg_packages *aps) 
{
    n_array_free(aps->packages);
    n_array_free(aps->package_masks);
    n_array_free(aps->package_lists);
    n_array_free(aps->package_files);
    n_hash_free(aps->resolved_caps);
    n_array_free(aps->resolved_pkgs);
    if (aps->na)
        n_alloc_free(aps->na);
    free(aps);
}

void arg_packages_clean(struct arg_packages *aps) 
{
    n_array_clean(aps->packages);
    n_array_clean(aps->package_masks);
    n_array_clean(aps->package_lists);
    n_hash_clean(aps->resolved_caps);
    n_array_clean(aps->resolved_pkgs);
    aps->flags = 0;
}


int arg_packages_size(struct arg_packages *aps) 
{
    return n_array_size(aps->package_masks) + n_array_size(aps->packages)
        + n_array_size(aps->package_lists) + n_array_size(aps->package_files);
//        n_hash_size(aps->resolved_caps);
}

/* tries to convert N-[E:]V-R to N#[E:]V-R */
static char *mask2evrhashedmask(const char *mask) 
{
    const char *name, *ver, *rel, *p;
    char nmask[1024], e[32] = "", *tmp;
    int32_t epoch = 0;
    int n;
    
    n_strdupap(mask, &tmp);
    if (!poldek_util_parse_nevr(tmp, &name, &epoch, &ver, &rel))
        return NULL;
    
    p = ver;          /* check if it is really version */
    while (*p) {
        if (isdigit(*p))
            break;
        p++;
    }
    
    if (*p == '\0')    /* no digits => part of name propably */
        return NULL;
            
    if (epoch)
        snprintf(e, sizeof(e), "%d:", epoch);
    n = n_snprintf(nmask, sizeof(nmask), "%s#%s%s-%s", name, e, ver, rel);
    return n_strdupl(nmask, n);
}

tn_array *arg_packages_get_masks(struct arg_packages *aps, int hashed)
{
    tn_array *masks;
    int i;

    masks = n_array_clone(aps->package_masks);
    for (i=0; i < n_array_size(aps->package_masks); i++) {
        const char *mask;

        mask = n_array_nth(aps->package_masks, i);
        if (hashed && strchr(mask, '-') && strchr(mask, '*') == NULL) {
            char *nmask;

            if ((nmask = mask2evrhashedmask(mask)))
                mask = nmask;
        }
        n_array_push(masks, n_strdup(mask));
    }
    
    for (i=0; i < n_array_size(aps->packages); i++) {
        struct pkg *pkg = n_array_nth(aps->packages, i);
        char mask[1024], e[32] = "";
        int n;
        
        if (pkg->epoch)
            snprintf(e, sizeof(e), "%d:", pkg->epoch);
        
        n = n_snprintf(mask, sizeof(mask), "%s%s%s%s-%s", pkg->name,
                   hashed ? "#" : "-", e, pkg->ver, pkg->rel);
        n_array_push(masks, n_strdupl(mask, n));
    }
                   
    return masks;
}


int arg_packages_add_pkglist(struct arg_packages *aps, const char *path) 
{
    n_array_push(aps->package_lists, n_strdup(path));
    return 1;
}

int arg_packages_add_pkgmask(struct arg_packages *aps, const char *mask)
{
    
    n_array_push(aps->package_masks, n_strdup(mask));
    return 1;
}


int arg_packages_add_pkgmasks(struct arg_packages *aps, const tn_array *masks) 
{
    int i;
    for (i=0; i < n_array_size(masks); i++)
        n_array_push(aps->package_masks, n_strdup(n_array_nth(masks, i)));
    return 1;
}


static
int is_package_file(const char *path)
{
    struct stat st;
    
    if (strstr(path, ".rpm") == 0)
        return 0;

    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static int load_pkgfiles(struct arg_packages *aps, struct pm_ctx *pmctx)
{
    struct pkg *pkg;
    int i, nerr = 0;

    DBGF("%d\n", n_array_size(aps->package_files));
    for (i=0; i < n_array_size(aps->package_files); i++) {
        const char *path = n_array_nth(aps->package_files, i);
        
        if (aps->na == NULL)
            aps->na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    
        if ((pkg = pm_load_package(pmctx, aps->na, path, PKG_LDNEVR))) {
            arg_packages_add_pkg(aps, pkg);
            pkg_free(pkg);
            
        } else {
            nerr++;
        }
    }
        
    return nerr == 0;
}

int arg_packages_add_pkgfile(struct arg_packages *aps, const char *path)
{
    int rc = 1;

    DBGF("%s\n", path);
    
    if (!is_package_file(path))  
        rc = arg_packages_add_pkgmask(aps, path);
    else 
        n_array_push(aps->package_files, n_strdup(path));
    
    return rc;
}


int arg_packages_add_pkg(struct arg_packages *aps, struct pkg *pkg)
{
    n_array_push(aps->packages, pkg_link(pkg));
    return 1;
}


static 
int arg_packages_load_list(struct arg_packages *aps, const char *fpath)
{
    char buf[1024];
    struct vfile *vf;
    int nline, rc = 1;
    
    if ((vf = vfile_open(fpath, VFT_STDIO, VFM_RO)) == NULL) 
        return 0;

    nline = 0;
    while (fgets(buf, sizeof(buf), vf->vf_stream))
        prepare_file_pkgmask(aps, n_str_strip_ws(buf), fpath, ++nline);

    vfile_close(vf);
    return rc;
}

int arg_packages_setup(struct arg_packages *aps, struct pm_ctx *ctx)
{
    int i, rc = 1, n, nremoved;

    if (aps->flags & ARG_PACKAGES_SETUPDONE)
        return 1;

    if (!load_pkgfiles(aps, ctx)) 
        rc = 0;
        
    for (i=0; i < n_array_size(aps->package_lists); i++) {
        char *path = n_array_nth(aps->package_lists, i);
        
        if (!arg_packages_load_list(aps, path))
            rc = 0;
    }
    
    n = n_array_size(aps->package_masks);
    n_array_sort(aps->package_masks);
    n_array_uniq(aps->package_masks);

    nremoved = n - n_array_size(aps->package_masks);
    n = n_array_size(aps->packages); 
    n_array_sort(aps->packages);
    n_array_uniq(aps->packages);

    nremoved += n - n_array_size(aps->packages);
    

    if (nremoved > 0)
        msgn(2, _("Removed %d duplicates from given packages"), nremoved); 

    aps->flags |= ARG_PACKAGES_SETUPDONE;
    
    return rc;
}

static
tn_array *resolve_bycap(struct arg_packages *aps, struct pkgset *ps,
                        const char *mask)
{
    tn_array *pkgscaps, *pkgsfiles, *pkgs;

    if ((pkgs = n_hash_get(aps->resolved_caps, mask)))
        return pkgs;
    
    pkgscaps =  pkgset_search(ps, PS_SEARCH_CAP, mask);
    pkgsfiles = pkgset_search(ps, PS_SEARCH_FILE, mask);
    
    if (pkgscaps == NULL && pkgsfiles == NULL)
        return NULL;

    pkgs = n_array_clone(pkgscaps ? pkgscaps : pkgsfiles);
    while (pkgscaps && n_array_size(pkgscaps))
        n_array_push(pkgs, n_array_shift(pkgscaps));
    
    while (pkgsfiles && n_array_size(pkgsfiles))
        n_array_push(pkgs, n_array_shift(pkgsfiles));

    n_array_cfree(&pkgscaps);
    n_array_cfree(&pkgsfiles);
    
    n_array_sort(pkgs);
    n_array_uniq(pkgs);
    
    if (poldek_VERBOSE > 1) {
        int i;
        
        msgn(2, "%s: %d package(s) found:", mask, n_array_size(pkgs));
        for (i=0; i < n_array_size(pkgs); i++)
            msgn(2, " - %s", pkg_snprintf_s(n_array_nth(pkgs, i)));
    }
    
    n_hash_insert(aps->resolved_caps, mask, pkgs);
    return pkgs;
}

static
int resolve_masks(tn_array *pkgs,
                  struct arg_packages *aps, tn_array *avpkgs,
                  struct pkgset *ps,
                  unsigned flags)
{
    int i, j, nmasks, rc = 1;
    int *matches, *matches_bycmp;

    nmasks = n_array_size(aps->package_masks);

    matches = alloca(nmasks * sizeof(*matches));
    memset(matches, 0, nmasks * sizeof(*matches));

    matches_bycmp = alloca(nmasks * sizeof(*matches_bycmp));
    memset(matches_bycmp, 0, nmasks * sizeof(*matches_bycmp));
    
    for (i=0; i < n_array_size(avpkgs); i++) {
        struct pkg *pkg = n_array_nth(avpkgs, i);

        for (j=0; j < nmasks; j++) {
            char *mask = n_array_nth(aps->package_masks, j);
            int  skip = 0;

            switch (*mask) {
                case '~':
                case '!':           /* for backward compatybility */
                    skip = 1;       /* optional package */
                    break;
                
                case  '@':
                    mask++;
                    break;
            }
            
            
            if (strcmp(mask, pkg->name) == 0) {
                n_array_push(pkgs, pkg_link(pkg));
                matches_bycmp[j]++;
                matches[j]++;
                
            } else if (fnmatch(mask, pkg_id(pkg), 0) == 0) {
                n_array_push(pkgs, pkg_link(pkg));
                matches[j]++;
                
            }
        }
    }
    
    
    for (j=0; j < n_array_size(aps->package_masks); j++) {
        const char *mask = n_array_nth(aps->package_masks, j);

        if (matches[j] == 0 && ps && (flags & ARG_PACKAGES_RESOLV_CAPS)) {
            if (resolve_bycap(aps, ps, mask)) {
                matches[j]++;
                continue;
            }
        }
        
        if (matches[j] == 0 && (flags & ARG_PACKAGES_RESOLV_MISSINGOK) == 0) {
            logn(LOGERR, _("%s: no such package"), mask);
            rc = 0;
        }

        if ((flags & ARG_PACKAGES_RESOLV_UNAMBIGUOUS) == 0 && matches_bycmp[j] > 1) {
            int pri = (flags & ARG_PACKAGES_RESOLV_EXACT) ? LOGERR : LOGWARN;
            logn(pri, _("%s: ambiguous name"), mask);
            if (flags & ARG_PACKAGES_RESOLV_EXACT)
                rc = 0;
        }
    }

    return rc;
}


static
int resolve_pkgs(tn_array *pkgs,
                 struct arg_packages *aps, tn_array *avpkgs, unsigned flags)
{
    int i, rc = 1;

    for (i=0; i < n_array_size(aps->packages); i++) {
        struct pkg *pkg, *spkg = n_array_nth(aps->packages, i);

        if ((pkg = n_array_bsearch(avpkgs, spkg)))
            n_array_push(pkgs, pkg_link(pkg));
        
        else if ((flags & ARG_PACKAGES_RESOLV_MISSINGOK) == 0) {
            logn(LOGERR, _("%s: no such package"), pkg_snprintf_s(spkg));
            rc = 0;
        }
    }
    
    return rc;
}

static
int resolve_pset_virtuals(struct arg_packages *aps, struct pkgset *ps,
                          unsigned flags)
{
    int i, rc = 1;
    tn_array *keys;
    
    n_assert(ps);

    keys = n_hash_keys(aps->pset_virtuals);
    for (i=0; i < n_array_size(keys); i++) {
        char *vmask = n_array_nth(keys, i);
        tn_array *pkgs;
        
        if ((pkgs = resolve_bycap(aps, ps, vmask))) {
            continue;
            
        } else if ((flags & ARG_PACKAGES_RESOLV_MISSINGOK) == 0) {
            logn(LOGERR, _("%s: no such package"), vmask);
            rc = 0;
        }
    }
    
    n_array_free(keys);
    return rc;
}


static int verify_pakcage_caps(struct arg_packages *aps, tn_array *resolved_pkgs) 
{
    tn_array *keys;
    int i, j;
    
    keys = n_hash_keys_cp(aps->resolved_caps);
    n_array_sort(resolved_pkgs);
    for (i=0; i < n_array_size(keys); i++) {
        const char *cap = n_array_nth(keys, i);
        tn_array *pkgs = n_hash_get(aps->resolved_caps, cap);
        for (j=0; j < n_array_size(pkgs); j++) {
            struct pkg *pkg = n_array_nth(pkgs, j);
            if (n_array_bsearch(resolved_pkgs, pkg)) {
                logn(LOGNOTICE, "%s: removed cap due to %s is marked",
                     cap, pkg_snprintf_s0(pkg));
                n_hash_remove(aps->resolved_caps, cap);
                break;
            }
        }
    }
    n_array_free(keys);
    return n_hash_size(aps->resolved_caps);
}


tn_array *resolve_resolved_caps(tn_array *topkgs, struct arg_packages *aps) 
{
    struct pset_virtual_ent *ent;
    tn_array *keys = n_hash_keys(aps->resolved_caps);
    int i;
    
    for (i=0; i < n_array_size(keys); i++) {
        tn_array *pkgs;
        struct pkg *pkg = NULL;
        char *key = n_array_nth(keys, i);
        
        pkgs = n_hash_get(aps->resolved_caps, key);

        ent = n_hash_get(aps->pset_virtuals, key);
        if (ent && ent->mask) {
            int j;
            for (j=0; j < n_array_size(pkgs); j++) {
                struct pkg *p = n_array_nth(pkgs, j);
                if (strcmp(p->name, ent->mask) == 0 ||
                    fnmatch(pkg_id(p), ent->mask, 0) == 0) {
                    pkg = n_array_nth(pkgs, j);
                    break;
                }
            }
        }

        if (pkg == NULL) {      /* no default or default not found */
            int j, ndragged_min = INT_MAX - 1;
            for (j=0; j < n_array_size(pkgs); j++) {
                struct pkg *p = n_array_nth(pkgs, j);
                int ndragged = pkgmark_pkg_drags(p, NULL, 2);
                
                DBGF("- %s %d\n", pkg_snprintf_s0(p), ndragged);
                if (ndragged < ndragged_min) {
                    ndragged_min = ndragged;
                    pkg = p;
                }
            }
        }
        
        if (poldek_VERBOSE > 2) 
            msgn(1, "%s: choosen %s among %d packages", key,
                 pkg_snprintf_s(pkg), n_array_size(pkgs));
        
        n_array_push(topkgs, pkg_link(pkg));
    }
    
    n_array_free(keys);
    return topkgs;
}


int arg_packages_resolve(struct arg_packages *aps, tn_array *avpkgs,
                         struct pkgset *ps, unsigned flags)
{
    int i, j, nmasks, rc = 0;

    n_hash_clean(aps->resolved_caps);
    n_array_clean(aps->resolved_pkgs);
    
    nmasks = n_array_size(aps->package_masks);
    for (i=0; i < nmasks; i++) {
        char *mask = n_array_nth(aps->package_masks, i);
        int len = strlen(mask);
        
        if (len > 1 && mask[len - 1] == '-')
            mask[len - 1] = '\0';

        DBGF("mask %s\n", mask);
        if (*mask == '*' && *(mask + 1) == '\0') {
            for (j=0; j < n_array_size(avpkgs); j++)
                n_array_push(aps->resolved_pkgs,
                             pkg_link(n_array_nth(avpkgs, j)));
            return n_array_size(aps->resolved_pkgs);
        }
    }
    
    rc = resolve_pkgs(aps->resolved_pkgs, aps, avpkgs, flags);
    if (rc)                     /* continue with masks */
        rc = resolve_masks(aps->resolved_pkgs, aps, avpkgs, ps, flags);
    
    if (rc && ps && aps->pset_virtuals)
        rc = resolve_pset_virtuals(aps, ps, flags);

    if (!rc) {
        n_array_clean(aps->resolved_pkgs);
        n_hash_clean(aps->resolved_caps);
        
    } else {
        n_array_sort(aps->resolved_pkgs);
        n_array_uniq(aps->resolved_pkgs);
        
        if (flags & ARG_PACKAGES_RESOLV_UNAMBIGUOUS)
            n_array_uniq_ex(aps->resolved_pkgs, (tn_fn_cmp)pkg_cmp_uniq_name);
    }
    
    if (n_array_size(aps->resolved_pkgs))
        verify_pakcage_caps(aps, aps->resolved_pkgs);

    if (flags & ARG_PACKAGES_RESOLV_CAPSINLINE) {
        resolve_resolved_caps(aps->resolved_pkgs, aps);
        n_array_sort(aps->resolved_pkgs);
        n_array_uniq(aps->resolved_pkgs);
        if (flags & ARG_PACKAGES_RESOLV_UNAMBIGUOUS)
            n_array_uniq_ex(aps->resolved_pkgs, (tn_fn_cmp)pkg_cmp_uniq_name);
    }
    
    DBGF("ret %d pkgs\n",
         aps->resolved_pkgs ? n_array_size(aps->resolved_pkgs) : 0);
    return n_array_size(aps->resolved_pkgs) + n_hash_size(aps->resolved_caps);
}


tn_hash *arg_packages_get_resolved_caps(struct arg_packages *aps)
{
    return n_ref(aps->resolved_caps);
}

tn_array *arg_packages_get_resolved(struct arg_packages *aps)
{
    return n_array_dup(aps->resolved_pkgs, (tn_fn_dup)pkg_link);
}
