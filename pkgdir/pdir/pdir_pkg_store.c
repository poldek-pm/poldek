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
#include "pkgu.h"
#include "pkgfl.h"
#include "capreq.h"
#include "pkgroup.h"

static
int pkg_store_caps(const struct pkg *pkg, tn_buf *nbuf) 
{
    tn_array *arr;
    int i;

    arr = n_array_new(n_array_size(pkg->caps), NULL, NULL);
    
    for (i=0; i < n_array_size(pkg->caps); i++) {
        struct capreq *cr = n_array_nth(pkg->caps, i);
        if (pkg_eq_capreq(pkg, cr))
            continue;

        if (capreq_is_bastard(cr))
            continue;
        
        n_array_push(arr, cr);
    }

    if (n_array_size(arr)) {
        n_buf_addstr(nbuf, "P:\n");
        capreq_arr_store(arr, nbuf);
    }
    
    n_array_free(arr);
    return 1;
}

static 
void store_pkg_fields_v0_17(tn_buf *nbuf, uint32_t size, uint32_t fsize,
                            uint32_t btime, uint32_t groupid) 
{
    uint8_t n = 4;

    n_buf_add_int8(nbuf, n);
    n_buf_add_int32(nbuf, size);
    
    n_buf_add_int32(nbuf, fsize);
    n_buf_add_int32(nbuf, btime);
    n_buf_add_int32(nbuf, groupid);
    n_buf_printf(nbuf, "\n");
}

static
void pkg_store_fl(const struct pkg *pkg, tn_buf *nbuf, tn_array *depdirs) 
{
    struct pkgflist *flist;

    flist = pkg_info_get_flist(pkg);
    if (flist && n_tuple_size(flist->fl) == 0) {
        pkg_info_free_flist(flist);
        return;
    }

    pkgfl_array_store_order(flist->fl);
        
    if (depdirs == NULL) {
        n_buf_printf(nbuf, "l:\n");
        pkgfl_store(flist->fl, nbuf, NULL, depdirs, PKGFL_ALL);
        
    } else {
        n_buf_printf(nbuf, "L:\n");
        pkgfl_store(flist->fl, nbuf, NULL, depdirs, PKGFL_DEPDIRS);
    
        n_buf_printf(nbuf, "l:\n");
        pkgfl_store(flist->fl, nbuf, NULL, depdirs, PKGFL_NOTDEPDIRS);
    }
    
    pkg_info_free_flist(flist);
}

static
int do_pkg_store(const struct pkg *pkg, tn_buf *nbuf, tn_array *depdirs,
                 unsigned flags)
{

    n_buf_printf(nbuf, "N: %s\n", pkg->name);
    if (pkg->epoch)
        n_buf_printf(nbuf, "V: %d:%s-%s\n", pkg->epoch, pkg->ver, pkg->rel);
    else 
        n_buf_printf(nbuf, "V: %s-%s\n", pkg->ver, pkg->rel);
    
    if (pkg->arch)
        n_buf_printf(nbuf, "A: %s\n", pkg->arch);
    
    if (pkg->os)
        n_buf_printf(nbuf, "O: %s\n", pkg->os);

    if (pkg->fn)
        n_buf_printf(nbuf, "n: %s\n", pkg->fn);

    n_buf_printf(nbuf, "F:\n");
    store_pkg_fields_v0_17(nbuf, pkg->size, pkg->fsize, pkg->btime,
                           pkg->groupid);
        
    if (pkg->caps && n_array_size(pkg->caps))
        pkg_store_caps(pkg, nbuf);
    
    if (pkg->reqs && n_array_size(pkg->reqs)) {
        n_buf_puts(nbuf, "R:\n");
        capreq_arr_store(pkg->reqs, nbuf);
    }
    
    
    if (pkg->cnfls && n_array_size(pkg->cnfls)) {
        n_buf_puts(nbuf, "C:\n");
        capreq_arr_store(pkg->cnfls, nbuf);
    }
    
    //mem_info(-10, "before fl");
    pkg_store_fl(pkg, nbuf, depdirs);
    //mem_info(-10, "after fl");

    if ((flags & PKGSTORE_NODESC) == 0) {
        struct pkguinf *pkgu;
        
        //mem_info(-10, "before uinf");
        if ((pkgu = pkg_info(pkg))) {
            n_buf_printf(nbuf, "U:\n");
            pkguinf_store_rpmhdr(pkgu, nbuf);
            n_buf_printf(nbuf, "\n");
            pkguinf_free(pkgu);
            //mem_info(-10, "after uinf");
        }
    }
    n_buf_printf(nbuf, "\n");
    return n_buf_size(nbuf);
}


int pdir_pkg_store(const struct pkg *pkg, tn_stream *st, tn_array *depdirs,
                   unsigned flags)
{
    tn_buf *nbuf;
    int n = 0;
    
    nbuf = n_buf_new(1024 * 32);
    if (do_pkg_store(pkg, nbuf, depdirs, flags) > 0)
        n = n_stream_write(st, n_buf_ptr(nbuf), n_buf_size(nbuf));

    n_buf_free(nbuf);
    return n;
}
