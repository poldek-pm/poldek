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
#include <obstack.h>

#include <rpm/rpmlib.h>
#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/nbuf.h>

#include "rpmadds.h"
#include "log.h"
#include "pkg.h"
#include "pkgfl.h"
#include "depdirs.h"
#include "h2n.h"

#define obstack_chunk_alloc malloc
#define obstack_chunk_free  free

struct fl_allocator_s {
    tn_hash           *dirns;
    tn_array          *cur_dirns;
    void              *cur_mark;
    struct obstack    ob;
};

static struct fl_allocator_s *flalloct = NULL;

int pkgflmodule_init(void) 
{
    flalloct = malloc(sizeof(*flalloct));
    if (flalloct == NULL)
        return 0;
    
    flalloct->dirns = n_hash_new(5003, NULL);
    if (flalloct->dirns == NULL)
       return 0;
    
    n_hash_ctl(flalloct->dirns, TN_HASH_NOCPKEY);
    obstack_init(&flalloct->ob);
    obstack_chunk_size(&flalloct->ob) = 1024*128;
    flalloct->cur_dirns = NULL;
    flalloct->cur_mark = NULL;
    return 1;
}


void pkgflmodule_free_unneeded(void) 
{
    if (flalloct && flalloct->dirns) {
        n_hash_free(flalloct->dirns);
        flalloct->dirns = NULL;
    }
}


void pkgflmodule_destroy(void) 
{
    if (flalloct) {
        pkgflmodule_free_unneeded();
        obstack_free(&flalloct->ob, NULL);
        free(flalloct);
        flalloct = NULL;
    }
}

#if 1
void *pkgflmodule_allocator_push_mark(void) 
{
    flalloct->cur_mark = obstack_alloc(&flalloct->ob, 1);
    flalloct->cur_dirns = n_array_new(2, NULL, NULL);
    return flalloct->cur_mark;
}

void pkgflmodule_allocator_pop_mark(void *ptr) 
{
    int i;
    
    for (i=0; i<n_array_size(flalloct->cur_dirns); i++) 
        n_hash_remove(flalloct->dirns, n_array_nth(flalloct->cur_dirns, i));
    n_array_free(flalloct->cur_dirns);
    obstack_free(&flalloct->ob, ptr);
}
#endif
__inline__
static void *pkgfl_alloc(size_t size) 
{
    return obstack_alloc(&flalloct->ob, size);
}

__inline__
static char *pkgfl_strdup(const char *str, size_t size)
{
    return obstack_copy0(&flalloct->ob, str, size);
}


struct flfile *flfile_new(uint32_t size, uint16_t mode, 
                          const char *basename, int blen, 
                          const char *slinkto, int slen)
{
    struct flfile *file;
    char *p;
    
    file = pkgfl_alloc(sizeof(*file) + blen + 1 + slen + 1);
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

int pkgfl_ent_cmp(const void *a,  const void *b) 
{
    const struct pkgfl_ent *aa = a;
    const struct pkgfl_ent *bb = b;
    return strcmp(aa->dirname, bb->dirname);
}

tn_array *pkgfl_array_new(int size)
{
    tn_array *arr;
    
    if ((arr = n_array_new(size, NULL, pkgfl_ent_cmp)) != NULL)
        n_array_ctl(arr, TN_ARRAY_AUTOSORTED);
    
    return arr;
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


struct pkgfl_ent *pkgfl_ent_new(char *dirname, int dirname_len, int nfiles) 
{
    struct pkgfl_ent *flent;
    char *dirnamep;
    
    flent = pkgfl_alloc(sizeof(*flent)+(nfiles * sizeof(struct flfile*)));
                        
    dirname = prepare_dirname(dirname, &dirname_len);
    
    /* find directory */
    if ((dirnamep = n_hash_get(flalloct->dirns, dirname)) == NULL) {
        dirnamep = pkgfl_strdup(dirname, dirname_len);
        n_hash_insert(flalloct->dirns, dirnamep, dirnamep);
        if (flalloct->cur_dirns)
            n_array_push(flalloct->cur_dirns, dirnamep);
    }
    
    flent->dirname = dirnamep;
    flent->items = 0;
    return flent;
}


/*
  stores file list as binary data
 */
int pkgfl_store(tn_array *fl, tn_buf *nbuf, tn_array *depdirs, int which)
{
    int8_t *matches;
    int i, j;
    int ndirs = 0;
    

    matches = alloca(n_array_size(fl) * sizeof(*matches));
    memset(matches, 0, n_array_size(fl) * sizeof(*matches));

    if (which == PKGFL_ALL) {
        memset(matches, 1, n_array_size(fl) * sizeof(*matches));
        ndirs = n_array_size(fl);
        
    } else {
        for (i=0; i<n_array_size(fl); i++) {
            struct pkgfl_ent *flent = n_array_nth(fl, i);
            int dnl, is_depdir = 0;
            
            dnl = strlen(flent->dirname);
            is_depdir = (n_array_bsearch(depdirs, flent->dirname) != NULL);
        
            if (which == PKGFL_DEPDIRS && is_depdir) {
                matches[i] = 1;
                ndirs++;
                
            } else if (which == PKGFL_NOTDEPDIRS && !is_depdir) {
                matches[i] = 1;
                ndirs++;
            }
        }
    }

    n_buf_add_int32(nbuf, ndirs);
    
    for (i=0; i<n_array_size(fl); i++) {
        struct pkgfl_ent *flent = n_array_nth(fl, i);
        uint8_t dnl;
        
        if (matches[i] == 0) 
            continue;
        
        dnl = strlen(flent->dirname) + 1;

        n_buf_add_int8(nbuf, dnl);
        n_buf_add(nbuf, flent->dirname, dnl);
        n_buf_add_int32(nbuf, flent->items);
        
        for (j=0; j<flent->items; j++) {
            struct flfile *file = flent->files[j];
            uint8_t bnl = strlen(file->basename);

            n_buf_add_int8(nbuf, bnl);
            n_buf_add(nbuf, file->basename, bnl);
            n_buf_add_int16(nbuf, file->mode);
            n_buf_add_int32(nbuf, file->size);

            if (S_ISLNK(file->mode)) {
                char *linkto = file->basename + bnl + 1;
                bnl = strlen(linkto);
                n_buf_add_int8(nbuf, bnl);
                n_buf_add(nbuf, linkto, bnl);
            }
        }
    }
    
    return ndirs;
}

int pkgfl_store_f(tn_array *fl, FILE *stream, tn_array *depdirs, int which) 
{
    tn_buf *nbuf;
    uint32_t size;
    
    nbuf = n_buf_new(4096);
    
    pkgfl_store(fl, nbuf, depdirs, which);
    size = hton32(n_buf_size(nbuf));
    fwrite(&size, sizeof(size), 1, stream);
    
    return fwrite(n_buf_ptr(nbuf), n_buf_size(nbuf), 1, stream) == 1 &&
        fwrite("\n", 1, 1, stream) == 1;

    return 1;
}

tn_array *pkgfl_restore(tn_buf_it *nbufi, tn_array *only_dirs)
{
    tn_array *fl = NULL;
    int32_t ndirs = 0;
    int j;
    

    if (!n_buf_it_get_int32(nbufi, &ndirs))
        return NULL;
    
    fl = pkgfl_array_new(ndirs);
    
    while (ndirs--) {
        struct pkgfl_ent  *flent = NULL;
        char              *dn = NULL;
        int8_t            dnl = 0;
        int32_t           nfiles = 0;
        int               skipdir;
        
        
        n_buf_it_get_int8(nbufi, &dnl);
        dn = n_buf_it_get(nbufi, dnl);

        skipdir = 0;
        if (only_dirs && n_array_bsearch(only_dirs, dn) == NULL)
            skipdir = 1;

#if 0        
        if (only_dirs && skipdir == 0)
            DBGMSG_F("NOT skipdir %s\n", dn);
#endif        
        
        dnl--;
        n_buf_it_get_int32(nbufi, &nfiles);

        if (skipdir == 0)
            flent = pkgfl_ent_new(dn, dnl, nfiles);
        
        for (j=0; j < nfiles; j++) {
            struct flfile      *file = NULL;
            char               *bn = NULL, *linkto = NULL;
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
            
            if (skipdir == 0) {
                file = flfile_new(size, mode, bn, bnl, linkto, slen);
                flent->files[flent->items++] = file;
            }
            
        }
        
        if (skipdir == 0)
            n_array_push(fl, flent);
    }
    
    return fl;
}


tn_array *pkgfl_restore_f(FILE *stream, tn_array *only_dirs) 
{
    tn_array *fl;
    tn_buf *nbuf;
    tn_buf_it nbufi;
    uint32_t size;
    char *buf;
    
    if (fread(&size, sizeof(size), 1, stream) != 1)
        return NULL;
    
    size = ntoh32(size);
    
    buf = alloca(size);
    
    if (fread(buf, size, 1, stream) != 1)
        return NULL;
    
    nbuf = n_buf_new(0);
    n_buf_init(nbuf, buf, size);
    n_buf_it_init(&nbufi, nbuf);
    fl = pkgfl_restore(&nbufi, only_dirs);
    n_buf_free(nbuf);
    
    fseek(stream, 1, SEEK_CUR); /* skip final '\n' */

    return fl;
}

int pkgfl_skip_f(FILE *stream) 
{
    uint32_t size;
    
    if (fread(&size, sizeof(size), 1, stream) != 1)
        return 0;
    
    size = ntoh32(size);
    fseek(stream, size + 1, SEEK_CUR);
    return 1;
}



__inline__ 
static int valid_fname(const char *fname, mode_t mode, const char *pkgname) 
{

#if 0  /*  */
    char *denychars = "\r\n\t |;";
    if (strpbrk(fname, denychars)) {
        log(LOGINFO, "%s: bad habit: %s \"%s\" with whitespaces\n",
            pkgname, S_ISDIR(mode) ? "dirname" : "filename", fname);
    }
#endif     

    if (strlen(fname) > 255) {
        log(LOGERR, "%s: %s \"%s\" longer than 255 bytes\n",
            pkgname, S_ISDIR(mode) ? "dirname" : "filename", fname);
        return 0;
    }
    
    return 1;
}

/* -1 on error  */
int pkgfl_ldhdr(tn_array *fl, Header h, int which, const char *pkgname)
{
    int t1, t2, t3, t4, c1, c2, c3, c4;
    char **names = NULL, **dirs = NULL, **symlinks = NULL, **skipdirs;
    int32_t   *diridxs;
    uint32_t  *sizes;
    uint16_t  *modes;
    struct    flfile *flfile;
    struct    pkgfl_ent **fentdirs = NULL;
    int       *fentdirs_items;
    int       i, j, ndirs = 0, nerr = 0;
    
    
    if (!headerGetEntry(h, RPMTAG_BASENAMES, (void*)&t1, (void*)&names, &c1))
        return 0;

    n_assert(t1 == RPM_STRING_ARRAY_TYPE);
    if (!headerGetEntry(h, RPMTAG_DIRNAMES, (void*)&t2, (void*)&dirs, &c2))
        goto l_endfunc;
    
    n_assert(t2 == RPM_STRING_ARRAY_TYPE);
    if (!headerGetEntry(h, RPMTAG_DIRINDEXES, (void*)&t3,(void*)&diridxs, &c3))
    {
        log_msg("%s: no DIRINDEXES tag\n", pkgname);
        nerr++;
        goto l_endfunc;
    }

    n_assert(t3 == RPM_INT32_TYPE);
    
    if (c1 != c3) {
        log(LOGERR, "%s: DIRINDEXES (%d) != BASENAMES (%d) tag\n", c3, c1,
            pkgname);
        nerr++;
        goto l_endfunc;
    }
    
    if (!headerGetEntry(h, RPMTAG_FILEMODES, (void*)&t4, (void*)&modes, &c4)) {
        log_msg("%s: no FILEMODES tag\n", pkgname);
        nerr++;
        goto l_endfunc;
    }
    
    if (!headerGetEntry(h, RPMTAG_FILESIZES, (void*)&t4, (void*)&sizes, &c4)) {
        log_msg("%s: no FILESIZES tag\n", pkgname);
        nerr++;
        goto l_endfunc;
    }
    
    if (!headerGetEntry(h, RPMTAG_FILELINKTOS, (void*)&t4, (void*)&symlinks,
                        &c4)) {
        symlinks = NULL;
    }
    
    skipdirs = alloca(sizeof(*skipdirs) * c2);
    fentdirs = alloca(sizeof(*fentdirs) * c2);
    fentdirs_items = alloca(sizeof(*fentdirs_items) * c2);

    /* skip unneded dirnames */
    for (i=0; i<c2; i++) {
        struct pkgfl_ent *flent;

        fentdirs_items[i] = 0;
        if (!valid_fname(dirs[i], 0, pkgname))
            nerr++;

        
        if (which != PKGFL_ALL) {
            int is_depdir;

            is_depdir = in_depdirs(dirs[i] + 1);
            
            if (!is_depdir && which == PKGFL_DEPDIRS) {
                msg(5, "skip files in dir %s\n", dirs[i]);
                skipdirs[i] = NULL;
                fentdirs[i] = NULL;
                continue;
                
            } else if (is_depdir && which == PKGFL_NOTDEPDIRS) {
                msg(5, "skip files in dir %s\n", dirs[i]);
                skipdirs[i] = NULL;
                fentdirs[i] = NULL;
                continue;
            }
        }
        
        skipdirs[i] = dirs[i];
        for (j=0; j<c1; j++)
            if (diridxs[j] == i)
                fentdirs_items[i]++;
        
        flent = pkgfl_ent_new(dirs[i], strlen(dirs[i]), fentdirs_items[i]);
        fentdirs[i] = flent;
        ndirs++;
    }
    
    msg(4, "%d files in package\n", c1);
    for (i=0; i<c1; i++) {
        struct pkgfl_ent *flent;
        register int j = diridxs[i];
        int len;

        if (!valid_fname(names[i], modes[i], pkgname))
            nerr++;
        
        msg(5, "  %d: %s %s/%s \n", i, skipdirs[j] ? "add " : "skip",
            dirs[j], names[i]);
            
        if (skipdirs[j] == NULL)
            continue;
        
        flent = fentdirs[j];
        len = strlen(names[i]);
        if (symlinks) { 
            flfile = flfile_new(sizes ? sizes[i] : 0,
                                modes ? modes[i] : 0,
                                names[i], len,
                                symlinks[i],
                                strlen(symlinks[i]));
        } else {
            flfile = flfile_new(sizes ? sizes[i] : 0,
                                modes ? modes[i] : 0,
                                names[i], len,
                                NULL,
                                0);
            
        }
        
        flent->files[flent->items++] = flfile;
        n_assert(flent->items <= fentdirs_items[j]);
    }
    
 l_endfunc:
    
    if (c1 && names)
        rpm_headerEntryFree(names, t1);

    if (c2 && dirs)
        rpm_headerEntryFree(dirs, t2);

    if (c4 && symlinks)
        rpm_headerEntryFree(symlinks, t4);

    if (nerr) {
        log(LOGERR, "%s: skiped\n", pkgname);
        
    } else if (ndirs) {
        for (i=0; i<c2; i++) 
            if (fentdirs[i] != NULL)
                n_array_push(fl, fentdirs[i]);
    }
    
    return nerr ? -1 : 1;
}


