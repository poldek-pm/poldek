/* 
  Copyright (C) 2000 - 2008 Pawel A. Gajda (mis@pld-linux.org)
 
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

#include <stdlib.h>
#include <string.h>

#include <trurl/nassert.h>
#include <trurl/nmalloc.h>

#include "i18n.h"
#include "pkg.h"
#include "capreqidx.h"
#include "capreq.h"
#include "log.h"

static void capreq_ent_free(struct capreq_idx_ent *ent) 
{
    DBGF("ent %p, %p %d, %d\n", ent, ent->crent_pkgs, ent->_size, ent->items);
    if (ent->_size > 1)
        free(ent->crent_pkgs);
}

int capreq_idx_init(struct capreq_idx *idx, unsigned type, int nelem)  
{
    idx->flags = type;

    MEMINF("START");
    idx->na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    idx->ht = n_hash_new_na(idx->na, nelem, (tn_fn_free)capreq_ent_free);
    n_hash_ctl(idx->ht, TN_HASH_NOCPKEY);
    MEMINF("END");
    return 1;
}


void capreq_idx_destroy(struct capreq_idx *idx) 
{
    n_hash_free(idx->ht);
    n_alloc_free(idx->na);
    memset(idx, 0, sizeof(*idx));
}

static int capreq_idx_ent_transform_to_array(struct capreq_idx_ent *ent)
{
    struct pkg *tmp;
    
    n_assert(ent->_size == 1);   /* crent_pkgs is NOT allocated */
    tmp = ent->crent_pkg;
    ent->crent_pkgs = n_malloc(2 * sizeof(*ent->crent_pkgs));
    ent->crent_pkgs[0] = tmp;
    ent->_size = 2;
    return 1;
}



int capreq_idx_add(struct capreq_idx *idx, const char *capname,
                   struct pkg *pkg)
{
    struct capreq_idx_ent *ent;
    unsigned khash = 0;
    int klen = 0;
    
    if ((ent = n_hash_get_ex(idx->ht, capname, &klen, &khash)) == NULL) {
        const char *hcapname = capreq__alloc_name(capname);
        
        ent = idx->na->na_malloc(idx->na, sizeof(*ent));
        ent->_size = 1;
        ent->items = 1;
        ent->crent_pkg = pkg;
        
        n_hash_insert_ex(idx->ht, hcapname, klen, khash, ent);
#if ENABLE_TRACE        
        if ((n_hash_size(idx->ht) % 1000) == 0)
            n_hash_stats(idx->ht);
#endif        
        
    } else {
        if (ent->_size == 1)    /* crent_pkgs is NOT allocated */
            capreq_idx_ent_transform_to_array(ent);

        /*
         * Sometimes, there are duplicates, especially in dotnet-* packages
         * which provides multiple versions of one cap. For example dotnet-mono-zeroconf
         * provides: mono(Mono.Zeroconf) = 1.0.0.0, mono(Mono.Zeroconf) = 2.0.0.0, etc.
         */
        if (idx->flags & CAPREQ_IDX_CAP) { /* check for duplicates */
            register int i;
            for (i=0; i < ent->items; i++) { 
                if (pkg == ent->crent_pkgs[i])
                    return 1;
            }
        }
        
        if (ent->items == ent->_size) {
            ent->_size *= 2;
            ent->crent_pkgs = n_realloc(ent->crent_pkgs,
                                        ent->_size * sizeof(*ent->crent_pkgs));
        }
        
        ent->crent_pkgs[ent->items++] = pkg;
    }
    
    return 1;
}


void capreq_idx_remove(struct capreq_idx *idx, const char *capname,
                       struct pkg *pkg)
{
    struct capreq_idx_ent *ent;
    int i;
            
    if ((ent = n_hash_get(idx->ht, capname)) == NULL)
        return;

    if (ent->_size == 1) {      /* no crent_pkgs */
        if (pkg_cmp_name_evr(pkg, ent->crent_pkg) == 0) {
            ent->items = 0;
            ent->crent_pkg = NULL;
        }
        return;
    }
    
    for (i=0; i < ent->items; i++) {
        if (pkg_cmp_name_evr(pkg, ent->crent_pkgs[i]) == 0) {
            if (i == ent->items - 1) 
                ent->crent_pkgs[i] = NULL;
            else 
                memmove(&ent->crent_pkgs[i], &ent->crent_pkgs[i + 1],
                        (ent->_size - 1 - i) * sizeof(*ent->crent_pkgs));
            ent->crent_pkgs[ent->_size - 1] = NULL;
            ent->items--;
        }
    }
}


void capreq_idx_stats(const char *prefix, struct capreq_idx *idx) 
{
    tn_array *keys = n_hash_keys(idx->ht);
    int i, stats[100000];

    memset(stats, 0, sizeof(stats));
    
    for (i=0; i < n_array_size(keys); i++) {
        struct capreq_idx_ent *ent;
        ent = n_hash_get(idx->ht, n_array_nth(keys, i));
        stats[ent->items]++;
    }
    n_array_free(keys);
    printf("CAPREQ_IDX %s %d\n", prefix, n_hash_size(idx->ht));
    
    for (i=0; i < 100000; i++) {
        if (stats[i])
            printf("%s: %d: %d\n", prefix, i, stats[i]);
    }
}


const
struct capreq_idx_ent *capreq_idx_lookup(struct capreq_idx *idx,
                                         const char *capname)
{
    struct capreq_idx_ent *ent;
    
    if ((ent = n_hash_get(idx->ht, capname)) == NULL)
        return NULL;

    if (ent->items == 0)
        return NULL;
    
    if (ent->_size == 1)        /* return only transformed ents */
        capreq_idx_ent_transform_to_array(ent);
    
    return ent;
}




