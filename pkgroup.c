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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <trurl/trurl.h>
#include <rpm/rpmlib.h>

#define ENABLE_TRACE 0

#include "rpmhdr.h"
#include "log.h"
#include "pkgu.h"
#include "h2n.h"


const char *pkgroups_tag = "GROUPS:";

struct pkgroup_idx {
    tn_hash *ht;                /* name => struct pkgroup */
    tn_array *arr;
};

struct tr {
    char lang[64];
    char name[0];
};

struct pkgroup {
    int      id;
    int      ntrs;
    tn_hash  *trs;                /* translations */
    char     name[0];
};


struct tr *tr_new(const char *lang, const char *name) 
{
    struct tr *tr;
    int len;


    len = strlen(name) + 1;
    tr = malloc(sizeof(*tr) + len);
    strncpy(tr->lang, lang, sizeof(tr->lang))[sizeof(tr->lang) - 1] = '\0';
    memcpy(tr->name, name, len);
    return tr;
}

static
int tr_store(struct tr *tr, FILE *stream) 
{
    int      len, n;
    uint8_t  nlen;
    char     buf[255];
    
    len = strlen(tr->lang) + strlen(tr->name) + 1 /*:*/;
    n_assert(len < UINT8_MAX);
    
    nlen = len;
    fwrite(&nlen, sizeof(nlen), 1, stream);
    n = snprintf(buf, sizeof(buf), "%s:%s", tr->lang, tr->name);
    DBGF("%s\n", buf);
    n_assert(n == len);
    
    return fwrite(buf, len, 1, stream) == 1;
}

static
struct tr *tr_restore(FILE *stream) 
{
    int      len;
    uint8_t  nlen;
    char     buf[255], *p;

    if (fread(&nlen, sizeof(nlen), 1, stream) != 1)
        return NULL;

    len = nlen;
    if (fread(buf, len, 1, stream) != 1)
        return NULL;

    buf[len] = '\0';

    if ((p = strchr(buf, ':')) == NULL)
        return NULL;

    *p++ = '\0';

    return tr_new(buf, p);
}

static
struct pkgroup *pkgroup_new(int id, const char *name) 
{
    struct pkgroup *gr;
    int len;

    len = strlen(name) + 1;
    
    if ((gr = malloc(sizeof(*gr) + len)) == NULL)
        return gr;
    
    gr->id = id;
    gr->ntrs = 0;
    memcpy(gr->name, name, len);

    gr->trs = n_hash_new(21, free);
    n_hash_ctl(gr->trs, TN_HASH_NOCPKEY);

    return gr;
}

static    
void pkgroup_free(struct pkgroup *gr) 
{
    if (gr->trs) {
        n_hash_free(gr->trs);
        gr->trs = NULL;
    }
    free(gr);
}

static
int pkgroup_cmp(const struct pkgroup *gr1, const struct pkgroup *gr2) 
{
    return gr1->id - gr2->id;
}

static
int pkgroup_add_tr(struct pkgroup *gr, struct tr *tr)  
{
    if (n_hash_exists(gr->trs, tr->lang))
        return 1;

    if (n_hash_insert(gr->trs, tr->lang, tr)) {
        gr->ntrs++;
        return 1;
    }
    
    return 0;
}

static
int pkgroup_add(struct pkgroup *gr, const char *lang, const char *name)  
{
    struct tr *tr;

    
    if (n_hash_exists(gr->trs, lang))
        return 1;

    if ((tr = tr_new(lang, name))) {
        if (n_hash_insert(gr->trs, lang, tr)) {
            gr->ntrs++;
            return 1;
        }
    }
    
    return 0;
}


void map_fn_store_tr(const char *lang, void *tr, void *stream) 
{
    lang = lang;
    tr_store(tr, stream);
}

static
int pkgroup_store(struct pkgroup *gr, FILE *stream)  
{
    int      len;
    uint8_t  nlen;
    uint32_t nid, nntrs;
    
    nid = hton32(gr->id);
    if (fwrite(&nid, sizeof(nid), 1, stream) != 1)
        return 0;

    len = strlen(gr->name) + 1;
    n_assert(len < UINT8_MAX);
    
    nlen = len;
    if (fwrite(&nlen, sizeof(nlen), 1, stream) != 1)
        return 0;
    DBGF("%d %s\n", gr->id, gr->name);
    if (fwrite(gr->name, len, 1, stream) != 1)
        return 0;
    
    nntrs = hton32(gr->ntrs);
    if (fwrite(&nntrs, sizeof(nntrs), 1, stream) != 1)
        return 0;
    
    n_hash_map_arg(gr->trs, map_fn_store_tr, stream);
    return 1;
}

static
struct pkgroup *pkgroup_restore(FILE *stream)  
{
    int             ntrs, nerr = 0, i;
    uint8_t         nlen;
    uint32_t        nid, nntrs;
    struct pkgroup  *gr;
    char            name[255];
    
    if (fread(&nid, sizeof(nid), 1, stream) != 1)
        return 0;

    if (fread(&nlen, sizeof(nlen), 1, stream) != 1 || nlen > sizeof(name))
        return 0;
    
    if (fread(name, nlen, 1, stream) != 1)
        return 0;
    
    if (fread(&nntrs, sizeof(nntrs), 1, stream) != 1)
        return 0;

    gr = pkgroup_new(ntoh32(nid), name);
    ntrs = ntoh32(nntrs);
    
    for (i=0; i < ntrs; i++) {
        struct tr *tr;

        if ((tr = tr_restore(stream)) == NULL)
            nerr++;
        else
            pkgroup_add_tr(gr, tr);
    }
    
    if (nerr > 0) {
        pkgroup_free(gr);
        gr = NULL;
    }
    
    return gr;
}


struct pkgroup_idx *pkgroup_idx_new(void) 
{
    struct pkgroup_idx *idx;

    idx = malloc(sizeof(*idx));
    idx->ht = n_hash_new(101, NULL);
    n_hash_ctl(idx->ht, TN_HASH_NOCPKEY);
    idx->arr = n_array_new(128, (tn_fn_free)pkgroup_free,
                           (tn_fn_cmp)pkgroup_cmp);
    
    return idx;
}

void pkgroup_idx_free(struct pkgroup_idx *idx) 
{
    if (idx->ht) {
        n_hash_free(idx->ht);
        idx->ht = NULL;
    }
    
    if (idx->arr) {
        n_array_free(idx->arr);
        idx->arr = NULL;
    }
        
    free(idx);
}


static
void map_fn_store_gr(void *gr, void *stream) 
{
    pkgroup_store(gr, stream);
}


int pkgroup_idx_store(struct pkgroup_idx *idx, FILE *stream) 
{
    uint32_t nsize;

    fprintf(stream, "%%%s\n", pkgroups_tag);
    nsize = hton32(n_array_size(idx->arr));
    if (fwrite(&nsize, sizeof(nsize), 1, stream) != 1)
        return 0;
    
    n_array_map_arg(idx->arr, map_fn_store_gr, stream);
    return fprintf(stream, "\n") == 1;
}


struct pkgroup_idx *pkgroup_idx_restore(FILE *stream, unsigned flags)
{
    struct pkgroup_idx *idx;
    uint32_t nsize;
    int i;

    
    flags = flags;
    if (fread(&nsize, sizeof(nsize), 1, stream) != 1)
        return NULL;

    nsize = ntoh32(nsize);
    
    idx = pkgroup_idx_new();

    for (i=0; i < (int)nsize; i++) 
        n_array_push(idx->arr, pkgroup_restore(stream));

//    if (flags & PKGROUP_MKNAMIDX)
//        ;
    n_array_sort(idx->arr);
    getc(stream); /* eat '\n' */
    return idx;
}


int pkgroup_idx_update(struct pkgroup_idx *idx, Header h) 
{
    char               **langs, **groups;
    int                i, ngroups, nlangs = 0;
    struct pkgroup     *gr = NULL;

    if ((langs = headerGetLangs(h)) == NULL)
        return 0;

    headerGetRawEntry(h, RPMTAG_GROUP, 0, (void*)&groups, &ngroups);

    i = 0;
    while (langs[i++] != NULL)
        ;
    
    nlangs = i;

    n_assert(nlangs >= ngroups);

    for (i=0; i < ngroups; i++) {
        if (langs[i] == NULL)
            break;
        
        if (strcmp(langs[i], "C") == 0) {
            if ((gr = n_hash_get(idx->ht, groups[i])) == NULL) {
                gr = pkgroup_new(n_array_size(idx->arr) + 1, groups[i]);
                n_array_push(idx->arr, gr);
                n_hash_insert(idx->ht, gr->name, gr);
            }
            break;
        }
    }

    if (gr != NULL) {
        for (i=0; i < ngroups; i++) {
            if (langs[i] == NULL) 
                break;
            
            if (strcmp(langs[i], "C") == 0 || *groups[i] == '\0')
                continue;
            
            pkgroup_add(gr, langs[i], groups[i]);
        }
    }

    free(groups);
    free(langs);

    if (gr)
        return gr->id;
    return 0;
}


static const char *find_tr(const char *lang, const struct pkgroup *gr)
{
    struct tr *tr;
    const char **langs, **p;
    
    langs = n_str_tokl(lang, ":");

    p = langs;
    while (*p) {
        char   *l, *q, *sep = "@._";
        int    len;
        
        if ((tr = n_hash_get(gr->trs, *p)))
            return tr->name;

        len = strlen(*p) + 1;
        l = alloca(len + 1);
        memcpy(l, *p, len);

        while (*sep) {
            if ((q = strchr(l, *sep))) {
                *q = '\0';
                
                if ((tr = n_hash_get(gr->trs, l)))
                    return tr->name;
            }
            sep++;
        }

        p++;
    }
    
    n_str_tokl_free(langs);
    
    return gr->name;
}

const char *pkgroup(struct pkgroup_idx *idx, int groupid) 
{
    struct pkgroup *gr, tmpgr;
    const char *lang;
        
    tmpgr.id = groupid;
    if ((gr = n_array_bsearch(idx->arr, &tmpgr)) == NULL)
        return NULL;

    if (gr->trs == NULL)
        return gr->name;
    
    if ((lang = getenv("LANGUAGE")) == NULL &&
        (lang = getenv("LC_ALL")) == NULL &&
        (lang = getenv("LC_MESSAGES")) == NULL &&
	(lang = getenv("LANG")) == NULL)
        return gr->name;

    return find_tr(lang, gr);
}

static int pkgroupid(struct pkgroup_idx *idx, const char *name) 
{
    int i;

    for (i=0; i < n_array_size(idx->arr); i++) {
        struct pkgroup *gr = n_array_nth(idx->arr, i);
        if (strcmp(gr->name, name) == 0)
            return gr->id;
    }

    return -1;
}


int pkgroup_idx_merge(struct pkgroup_idx *idx,
                      struct pkgroup_idx *idx2, int groupid) 
{
    struct pkgroup *gr, tmpgr;
    int new_id;
    
        
    tmpgr.id = groupid;
    if ((gr = n_array_bsearch(idx2->arr, &tmpgr)) == NULL)
        n_assert(0);

    if ((new_id = pkgroupid(idx, gr->name)) < 0) {
        new_id = n_array_size(idx->arr) + 1;
        n_array_push(idx->arr, gr);
        n_array_sort(idx->arr);
    }
    
    return new_id;
}


