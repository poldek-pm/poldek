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

#include "i18n.h"
#include "capreqidx.h"
#include "log.h"

#define obstack_chunk_alloc malloc
#define obstack_chunk_free  free

static void free_ptrs(void *p) 
{
    free(*(void**)p);
}


int capreq_idx_init(struct capreq_idx *idx, int nelem)  
{
    idx->ht = n_hash_new(nelem, free_ptrs);
    if (idx->ht == NULL)
       return 0;
    
    n_hash_ctl(idx->ht, TN_HASH_NOCPKEY);
    
    obstack_init(&idx->obs);
    obstack_chunk_size(&idx->obs) = 1024*128;
    obstack_alignment_mask(&idx->obs) = 0;
    return 1;
}


void capreq_idx_destroy(struct capreq_idx *idx) 
{
    n_hash_free(idx->ht);
    obstack_free(&idx->obs, NULL); /* freeing whole obstack */
}


int capreq_idx_add(struct capreq_idx *idx, const char *capname,
                   struct pkg *pkg, int isprov)
{
    void **p;
    struct capreq_idx_ent *ent;
            
    if ((p = n_hash_get(idx->ht, capname)) == NULL) {
        ent = malloc(sizeof(*ent) + sizeof(void*));
        ent->items = 1;
        ent->_size = 1;
        ent->pkgs[0] = pkg;
        p = obstack_alloc(&idx->obs, sizeof(ent));
        *p = ent;
        n_hash_insert(idx->ht, capname, p);
        
    } else {
        int i;
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
        
        if (ent->items == ent->_size) {
            struct capreq_idx_ent *new_ent;
            int new_size;
            
            new_size = sizeof(*ent) + (2 * ent->_size * sizeof(void*));
            new_ent = realloc(ent, new_size);
            ent = new_ent;
            
            ent->_size *= 2;
            *p = ent;
        }
        ent->pkgs[ent->items++] = pkg;
    }
    
    return 1;
}

void capreq_idx_remove(struct capreq_idx *idx, const char *capname) 
{
    n_hash_remove(idx->ht, capname);
}

const
struct capreq_idx_ent *capreq_idx_lookup(struct capreq_idx *idx,
                                         const char *capname)
{
    void **p;
    
    if ((p = n_hash_get(idx->ht, capname)) == NULL)
        return 0;

    return *p;
}




