/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@k2.net.pl>

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

#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <trurl/trurl.h>

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgfl.h"
#include "depdirs.h"

struct dirname_h {
    tn_hash *dnh;
    tn_alloc *na;
    int      n;
};


static struct dirname_h dirname_h = { 0, 0, 0 };

static inline char *register_dn(char *dn)
{
    char *dnn;
    
    if (dirname_h.dnh == NULL) {
        dirname_h.na = n_alloc_new(16, TN_ALLOC_OBSTACK);
        dirname_h.dnh = n_hash_new_na(dirname_h.na, 4096, NULL);
        n_hash_ctl(dirname_h.dnh, TN_HASH_NOCPKEY | TN_HASH_REHASH);
    }
    
    if ((dnn = n_hash_get(dirname_h.dnh, dn)) == NULL) {
        int len = strlen(dn) + 1;
        dnn = dirname_h.na->na_malloc(dirname_h.na, len);
        memcpy(dnn, dn, len);
        n_hash_insert(dirname_h.dnh, dnn, dnn);
        
    } else {
        dirname_h.n++;
        //printf("hhhh %d %s\n", dirname_h.n, dnn);
    }

    return dnn;
}


struct flfile *flfile_new(tn_alloc *na, uint32_t size, uint16_t mode, 
                          const char *basename, int blen, 
                          const char *slinkto, int slen)
{
    struct flfile *file;
    char *p;

    if (na)
        file = na->na_malloc(na, sizeof(*file) + blen + 1 + slen + 1);
    else
        file = n_malloc(sizeof(*file) + blen + 1 + slen + 1);
    
    file->mode = mode;
    file->size = size;

    memcpy(file->basename, basename, blen);
    p = file->basename + blen;
    *p++ = '\0';
    *p = '\0';
    
    if (slinkto && *slinkto) {
        memcpy(p, slinkto, slen);
        *(p + slen) = '\0';
    }
    return file;
}


struct flfile *flfile_clone(struct flfile *flfile) 
{
    int bnl = strlen(flfile->basename);
    
    return flfile_new(NULL, flfile->size, flfile->mode,
                      flfile->basename, bnl,
                      S_ISLNK(flfile->mode) ? flfile->basename + bnl + 1 : NULL,
                      S_ISLNK(flfile->mode) ? strlen(flfile->basename + bnl + 1) : 0);
}


int flfile_cnfl2(const struct flfile *f1, uint32_t size, uint16_t mode,  
                 const char *slinkto, int strict)
{
    register int cmprc;
    
    if ((cmprc = (f1->size - size)) == 0)
        cmprc = f1->mode - mode;

    if (cmprc == 0 || strict == 0) {
        if (S_ISLNK(f1->mode)) {
            if (!S_ISLNK(mode))
                cmprc = 1;
            else {
                register char *l1;
                
                l1 = strchr(f1->basename, '\0') + 1;
                n_assert(slinkto);
                cmprc = strcmp(l1, slinkto);
            }
            
        } else if (S_ISLNK(mode)) {
            cmprc = -1;
        }
    }

    if (cmprc && strict == 0 && S_ISDIR(f1->mode) && S_ISDIR(mode))
        cmprc = 0;
    
    return cmprc;
    
}

int flfile_cnfl(const struct flfile *f1, const struct flfile *f2, int strict)
{
    register int cmprc;

    if (f1->mode == 0 || f2->mode == 0) /* missing FILEMODES || FILESIZES */
        return 0;
    
    if ((cmprc = (f1->mode - f2->mode)) == 0 &&
        !S_ISDIR(f1->mode) && !S_ISDIR(f1->mode)) 
        cmprc = f1->size - f2->size;

    if (cmprc == 0 || strict == 0) {
        if (S_ISLNK(f1->mode)) {
            if (!S_ISLNK(f2->mode))
                cmprc = 1;
            else {
                register char *l1, *l2;
               
                l1 = strchr(f1->basename, '\0') + 1;
                l2 = strchr(f2->basename, '\0') + 1;
                cmprc = strcmp(l1, l2);
            }
            
        } else if (S_ISLNK(f2->mode)) {
            cmprc = -1;
        }
    }

    if (cmprc && strict == 0 && S_ISDIR(f1->mode) && S_ISDIR(f2->mode))
        cmprc = 0;
    
    return cmprc;
}

int flfile_cmp(const struct flfile *f1, const struct flfile *f2)
{
    register int cmprc;

    //printf("cmp %s %s\n", f1->basename, f2->basename);
    if ((cmprc = strcmp(f1->basename, f2->basename)))
        return cmprc;
    
    if ((cmprc = (f1->size - f2->size)) == 0)
        cmprc = f1->mode - f2->mode;
    
    if (cmprc == 0 && S_ISLNK(f1->mode)) {
        register char *l1, *l2;
        
        l1 = strchr(f1->basename, '\0') + 1;
        l2 = strchr(f2->basename, '\0') + 1;
        cmprc = strcmp(l1, l2);
    }
    
    return cmprc;
}

int flfile_cmp_qsort(const struct flfile **f1, const struct flfile **f2)
{
    return flfile_cmp(*f1, *f2);
}

int pkgfl_ent_cmp(const void *a,  const void *b) 
{
    const struct pkgfl_ent *aa = a;
    const struct pkgfl_ent *bb = b;
    return strcmp(aa->dirname, bb->dirname);
}

static
int pkgfl_ent_deep_cmp(const void *a,  const void *b) 
{
    register int i, cmprc;
    
    const struct pkgfl_ent *aa = a;
    const struct pkgfl_ent *bb = b;
    
    if ((cmprc = strcmp(aa->dirname, bb->dirname)))
        return cmprc;

    if ((cmprc = aa->items - bb->items))
        return cmprc;

    for (i = 0; i < aa->items; i++)
        if ((cmprc = flfile_cmp(aa->files[i], bb->files[i])))
            return cmprc;

    logn(LOGERR | LOGDIE, "pkgfl_ent_deep_cmp: %p:%s eq %p:%s", aa, aa->dirname,
         bb, bb->dirname);
    n_assert(0);                /* directories must be different */
    return cmprc;
}


tn_array *pkgfl_array_new(int size)
{
    tn_array *arr;
    
    if ((arr = n_array_new(size, NULL, pkgfl_ent_cmp)) != NULL)
        n_array_ctl(arr, TN_ARRAY_AUTOSORTED);
    
    return arr;
}

tn_tuple *pkgfl_array_pdir_sort(tn_tuple *fl)
{
    return n_tuple_isort_ex(fl, pkgfl_ent_deep_cmp);
}

/* trim slashes from dirname, update dirnamelen  */
static
char *prepare_dirname(char *dirname, int *dirnamelen) 
{
    if (dirname[*dirnamelen - 1] == '/' && *dirnamelen > 1) {
        (*dirnamelen)--;
        dirname[*dirnamelen] = '\0';
    }
    
    if (*dirname == '/' && *dirnamelen > 1) {
        dirname++;
        (*dirnamelen)--;
    }
    return dirname;
}


struct pkgfl_ent *pkgfl_ent_new(tn_alloc *na,
                                char *dirname, int dirname_len, int nfiles)
{
    struct pkgfl_ent *flent;
    
    flent = na->na_malloc(na, sizeof(*flent)+(nfiles * sizeof(struct flfile*)));
    dirname = prepare_dirname(dirname, &dirname_len);

    flent->dirname = register_dn(dirname);
    flent->items = 0;
    DBGF("flent_new %s %d\n", flent->dirname, nfiles);
    return flent;
}


static int strncmp_path(const char *p1, const char *p2)
{
    int rc;
    
    if ((rc = *p1 - *p2))
        return rc;

    rc = strncmp(p1, p2, strlen(p1));
    //printf("cmp %s %s = %d\n", p1, p2, rc);
    return rc;
}

/*
  stores file list as binary data
 */
static
int pkgfl_store_buf(tn_tuple *fl, tn_buf *nbuf, tn_array *exclpath, 
                    tn_array *depdirs, int which)
{
    uint8_t *matches, *skipped, *lengths;
    int i, j;
    int ndirs = 0;
    

    matches = alloca(n_tuple_size(fl) * sizeof(*matches));
    memset(matches, 0, n_tuple_size(fl) * sizeof(*matches));
    skipped = alloca(n_tuple_size(fl) * sizeof(*matches));
    memset(skipped, 0, n_tuple_size(fl) * sizeof(*skipped));

    lengths = alloca(n_tuple_size(fl) * sizeof(*lengths));
    memset(lengths, 0, n_tuple_size(fl) * sizeof(*lengths));

    if (which == PKGFL_ALL && exclpath == NULL) {
        memset(matches, 1, n_tuple_size(fl) * sizeof(*matches));
        ndirs = n_tuple_size(fl);
        
    } else {
        for (i=0; i<n_tuple_size(fl); i++) {
            struct pkgfl_ent *flent = n_tuple_nth(fl, i);
            int is_depdir = 0, dnl;

            dnl = strlen(flent->dirname);
            n_assert(dnl < UINT8_MAX - 1);
            
            lengths[i] = dnl;

            if (depdirs)
                is_depdir = (n_array_bsearch(depdirs, flent->dirname) != NULL);

            n_assert(which != PKGFL_ALL);
            
            
            if (which == PKGFL_DEPDIRS && is_depdir) {
                matches[i] = 1;
                ndirs++;
                
            } else if (which == PKGFL_NOTDEPDIRS && !is_depdir) {
                if (exclpath && n_array_bsearch_ex(exclpath, flent->dirname,
                                                   (tn_fn_cmp)strncmp_path)) {
                    DBGF("%s skipped\n", flent->dirname);
                    skipped[i] = 1;
                    continue;
                }
                
                matches[i] = 1;
                ndirs++;
            }
        }
    }

    n_buf_add_int32(nbuf, ndirs);
    
    for (i=0; i < n_tuple_size(fl); i++) {
        struct pkgfl_ent *flent = n_tuple_nth(fl, i);
        uint8_t dnl;
        
        if (matches[i] == 0) 
            continue;
        
        dnl = strlen(flent->dirname) + 1;
        
        n_buf_add_int8(nbuf, dnl);
        n_buf_add(nbuf, flent->dirname, dnl);
        n_buf_add_int32(nbuf, flent->items);
#if 0
        if (strstr(flent->dirname, "SourceForgeXX"))
            printf("\nDIR %s\n", flent->dirname);
#endif        
        for (j=0; j < flent->items; j++) {
            struct flfile *file = flent->files[j];
            uint8_t bnl = strlen(file->basename);

#if 0            
            if (strstr(flent->dirname, "SourceForgeXX")) 
                printf("STORE%d %s\n", which, file->basename);
#endif
            n_buf_add_int8(nbuf, bnl);
            n_buf_add(nbuf, file->basename, bnl);
            n_buf_add_int16(nbuf, file->mode);
            n_buf_add_int32(nbuf, file->size);

            if (S_ISLNK(file->mode)) {
                char *linkto = file->basename + bnl + 1;
                bnl = strlen(linkto);
                n_buf_add_int8(nbuf, bnl);
                n_buf_add(nbuf, linkto, bnl);
#if 0          				
                if (strstr(flent->dirname, "SourceForgeXX")) 
                    printf("STORE%d linkto %s\n", which, linkto);
#endif				
            }
        }
    }
    
    return ndirs;
}

#if 0                           /* unused */
int pkgfl_store_st(tn_array *fl, tn_stream *st, tn_array *depdirs, int which) 
{
    tn_buf *nbuf;
    int rc;
    
    nbuf = n_buf_new(4096);
    
    pkgfl_store_buf(fl, nbuf, NULL, depdirs, which);
    rc = n_buf_store_buf(nbuf, st, TN_BUF_STORE_32B);
    n_buf_free(nbuf);
	n_stream_printf(st, "\n");
    return rc;
}
#endif

int pkgfl_store(tn_tuple *fl, tn_buf *nbuf,
                tn_array *exclpath, tn_array *depdirs, int which) 
{
    int sizeoffs, offs;
    int32_t bsize;
    
    sizeoffs = n_buf_tell(nbuf);
    n_buf_seek(nbuf, sizeof(bsize), SEEK_CUR); /* place for buf size */

    offs = n_buf_tell(nbuf);
    pkgfl_store_buf(fl, nbuf, exclpath, depdirs, which);
    bsize = n_buf_tell(nbuf) - offs;

    n_assert(bsize > 0);

    /* write buffer size  */
    n_buf_seek(nbuf, sizeoffs, SEEK_SET);
    n_buf_write_int32(nbuf, bsize);
    n_buf_seek(nbuf, 0, SEEK_END);
	n_buf_puts(nbuf, "\n");

    return bsize;
}


int pkgfl_restore(tn_alloc *na, tn_tuple **fl,
                  tn_buf_it *nbufi, tn_array *dirs, int include)
{
    struct pkgfl_ent **ents;
    int32_t ndirs = 0, n;
    int j, default_loadir;

    *fl = NULL;
    default_loadir = 1;
    if (dirs) 
        default_loadir = include ? 0 : 1;
    
    if (!n_buf_it_get_int32(nbufi, &ndirs))
        return -1;

    ents = alloca(ndirs * sizeof(*ents));
    n = 0;
    
    while (ndirs--) {
        struct pkgfl_ent  *flent = NULL;
        char              *dn = NULL;
        uint8_t           dnl = 0;
        int32_t           nfiles = 0;
        int               loadir;
        
        
        n_buf_it_get_int8(nbufi, &dnl);
        dn = n_buf_it_get(nbufi, dnl);

        loadir = default_loadir;            
        if (dirs && n_array_bsearch(dirs, dn))
            loadir = include;
        
#if 0
        if (loadir)
            printf("LOAD (%d) %s\n", include, dn);
#endif        
        
        dnl--;
        n_buf_it_get_int32(nbufi, &nfiles);

        if (loadir) {
            flent = pkgfl_ent_new(na, dn, dnl, nfiles);
            ents[n++] = flent;
        }
        
        for (j=0; j < nfiles; j++) {
            char               *bn, *linkto = NULL;
            uint8_t            bnl = 0, slen = 0;
            uint16_t           mode = 0;
            uint32_t           size = 0;
            

            n_buf_it_get_int8(nbufi, &bnl);
            bn = n_buf_it_get(nbufi, bnl);
            
            n_buf_it_get_int16(nbufi, &mode);
            n_buf_it_get_int32(nbufi, &size);

            if (S_ISLNK(mode)) {
                n_buf_it_get_int8(nbufi, &slen);
                linkto = n_buf_it_get(nbufi, slen);
            }
            
            if (loadir) {
                struct flfile *file;
                file = flfile_new(na, size, mode, bn, bnl, linkto, slen);
                flent->files[flent->items++] = file;
            }
            
        }
    }
    DBGF("n = %d\n", n);
    if (n > 0)
        *fl = n_tuple_new(na, n, (void **)ents);
        
    return n;
}


int pkgfl_restore_st(tn_alloc *na, tn_tuple **fl, 
                     tn_stream *st, tn_array *dirs, int include) 
{
    tn_buf *nbuf = NULL;
    tn_buf_it nbufi;
    int rc = 0;

    *fl = NULL;
    n_buf_restore(st, &nbuf, TN_BUF_STORE_32B);
    if (nbuf == NULL)
        return -1;
    
    n_buf_it_init(&nbufi, nbuf);
    rc = pkgfl_restore(na, fl, &nbufi, dirs, include);
    n_buf_free(nbuf);
    n_stream_seek(st, 1, SEEK_CUR); /* skip ending '\n' */
    return rc;
}


int pkgfl_skip_st(tn_stream *st) 
{
    n_buf_restore_skip(st, TN_BUF_STORE_32B);
    n_stream_seek(st, 1, SEEK_CUR); /* skip ending '\n' */
    return 1;
}

void pkgfl_dump(tn_tuple *fl)
{
    int i, j;

    if (fl == NULL)
        return;
    
    for (i=0; i < n_tuple_size(fl); i++) {
        struct pkgfl_ent *flent = n_tuple_nth(fl, i);
        printf("DIR %s:", flent->dirname);
        for (j=0; j<flent->items; j++) {
            printf(" %s,", flent->files[j]->basename);
        }
        printf("\n");
    }
    return;
}
