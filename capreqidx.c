/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
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
#include "capreqidx.h"
#include "log.h"

static void free_ptrs(void *p) 
{
    free(*(void**)p);
}

int capreq_idx_init(struct capreq_idx *idx, unsigned type, int nelem)  
{
    idx->flags = type;

    MEMINF("START");
    
    idx->ht = n_hash_new(nelem, free_ptrs);

    if (idx->ht == NULL)
       return 0;
    
    n_hash_ctl(idx->ht, TN_HASH_NOCPKEY);
    idx->na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    MEMINF("END");
    return 1;
}


void capreq_idx_destroy(struct capreq_idx *idx) 
{
    n_hash_free(idx->ht);
    n_alloc_free(idx->na);
    memset(idx, 0, sizeof(*idx));
}


int capreq_idx_add(struct capreq_idx *idx, const char *capname,
                   struct pkg *pkg, int isprov)
{
    void **p;
    struct capreq_idx_ent *ent;
            
    if ((p = n_hash_get(idx->ht, capname)) == NULL) {
        register int size = 1;
        
        if ((idx->flags & CAPREQ_IDX_CAP) == 0)
            size = 4;
        
        ent = n_malloc(sizeof(*ent) + (size * sizeof(void*)));
        ent->_size = size;

        ent->items = 1;
        ent->pkgs[0] = pkg;
        p = idx->na->na_malloc(idx->na, sizeof(ent));
        *p = ent;
        n_hash_insert(idx->ht, capname, p);
#if ENABLE_TRACE        
        if ((n_hash_size(idx->ht) % 1000) == 0)
            n_hash_stats(idx->ht);
#endif        
        
    } else {
        register int i;
        ent = *p;

#ifndef HAVE_RPM_4_0            /* rpm 4.x packages has NAME = E:V-R cap */
        if (isprov) {
            for (i=0; i<ent->items; i++) 
                if (ent->pkgs[i] == pkg) {
                    logn(LOGWARN, _("%s: redundant capability \"%s\""),
                        pkg_snprintf_s(pkg), capname);
                    return 1;
                }
        }
#else
        i = i;
        isprov = isprov;        /* avoid gcc's warn */
#endif
        if (idx->flags & CAPREQ_IDX_CAP) { /* check for duplicates */
            //printf("%s: %d: %s\n", capname, ent->items, pkg_snprintf_s(pkg));
            for (i=0; i < ent->items; i++) { 
                if (pkg == ent->pkgs[i])
                    return 1;
            }
        }
        
        
        if (ent->items == ent->_size) {
            struct capreq_idx_ent *new_ent;
            int new_size;
            new_size = sizeof(*ent) + (2 * ent->_size * sizeof(void*));
            new_ent = n_realloc(ent, new_size);
            ent = new_ent;
            ent->_size *= 2;
            *p = ent;
        }
        ent->pkgs[ent->items++] = pkg;
    }
    
    return 1;
}

void capreq_idx_remove(struct capreq_idx *idx, const char *capname,
                       struct pkg *pkg)
{
    struct capreq_idx_ent *ent;
    void **p;
    int i;
            
    if ((p = n_hash_get(idx->ht, capname)) == NULL)
        return;
    
    ent = *p;
    for (i=0; i < ent->items; i++) {
        if (pkg_cmp_name_evr(pkg, ent->pkgs[i]) == 0) {
            if (i == ent->items - 1) 
                ent->pkgs[i] = NULL;
            else 
                memmove(&ent->pkgs[i], &ent->pkgs[i + 1],
                        (ent->_size - 1 - i) * sizeof(*ent->pkgs));
            ent->pkgs[ent->_size - 1] = NULL;
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
        void **p;
        p = n_hash_get(idx->ht, n_array_nth(keys, i));
        ent = *p;
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
    void **p;
    
    if ((p = n_hash_get(idx->ht, capname)) == NULL)
        return NULL;

    return *p;
}




