/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <stdlib.h>
#include <string.h>

#include <trurl/nassert.h>

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


int capreq_idx_add(struct capreq_idx *idx, const char *prname,
                    struct pkg *pkg, int isprov)
{
    void **p;
    struct capreq_idx_ent *ent;
            
    if ((p = n_hash_get(idx->ht, prname)) == NULL) {
        ent = malloc(sizeof(*ent) + sizeof(void*));
        ent->items = 1;
        ent->_size = 1;
        ent->pkgs[0] = pkg;
        p = obstack_alloc(&idx->obs, sizeof(ent));
        *p = ent;
        n_hash_insert(idx->ht, prname, p);
        
    } else {
        int i;

        ent = *p;

        if (isprov) {
            for (i=0; i<ent->items; i++) 
                if (ent->pkgs[i] == pkg) {
                    log(LOGWARN, "%s: redundant capability \"%s\"\n",
                        pkg_snprintf_s(pkg), prname);
                    return 1;
                }
        }
        
        
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

const
struct capreq_idx_ent *capreq_idx_lookup(struct capreq_idx *idx,
                                           const char *prname)
{
    void **p;
    
    if ((p = n_hash_get(idx->ht, prname)) == NULL)
        return 0;

    return *p;
}



static void find_depdirs(const char *reqname,
                         void *dummy __attribute__((unused)), void *arr) 
{
    if (*reqname == '/') {
        char *p;

        p = strrchr(reqname, '/');
        if (p  != reqname) {
            char *dirname;
            int len;

            len = p - reqname;
            dirname = alloca(p - reqname);
            memcpy(dirname, reqname, len);
            dirname[len] = '\0';
            p = dirname;
        }
        
        if (n_array_bsearch(arr, p) == NULL) {
            n_array_push(arr, strdup(p));
            n_array_sort(arr);
        }
    }
}


tn_array *capreq_idx_find_depdirs(struct capreq_idx *reqidx) 
{
    tn_array *dirs, *depdirs = NULL;
    char *dir;
    int i, dirlen;
    
    
    dirs = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
    n_hash_map_arg(reqidx->ht, find_depdirs, dirs);
    
    if (n_array_size(dirs) > 0) {
        depdirs = n_array_new(16, free, (tn_fn_cmp)strcmp);
        
        dir = n_array_nth(dirs, 0);
        dirlen = strlen(dir);
        n_array_push(depdirs, dir);
        
        for (i=1; i<n_array_size(dirs); i++) {
            char *d;
            
            d = n_array_nth(dirs, i);
            if (strncmp(dir, d, dirlen) == 0) /* d is dir's subdir? skip:add */
                free(d);
            else {
                n_array_push(depdirs, d);
                dir = d;
                dirlen = strlen(d);
            }
        }
#if 0       
        printf("DEPDIRS=\"");
        for (i=1; i<n_array_size(depdirs); i++) 
            printf("%s:",n_array_nth(depdirs, i));
        printf("\"\n");
#endif        
    }
    n_array_free(dirs);
    return depdirs;
}


