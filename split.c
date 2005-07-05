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

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include <sys/param.h>          /* for PATH_MAX */

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nmalloc.h>

#include <vfile/vfile.h>

#include "i18n.h"
#include "pkgset.h"
#include "pkgset-req.h"
#include "log.h"
#include "pkg.h"
#include "misc.h"
#include "poldek.h"
#include "poldek_intern.h"

struct chunk {
    int       no;
    unsigned  size;
    unsigned  maxsize;
    int       items;
    tn_array  *pkgs;
};

struct pridef {
    int         pri;
    char        mask[0];
};

static struct chunk *chunk_new(int no, int maxsize)
{
    struct chunk *chunk;

    chunk = n_malloc(sizeof(*chunk));
    chunk->no = no;
    chunk->size = chunk->items = 0;
    chunk->maxsize = maxsize;
    chunk->pkgs = n_array_new(512, (tn_fn_free)pkg_free, NULL);
    return chunk;
}

static void chunk_free(struct chunk *chunk)
{
    n_array_free(chunk->pkgs);
    chunk->pkgs = NULL;
    free(chunk);
}


static void chunk_dump(struct chunk *chunk, FILE *stream) 
{
    int i;

    if (poldek_VERBOSE > 1) {
        n_array_sort_ex(chunk->pkgs, (tn_fn_cmp)pkg_cmp_pri_name_evr_rev);
        for (i=0; i < n_array_size(chunk->pkgs); i++) {
            struct pkg *pkg = n_array_nth(chunk->pkgs, i);
            msgn(2, "[#%d] [%d] %s", chunk->no, pkg->pri, pkg_snprintf_s(pkg));
        }
    }

    n_array_sort_ex(chunk->pkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev);
    for (i=0; i < n_array_size(chunk->pkgs); i++) {
        struct pkg *pkg = n_array_nth(chunk->pkgs, i);
        fprintf(stream, "%s\n", pkg_filename_s(pkg));
        //fprintf(stream, "%s\n", pkg->name);
    }
}

static 
int pridef_cmp_pri(struct pridef *pridef1, struct pridef *pridef2)
{
    return pridef1->pri - pridef2->pri;
}

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
                logn(LOGERR, _("%s:%d: syntax error near %s"), fpath, nline, p);
                return -1;
            }
        }
    }
    
    if (mask) {
        n = strlen(mask) + 1;
        *pridef = n_malloc(sizeof(**pridef) + n);
        memcpy((*pridef)->mask, mask, n);
        (*pridef)->pri = pri;
        DBGF("mask = %s, pri = %d\n", mask, pri);
    }
    
    return 1;
}

static 
tn_array *load_pri_conf(const char *fpath)
{
    char              buf[1024];
    struct vfile      *vf;
    int               nline, rc = 1;
    tn_array          *defs;
    
    if ((vf = vfile_open(fpath, VFT_TRURLIO, VFM_RO)) == NULL) 
        return 0;

    nline = 0;
    defs = n_array_new(64, free, (tn_fn_cmp)pridef_cmp_pri);

    while (n_stream_gets(vf->vf_tnstream, buf, sizeof(buf) - 1)) {
        struct pridef *pd = NULL;
        nline++;

        if (read_pridef(buf, strlen(buf), &pd, fpath, nline) == -1) {
            logn(LOGERR, _("%s: give up at %d"), fpath, nline);
            rc = 0;
            break;
        } 

        if (pd)
            n_array_push(defs, pd);
    }
    
    vfile_close(vf);

    if (rc) 
        n_array_sort(defs);
    
    else {
        n_array_free(defs);
        defs = NULL;
    }
    
    return defs;
}

static
void set_pri(int deep, struct pkg *pkg, int pri) 
{
    int i;

    if (pkg->pri == pri)
        return;

    if (pkg->pri != 0) {
        if (pri > 0 && pkg->pri < 0) { /* priorities < 0 are stronger */
            msg_i(3, deep, "skip pri %d %s [%d]\n", pri, pkg_snprintf_s(pkg),
                  pkg->pri);
            return;
        }

        if (pri < 0 && pkg->pri < pri) { /* higher priorities are sticky */
            msg_i(3, deep, "skip pri %d %s [%d]\n", pri, pkg_snprintf_s(pkg),
                  pkg->pri);
            return;
        }
    }
    
    
    pkg->pri = pri;
    
    msg_i(3, deep, "pri %d %s\n", pri, pkg_snprintf_s(pkg));
    deep += 2;
    
    if (pri > 0 && pkg->revreqpkgs) {
        for (i=0; i<n_array_size(pkg->revreqpkgs); i++) {
            struct pkg *revpkg = n_array_nth(pkg->revreqpkgs, i);
            set_pri(deep, revpkg, pri);
        }
        
    } else if (pri < 0 && pkg->reqpkgs) {
        for (i=0; i<n_array_size(pkg->reqpkgs); i++) {
            struct reqpkg *reqpkg = n_array_nth(pkg->reqpkgs, i);
            if (reqpkg->pkg->pri > pri)
                set_pri(deep, reqpkg->pkg, pri);
        }
    }
}


static void mapfn_clean_pkg_color(struct pkg *pkg) 
{
    pkg_set_color(pkg, PKG_COLOR_WHITE);
}


static
int try_package(int deep, unsigned *chunk_size, unsigned maxsize,
                struct pkg *pkg, tn_array *stack) 
{
    int i, rc = 1;

    if (!pkg_is_color(pkg, PKG_COLOR_WHITE))
        return 1;
    
    n_assert(stack != NULL);

    pkg_set_color(pkg, PKG_COLOR_BLACK); /* visited */

    n_array_push(stack, pkg_link(pkg));
    *chunk_size += pkg->fsize;
    
    DBGF("trying %s: %d (%d) > %d\n", pkg_snprintf_s(pkg), *chunk_size,
         pkg->fsize, maxsize);
    
    if (*chunk_size > maxsize)
        return 0;
    
    if (pkg->reqpkgs == NULL)
        return 1;
    
    for (i=0; i<n_array_size(pkg->reqpkgs); i++) {
        struct reqpkg *reqpkg = n_array_nth(pkg->reqpkgs, i);
        if (!try_package(deep + 2, chunk_size, maxsize, reqpkg->pkg, stack)) {
            rc = 0;
            break;
        }
    }
    
    return rc;
}


static
int chunk_add(struct chunk *chunk, struct pkg *pkg) 
{
    int i, rc = 0;
    int chunk_size = 0;
    tn_array *stack = NULL;

    
    if (!pkg_is_color(pkg, PKG_COLOR_WHITE))
        return 1;

    DBGF("to #%d %s\n", chunk->no, pkg_snprintf_s(pkg));
    
    
    stack = n_array_new(16, (tn_fn_free)pkg_free, NULL);
    
    if (chunk->size + pkg->fsize > chunk->maxsize)
        return 0;

    chunk_size = chunk->size;
    
    if (try_package(0, &chunk_size, chunk->maxsize, pkg, stack)) {
        chunk->items += n_array_size(stack);
        chunk->size = chunk_size;

        while (n_array_size(stack) > 0)
            n_array_push(chunk->pkgs, n_array_pop(stack));
        
        rc = 1;
        
    } else {
        for (i=0; i<n_array_size(stack); i++) {
            struct pkg *pkg = n_array_nth(stack, i);
            pkg_set_color(pkg, PKG_COLOR_WHITE);
            msgn(3, "%s: rollback", pkg_snprintf_s(pkg));
        }
        rc = 0;
    }

    n_array_free(stack);
    return rc;
}


static
int make_chunks(tn_array *pkgs, unsigned split_size, unsigned first_free_space,
                const char *outprefix)
{
    int             i, chunk_no = 0, rc = 1;
    tn_array        *chunks;
    struct chunk    *chunk;

    chunks = n_array_new(16, (tn_fn_free)chunk_free, NULL);
    chunk = chunk_new(0, split_size - first_free_space);
    n_array_push(chunks, chunk);

    n_array_map(pkgs, (tn_fn_map1)mapfn_clean_pkg_color);
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        if (!pkg_is_color(pkg, PKG_COLOR_WHITE))
            continue;
        
        if (!chunk_add(chunk, pkg)) {
            if (n_array_size(chunk->pkgs) == 0) {
                logn(LOGERR, _("split failed: packages size is "
                               "greater than chunk size"));
                rc = 0;
                goto l_end;
            }
            
            chunk_no++;
            chunk = chunk_new(chunk_no, split_size);
            n_array_push(chunks, chunk);
            i = 0;
        }
    }
    
    for (i=0; i < n_array_size(chunks); i++) {
        struct vfile   *vf;
        char           path[PATH_MAX], strsize[128];
        struct chunk   *chunk;
        struct pkg     *pkg;
        int            pri_max, pri_min;
        
        
        chunk = n_array_nth(chunks, i);
        n_array_sort_ex(chunk->pkgs, (tn_fn_cmp)pkg_cmp_pri_name_evr_rev);

        pkg = n_array_nth(chunk->pkgs, 0);
        pri_min = pkg->pri;

        pkg = n_array_nth(chunk->pkgs, n_array_size(chunk->pkgs) - 1);
        pri_max = pkg->pri;
        
        snprintf(path, sizeof(path), "%s.%.2d", outprefix, chunk->no);
        snprintf_size(strsize, sizeof(strsize), chunk->size, 2, 0);
        msgn(0, _("Writing %s (%4d packages, %s, "
                  "pri min, max = %d, %d)"),
             path, chunk->items, strsize, pri_min, pri_max);
        
        
        if ((vf = vfile_open(path, VFT_STDIO, VFM_RW)) == NULL)
            return 0;

#if 0        
        fprintf(vf->vf_stream, "# chunk #%d: %d packages, %d bytes\n",
                i, chunk->items, chunk->size);
#endif
        chunk_dump(chunk, vf->vf_stream);
        vfile_close(vf);
    }

 l_end:
    
    return rc;
}


int packages_set_priorities(tn_array *pkgs, const char *priconf_path)
{
    tn_array *defs = NULL;
    int i, j, nmached = 0;

    if ((defs = load_pri_conf(priconf_path)) == NULL)
        return 0;

    if (n_array_size(defs) == 0) {
        logn(LOGWARN, _("%s: no priorities loaded"), priconf_path);
        n_array_free(defs);
        return 1;               /* not an error in fact */
    }

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        pkg->pri = 0;
    }
    
    n_array_sort(pkgs);
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        int pri = 0;

        for (j=0; j<n_array_size(defs); j++) {
            struct pridef *pd = n_array_nth(defs, j);
            
            if (fnmatch(pd->mask, pkg->name, 0) == 0) {
                pri = pd->pri;
                msgn(2, "split: assign %d pri to %s (mask %s)", pri,
                     pkg_id(pkg), pd->mask);
                nmached++;
                break;
            }
        }
        
        if (pri != 0)
            set_pri(0, pkg, pri);
    }
    
    if (nmached == 0)
        logn(LOGNOTICE, "split: %s",  _("no maching priorities"));
    
    n_array_free(defs);
    return 1;
}

static
int packages_split(const tn_array *pkgs, unsigned split_size,
                   unsigned first_free_space, const char *outprefix)
{
    tn_array *packages = NULL, *ordered_pkgs = NULL;
    int i, rc = 1;

    
    packages = n_array_dup(pkgs, (tn_fn_dup)pkg_link);
    // pre-sort packages with pkg_cmp_pri_name_evr_rev()
    n_array_sort_ex(packages, (tn_fn_cmp)pkg_cmp_pri_name_evr_rev);
    n_array_ctl_set_cmpfn(packages, (tn_fn_cmp)pkg_cmp_name_evr_rev);

    msg(2, "\nPackages ordered by priority:\n");
    for (i=0; i < n_array_size(packages); i++) {
        struct pkg *pkg = n_array_nth(packages, i);
        msg(2, "%d. [%d] %s\n", i, pkg->pri,  pkg_snprintf_s(pkg));
    }

    ordered_pkgs = NULL;
    packages_order(packages, &ordered_pkgs, PKGORDER_INSTALL);
    
    msg(2, "\nPackages ordered:\n");
    for (i=0; i < n_array_size(ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ordered_pkgs, i);
        msg(2, "%d. [%d] %s\n", i, pkg->pri,  pkg_snprintf_s(pkg));
    }

    rc = make_chunks(ordered_pkgs, split_size, first_free_space, outprefix);
    
    if (ordered_pkgs) 
        n_array_free(ordered_pkgs);
    
    if (packages)
        n_array_free(packages);
    
    return rc;
}

int poldek_split(const struct poldek_ctx *ctx, unsigned size,
                 unsigned first_free_space, const char *outprefix)
{
    if (outprefix == NULL)
        outprefix = "packages.chunk";
    
    if (n_array_size(ctx->ps->pkgs) == 0) {
        logn(LOGERR, "split: %s", _("no available packages found"));
        return 0;
    }

    return packages_split(ctx->ps->pkgs, size, first_free_space, outprefix);
}
