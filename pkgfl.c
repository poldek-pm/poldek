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

#include "rpmadds.h"
#include "log.h"
#include "pkg.h"
#include "pkgfl.h"
#include "depdirs.h"

#define obstack_chunk_alloc malloc
#define obstack_chunk_free  free

struct fl_allocator_s {
    tn_hash *dirns;
    struct obstack ob;
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
    return 1;
}


void pkgflmodule_free_unneeded(void) 
{
    if (flalloct) {
        if (flalloct->dirns) {
            n_hash_free(flalloct->dirns);
            flalloct->dirns = NULL;
        }
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

__inline__
static void *pkgfl_alloc(size_t size) 
{
    return obstack_alloc(&flalloct->ob, size);
}

struct flfile *flfile_new(uint32_t size, uint16_t mode, 
                          const char *basename, int blen, 
                          const char *slinkto, int slen)
{
    struct flfile *file;

    file = pkgfl_alloc(sizeof(*file) + blen + 1 + slen + 1);
    file->mode = mode;
    file->size = size;
    strcpy(file->basename, basename);
    if (slinkto) 
        strcpy(file->basename + blen + 1, slinkto);
    return file;
}

int flfile_cmp(const struct flfile *f1, const struct flfile *f2, int strict)
{
    register int cmprc;

    if ((cmprc = (f1->size - f2->size)) == 0)
        cmprc = f1->mode - f2->mode;

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


__inline__
static char *pkgfl_strdup(const char *str, size_t size)
{
    return obstack_copy0(&flalloct->ob, str, size);
}


int pkgfl_ent_cmp(const void *a,  const void *b) 
{
    const struct pkgfl_ent *aa = a;
    const struct pkgfl_ent *bb = b;
    return strcmp(aa->dirname, bb->dirname);
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
    }
    
    flent->dirname = dirnamep;
    flent->items = 0;
    return flent;
}


/*
  stores file list in files_tag in poldek's txt format
  RET: length of files_tag
 */
int pkgfl_asftag(tn_array *fl, char **ftag, int which)
{
    int i, j;
    int len = 0;
    char *dest, *p;
    
    for (i=0; i<n_array_size(fl); i++) {
        struct pkgfl_ent *flent = n_array_nth(fl, i);
        int dnl;

        dnl = strlen(flent->dirname);
        if (which == PKGFL_DEPDIRS) {
            if (!in_depdirs_l(flent->dirname, dnl))
                continue;
        } else if (which == PKGFL_NOTDEPDIRS) {
            if (in_depdirs_l(flent->dirname, dnl))
                continue;
        } 
        
        
        len += dnl + 2;
        
        for (j=0; j<flent->items; j++) {
            int bnl = strlen(flent->files[j]->basename);
            len += bnl;
            if (S_ISLNK(flent->files[j]->mode)) {
                len += strlen(flent->files[j]->basename + bnl + 1);
            }
            len += 256;
        }
    }
    
    p = dest = malloc(len + 1);
    *dest = '\0';

    for (i=0; i<n_array_size(fl); i++) {
        struct pkgfl_ent *flent = n_array_nth(fl, i);
        int dnl;

        dnl = strlen(flent->dirname);
        
        if (which == PKGFL_DEPDIRS) {
            if (!in_depdirs_l(flent->dirname, dnl))
                continue;
        } else if (which == PKGFL_NOTDEPDIRS) {
            if (in_depdirs_l(flent->dirname, dnl))
                continue;
        } 

        memcpy(p, flent->dirname, dnl + 1);
        p += dnl;
        *p++ = ' ';
        
        for (j=0; j<flent->items; j++) {
            struct flfile *file = flent->files[j];
            int bnl = strlen(file->basename);
            
            strcpy(p, file->basename);
            p += bnl;
            p += snprintf(p, 256, " %u %u", file->size, file->mode);

            if (S_ISLNK(file->mode)) {
                *p++ = ' ';
                strcpy(p, file->basename + bnl + 1);
                p += strlen(file->basename + bnl + 1);
            }
            
            if (j < flent->items - 1)
                *p++ = '|';
            else
                *p++ = ';';
        }
        n_assert(p - dest <=  len);
    }

    n_assert(p - dest <=  len);
    *p = '\0';
    *ftag = dest;
    
    return len;
}

__inline__ 
static int valid_fname(const char *fname, mode_t mode, const char *pkgname) 
{
    char *denychars = "\r\n\t |;";
    char *prdenychars = "\\r\\n\\t |;";

    if (strpbrk(fname, denychars)) {
        log_msg("%s: %s \"%s\" with whitespaces\n", pkgname, 
            S_ISDIR(mode) ? "dirname" : "filename", fname, prdenychars);
        return 0;
    }
    return 1;
}

/* -1 on error  */
int pkgfl_ldhdr(tn_array *fl, Header h, const char *pkgname) 
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
        log_msg("%s: DIRINDEXES (%d) != BASENAMES (%d) tag\n", c3, c1,
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

        if (!in_depdirs(dirs[i] + 1)) {
            msg(5, "skip files in dir %s\n", dirs[i]);
            skipdirs[i] = NULL;
            fentdirs[i] = NULL;
            continue;
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
        
        flent->files[flent->items++]= flfile;
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
