/* 
  Copyright (C) 2001 Pawel A. Gajda (mis@k2.net.pl)
 
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
#include <vfile/vfile.h>

#include "pkgset.h"
#include "pkgset-req.h"
#include "log.h"
#include "pkg.h"
#include "rpmadds.h"
#include "misc.h"

struct chunk {
    int       no;
    unsigned  size;
    int       items;
    FILE      *stream;
};

struct pridef {
    int         pri;
    char        mask[0];
};

static 
int read_pridef(char *buf, int buflen, struct pridef **pridef,
                const char *fpath, int nline)
{
    char           *p;
    char           *mask = NULL;
    int            n, pri = -1; /* default priority */
    

    n_assert(*pridef == NULL);
    n = buflen;
    
    while (n && isspace(buf[n - 1]))
        buf[--n] = '\0';
    
    p = buf;
    while(isspace(*p))
        p++;
        
    if (*p == '\0' || *p == '#')
        return 0;

    mask = p;
    
    while (*p && !isspace(*p)) 
        p++;

    if (*p) {
        *p = '\0';
        p++;
        
        while(isspace(*p))
            p++;

        if (*p) {
            if (sscanf(p, "%d", &pri) != 1) {
                log(LOGERR, "%s:%d: syntax error near %s\n", fpath, nline, p);
                return -1;
            }
        }
    }
    
    if (mask) {
        n = strlen(mask) + 1;
        *pridef = malloc(sizeof(**pridef) + n);
        memcpy((*pridef)->mask, mask, n);
        (*pridef)->pri = pri;
        DBGF("mask = %s, pri = %d\n", mask, pri);
    }
    
    return 1;
}

static 
tn_array *read_split_conf(const char *fpath)
{
    char              buf[1024];
    struct vfile      *vf;
    int               nline, rc = 1;
    tn_array          *defs;
    
    if ((vf = vfile_open(fpath, VFT_STDIO, VFM_RO)) == NULL) 
        return 0;

    nline = 0;
    defs = n_array_new(64, free, NULL);
    
    while (fgets(buf, sizeof(buf), vf->vf_stream)) {
        struct pridef *pd = NULL;
        
        nline++;

        if (read_pridef(buf, strlen(buf), &pd, fpath, nline) == -1) {
            log(LOGERR, "%s: give up at %d\n", fpath, nline);
            rc = 0;
            break;
        } 

        if (pd)
            n_array_push(defs, pd);
    }
    
    vfile_close(vf);
    
    if (rc == 0) {
        n_array_free(defs);
        defs = NULL;
    }
    
    return defs;
}

static
void set_pri(struct pkg *pkg, int pri, int deep, int verb) 
{
    int i;
    
    if (pkg->pri != 0 &&
        !(pkg->pri > 0 && pri < 0) &&
        !(pkg->pri > 0 && pri > pkg->pri))
        return;
        

    pkg->pri = pri;
    
    if (verb)
        msg_i(2, deep, "pri %d %s\n", pri, pkg_snprintf_s(pkg));
    deep += 2;
    
    if (pri > 0 && pkg->revreqpkgs) {
        for (i=0; i<n_array_size(pkg->revreqpkgs); i++) {
            struct pkg *revpkg = n_array_nth(pkg->revreqpkgs, i);
            set_pri(revpkg, pri, deep, verb);
        }
    }
}

static void mapfn_clean_pkg_color(struct pkg *pkg) 
{
    pkg_set_color(pkg, PKG_COLOR_WHITE);
}


static
void set_chunk(struct pkg *pkg, int chunk_no, int deep, int verb) 
{
    int i;
    
    if (pkg_is_color(pkg, PKG_COLOR_WHITE)) {
        if (verb && pkg->pri != chunk_no)
            msg_i(1, deep, "move %s to chunk #%d\n", pkg_snprintf_s(pkg), chunk_no);
        deep += 2;

        pkg->pri = chunk_no;
        pkg_set_color(pkg, PKG_COLOR_BLACK); /* visited */
        
        if (pkg->reqpkgs) {
            for (i=0; i<n_array_size(pkg->reqpkgs); i++) {
                struct reqpkg *reqpkg = n_array_nth(pkg->reqpkgs, i);
                if (reqpkg->pkg->pri > chunk_no) /* earlier chunk */
                    set_chunk(reqpkg->pkg, chunk_no, deep, verb);
            }
        }
    }
}


static void mapfn_chunk_size(struct pkg *pkg, void *arg) 
{
    struct chunk *chunk = arg;
    if (pkg->pri == chunk->no) {
        chunk->size += pkg->fsize;
        chunk->items++;
    }
    	
}

static void mapfn_chunk_dump(struct pkg *pkg, void *arg) 
{
    struct chunk *chunk = arg;
    if (pkg->pri == chunk->no)
        fprintf(chunk->stream, "%s\n", pkg_filename_s(pkg));
}


int make_chunks(tn_array *pkgs, unsigned split_size, unsigned first_free_space,
                const char *outprefix)
{
    int i;
    int chunk_no = 0, chunk_size = 0;
    unsigned size;
    
    size = split_size - first_free_space;
    
    for (i=0; i<n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        if (chunk_size + pkg->fsize > size) {
            chunk_no++;
            chunk_size = 0;
            size = split_size;
        }
        
        chunk_size += pkg->fsize;
        pkg->pri = chunk_no;    /* pkg->pri used as chunk_no */
    }
    chunk_no++;
    
    n_array_map(pkgs, (tn_fn_map1)mapfn_clean_pkg_color);
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        set_chunk(pkg, pkg->pri, 0, 1);
    }

    n_array_sort_ex(pkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev);
    for (i=0; i < chunk_no; i++) {
        struct chunk chunk;

        chunk.no = i;
        chunk.size = chunk.items = 0;
        n_array_map_arg(pkgs, (tn_fn_map2)mapfn_chunk_size, &chunk);
        if (chunk.size > split_size) {
            log(LOGERR, "Split failed, try to rearrange package priorities\n");
            return 0;
        }
    }

    for (i=0; i < chunk_no; i++) {
        struct chunk   chunk;
        struct vfile   *vf;
        char           path[PATH_MAX];
        
        chunk.no = i;
        chunk.size = chunk.items = 0;
        n_array_map_arg(pkgs, (tn_fn_map2)mapfn_chunk_size, &chunk);

        
        snprintf(path, sizeof(path), "%s.%d", outprefix, i);
        msg(0, "Writing %s (%4d packages, % 10d bytes)\n", path, chunk.items,
            chunk.size);
        
        if ((vf = vfile_open(path, VFT_STDIO, VFM_RW)) == NULL)
            return 0;

#if 0        
        fprintf(vf->vf_stream, "# chunk #%d: %d packages, %d bytes\n",
                i, chunk.items, chunk.size);
#endif
        chunk.stream = vf->vf_stream;
        n_array_map_arg(pkgs, (tn_fn_map2)mapfn_chunk_dump, &chunk);
        vfile_close(vf);
    }
    	
    return 1;
}


int packages_set_priorities(tn_array *pkgs, const char *splitconf_path)
{
    tn_array *defs = NULL;
    int i, j;


    if ((defs = read_split_conf(splitconf_path)) == NULL)
        return 0;
    
    n_array_sort(pkgs);
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        int pri = 0;

        for (j=0; j<n_array_size(defs); j++) {
            struct pridef *pd = n_array_nth(defs, j);
            
            if (fnmatch(pd->mask, pkg->name, 0) == 0) {
                pri = pd->pri;
                //msg(0, "matched %d %s %s\n", pri, pd->mask,
                //    pkg_snprintf_s(pkg));
                break;
            }
        }
        
        if (pri != 0)
            set_pri(pkg, pri, 1, verbose > 4);
    }
    
    n_array_free(defs);
    return 1;
}


int packages_split(tn_array *pkgs, unsigned split_size, unsigned first_free_space, 
                   const char *splitconf_path, const char *outprefix)
{
    tn_array *defs = NULL, *packages = NULL, *ordered_pkgs = NULL;
    int i, j, rc = 1;

    if (splitconf_path == NULL)
        defs = n_array_new(2, NULL, NULL);
    else 
        defs = read_split_conf(splitconf_path);

    if (defs == NULL)
        return 0;
    
    n_array_sort(pkgs);
    packages = n_array_new(n_array_size(pkgs), (tn_fn_free)pkg_free,
                           (tn_fn_cmp)pkg_cmp_pri);
    
    
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        int mached = 0, pri = 0;

        if (pkg->fsize == 0) {
            log(LOGERR, "version 0.4.1 of package index is required for spliting\n");
            rc = 0;
            goto l_end;
        }

        if (defs) 
            for (j=0; j<n_array_size(defs); j++) {
                struct pridef *pd = n_array_nth(defs, j);
                
                if (fnmatch(pd->mask, pkg->name, 0) == 0) {
                    mached = 1;
                    pri = pd->pri;
                    //msg(0, "mached %d %s %s\n", pri, pd->mask,
                    //    pkg_snprintf_s(pkg));
                    break;
                }
            }

        if (pri != 0)
            set_pri(pkg, pri, 1, verbose);
        n_array_push(packages, pkg_link(pkg));
    }
    
    if (defs) {
        n_array_free(defs);
        defs = NULL;
    }
    
    n_array_isort(packages);

    msg(2, "\nPackages ordered by priority:\n");
    for (i=0; i<n_array_size(packages); i++) {
        struct pkg *pkg = n_array_nth(packages, i);
        msg(2, "%d. [%d] %s\n", i, pkg->pri,  pkg_snprintf_s(pkg));
    }

    ordered_pkgs = NULL;
    packages_order(packages, &ordered_pkgs);

    rc = make_chunks(ordered_pkgs, split_size, first_free_space, outprefix);

    
 l_end:
    if (defs)
        n_array_free(defs);

    if (ordered_pkgs) 
        n_array_free(ordered_pkgs);
    
    if (packages)
        n_array_free(packages);
    
    return rc;
}
