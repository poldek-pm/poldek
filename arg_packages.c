/* 
   Copyright (C) 2000 - 2002 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>


#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>
#include <trurl/nmalloc.h>
#include <trurl/n_snprintf.h>
#include <vfile/vfile.h>

#include "i18n.h"
#include "log.h"
#include "arg_packages.h"
#include "misc.h"
#include "pkgmisc.h"
#include "rpm/rpm_pkg_ld.h"

#define ARG_PACKAGES_SETUPDONE (1 << 0)

struct arg_packages {
    unsigned  flags;
    tn_array  *package_masks;   /* [@]foo(#|-)[VERSION[-RELEASE]] || foo.rpm   */
    tn_array  *package_lists;   /* --pset FILE */
};

static 
char *prepare_pkgmask(const char *maskstr, const char *fpath, int nline)
{
    char               *p, *s[1024], *buf, mask[1024];
    const char         **tl, **tl_save;
    const char         *evrstr = NULL, *name = NULL, *virtname = NULL;
    const char         *version = NULL, *release = NULL;
    int32_t            epoch = 0;
    int                is_virtual;
    

    
    n_strdupap(maskstr, &buf);
    
    s[0] = NULL;
    
    p = strip(buf);
        
    if (*p == '\0' || *p == '#')
        return NULL;

    is_virtual = 0;
    while (*p && !isalnum(*p)) {
        switch (*p) {
            case '~':
            case '!':           /* for backward compatybility */
                break;
                
            case  '@':          /* optional */
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
        return NULL;
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
        
    DBGF("virtname = %s, name = %s, evrstr = %s, %d\n",
         virtname, name, evrstr, tflags);
    
    if (evrstr) 
        parse_evr((char*)evrstr, &epoch, &version, &release);
        
                
    if (virtname) {
        n_snprintf(mask, sizeof(mask), "%s", virtname);
        
    } else {
        int n;
       
        n = n_snprintf(mask, sizeof(mask), name);
        if (version == NULL) {
            n_snprintf(&mask[n], sizeof(mask) - n, "-*");
           
        } else {
            n += n_snprintf(&mask[n], sizeof(mask) - n, "-%s", version);
            if (release)
                n_snprintf(&mask[n], sizeof(mask) - n, "-%s", release);
            else 
                n_snprintf(&mask[n], sizeof(mask) - n, "-*");
        }
    }
    
       
    n_str_tokl_free(tl_save);
    return n_strdup(mask);
}



struct arg_packages *arg_packages_new(void) 
{
    struct arg_packages *aps;

    aps = n_malloc(sizeof(*aps));
    aps->flags = 0;
    aps->package_masks = n_array_new(16, free, (tn_fn_cmp)strcmp);
    aps->package_lists = n_array_new(16, free, (tn_fn_cmp)strcmp);
    return aps;
}

void arg_packages_free(struct arg_packages *aps) 
{
    n_array_free(aps->package_masks);
    n_array_free(aps->package_lists);
    free(aps);
}

void arg_packages_clean(struct arg_packages *aps) 
{
    n_array_clean(aps->package_masks);
    n_array_clean(aps->package_lists);
    aps->flags = 0;
}


int arg_packages_size(struct arg_packages *aps) 
{
    return n_array_size(aps->package_masks);
}

tn_array *arg_packages_get_pkgmasks(struct arg_packages *aps) 
{
    return aps->package_masks;
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
    for (i=0; i<n_array_size(masks); i++)
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


int arg_packages_add_pkgfile(struct arg_packages *aps, const char *path)
{
    int rc = 1;
    
    if (!is_package_file(path))  
        rc = arg_packages_add_pkgmask(aps, path);
    
    else {
        struct pkg *pkg;
    

        if ((pkg = pkg_ldrpm(path, PKG_LDNEVR)) == NULL)
            return 0;

        arg_packages_add_pkgmask(aps, pkg_snprintf_s(pkg));
        pkg_free(pkg);
    }
    
    return rc;
}


int arg_packages_add_pkg(struct arg_packages *aps, const struct pkg *pkg)
{
    return arg_packages_add_pkgmask(aps, pkg_snprintf_s(pkg));
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
    while (fgets(buf, sizeof(buf), vf->vf_stream)) {
        char *mask;
        nline++;

        mask = prepare_pkgmask(strip(buf), fpath, nline);
        if (mask) 
            arg_packages_add_pkgmask(aps, mask);
    }
    
    vfile_close(vf);
    return rc;
}

int arg_packages_setup(struct arg_packages *aps)
{
    int i, rc = 1, n;

    if (aps->flags & ARG_PACKAGES_SETUPDONE)
        return 1;
        
    for (i=0; i < n_array_size(aps->package_lists); i++) {
        char *path = n_array_nth(aps->package_lists, i);
        
        if (!arg_packages_load_list(aps, path))
            rc = 0;
    }
    
    n = n_array_size(aps->package_masks);
    n_array_sort(aps->package_masks);
    n_array_uniq(aps->package_masks);

    if (n != n_array_size(aps->package_masks)) {
        msgn(1, _("Removed %d duplicates from given packages"),
             n - n_array_size(aps->package_masks));
        
    }

    aps->flags |= ARG_PACKAGES_SETUPDONE;
    return n;
}


tn_array *arg_packages_resolve(struct arg_packages *aps,
                               tn_array *avpkgs, unsigned flags)
{
    tn_array *pkgs = NULL;
    int i, j, nmasks;
    int *matches, *matches_bycmp;

    nmasks = n_array_size(aps->package_masks);
    
    for (i=0; i < nmasks; i++) {
        char  *mask = n_array_nth(aps->package_masks, i);
        
        if (*mask == '*' && *(mask + 1) == '\0')
            return n_ref(avpkgs);
    }

    matches = alloca(nmasks * sizeof(*matches));
    memset(matches, 0, nmasks * sizeof(*matches));

    matches_bycmp = alloca(nmasks * sizeof(*matches_bycmp));
    memset(matches_bycmp, 0, nmasks * sizeof(*matches_bycmp));
    

    pkgs = pkgs_array_new(nmasks * 6);
    
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
                
            } else if (fnmatch(mask, pkg->nvr, 0) == 0) {
                n_array_push(pkgs, pkg_link(pkg));
                matches[j]++;
                
            } else if (pkg->caps) {
                int ii;
                for (ii=0; ii < n_array_size(pkg->caps); ii++) {
                    // DUPA TODO
                }
            }
            
        }
    }
    
    
    for (j=0; j < n_array_size(aps->package_masks); j++) {
        const char *mask = n_array_nth(aps->package_masks, j);
        
        if (matches[j] == 0 && (flags & ARG_PACKAGES_RESOLV_MISSINGOK) == 0) {
            logn(LOGERR, _("%s: no such package"), mask);
            n_array_clean(pkgs);
        }

        if ((flags & ARG_PACKAGES_RESOLV_UNAMBIGUOUS) == 0 && matches_bycmp[j] > 1) {
            int pri = (flags & ARG_PACKAGES_RESOLV_EXACT) ? LOGERR : LOGWARN;
            logn(pri, _("%s: ambiguous name"), mask);
            if (flags & ARG_PACKAGES_RESOLV_EXACT)
                n_array_clean(pkgs);
        }
    }

    
    n_array_sort(pkgs);
    n_array_uniq(pkgs);
    
    if (flags & ARG_PACKAGES_RESOLV_UNAMBIGUOUS)
        n_array_uniq_ex(pkgs, (tn_fn_cmp)pkg_cmp_name_uniq);
    
    if (n_array_size(pkgs) == 0) {
        n_array_free(pkgs);
        pkgs = NULL;
    }

    return pkgs;
}
