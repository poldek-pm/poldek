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

#define PKGFIELD_TAG_SIZE   'S'
#define PKGFIELD_TAG_FSIZE  's'
#define PKGFIELD_TAG_BTIME  'b'
#define PKGFIELD_TAG_ITIME  'i'
#define PKGFIELD_TAG_GID    'g'
#define PKGFIELD_TAG_RECNO  'r'
static
void pkg_store_fields(tn_buf *nbuf, const struct pkg *pkg) 
{
    uint8_t n = 0;

    if (pkg->size) 
        n++;

    if (pkg->fsize) 
        n++;

    if (pkg->btime) 
        n++;

    if (pkg->itime) 
        n++;

    if (pkg->groupid) 
        n++;

    if (pkg->recno) 
        n++;

    n_buf_add_int8(nbuf, n);

    if (pkg->size) {
        n_buf_add_int8(nbuf, PKGFIELD_TAG_SIZE);
        n_buf_add_int32(nbuf, pkg->size);
    }

    if (pkg->fsize) {
        n_buf_add_int8(nbuf, PKGFIELD_TAG_FSIZE);
        n_buf_add_int32(nbuf, pkg->fsize);
    }
    
    if (pkg->btime) {
        n_buf_add_int8(nbuf, PKGFIELD_TAG_BTIME);
        n_buf_add_int32(nbuf, pkg->btime);
    }

    if (pkg->itime) {
        n_buf_add_int8(nbuf, PKGFIELD_TAG_ITIME);
        n_buf_add_int32(nbuf, pkg->itime);
    }

    if (pkg->groupid) {
        n_buf_add_int8(nbuf, PKGFIELD_TAG_GID);
        n_buf_add_int32(nbuf, pkg->groupid);
    }

    if (pkg->recno) {
        n_buf_add_int8(nbuf, PKGFIELD_TAG_RECNO);
        n_buf_add_int32(nbuf, pkg->recno);
    }
    n_buf_printf(nbuf, "\n");
}


int pkg_restore_fields(tn_stream *st, struct pkg *pkg) 
{
    uint8_t n = 0, tag;

    n_stream_read_uint8(st, &n);
        
    while (n) {
        n_stream_read_uint8(st, &tag);
        switch (tag) {
            case PKGFIELD_TAG_SIZE:
                n_stream_read_uint32(st, &pkg->size);
                break;
                
            case PKGFIELD_TAG_FSIZE:
                n_stream_read_uint32(st, &pkg->fsize);
                break;
                
            case PKGFIELD_TAG_BTIME:
                n_stream_read_uint32(st, &pkg->btime);
                break;

            case PKGFIELD_TAG_ITIME:
                n_stream_read_uint32(st, &pkg->itime);
                break;

            case PKGFIELD_TAG_GID:
                n_stream_read_uint32(st, &pkg->groupid);
                break;
                
            case PKGFIELD_TAG_RECNO:
                n_stream_read_uint32(st, &pkg->recno);
                break;
        }
        n--;
    }
    
    return n_stream_read_uint8(st, &n); /* '\n' */
}



static
void pkg_store_fl(const struct pkg *pkg, tn_buf *nbuf, tn_array *depdirs) 
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
        n_buf_printf(nbuf, "l:\n");
        pkgfl_store_buf(fl, nbuf, depdirs, PKGFL_ALL);
        
    } else {
        n_buf_printf(nbuf, "L:\n");
        pkgfl_store_buf(fl, nbuf, depdirs, PKGFL_DEPDIRS);
    
        n_buf_printf(nbuf, "l:\n");
        pkgfl_store_buf(fl, nbuf, depdirs, PKGFL_NOTDEPDIRS);
    }
    
    pkg_info_free_fl(pkg, fl);
    pkgflmodule_allocator_pop_mark(flmark);
}


int pkg_store(const struct pkg *pkg, tn_buf *nbuf, tn_array *depdirs,
              unsigned flags)
{

    //printf("ST %s\n", pkg_snprintf_s(pkg));
    if ((flags & PKGDIR_CREAT_PKG_NOEVR) == 0) {
        n_buf_printf(nbuf, "N: %s\n", pkg->name);
        if (pkg->epoch)
            n_buf_printf(nbuf, "V: %d:%s-%s\n", pkg->epoch, pkg->ver, pkg->rel);
        else 
            n_buf_printf(nbuf, "V: %s-%s\n", pkg->ver, pkg->rel);
    }
    
    if ((flags & PKGDIR_CREAT_PKG_NOARCH) == 0 && pkg->arch)
        n_buf_printf(nbuf, "A: %s\n", pkg->arch);
    
    if ((flags & PKGDIR_CREAT_PKG_NOOS) == 0 && pkg->os)
        n_buf_printf(nbuf, "O: %s\n", pkg->os);
    
    
    
    if (flags & PKGDIR_CREAT_PKG_Fv017) {
        n_buf_printf(nbuf, "F:\n");
        store_pkg_fields_v0_17(nbuf, pkg->size, pkg->fsize, pkg->btime, pkg->groupid);
        
    } else {
        n_buf_printf(nbuf, "f:\n");
        pkg_store_fields(nbuf, pkg);
    }
    
    
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


int pkg_store_st(const struct pkg *pkg, tn_stream *st, tn_array *depdirs,
                 unsigned flags)
{
    tn_buf *nbuf;
    int n = 0;
    
    nbuf = n_buf_new(1024 * 8);
    if (pkg_store(pkg, nbuf, depdirs, flags) > 0)
        n = n_stream_write(st, n_buf_ptr(nbuf), n_buf_size(nbuf));

    n_buf_free(nbuf);
    return n;
}
