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

#include <trurl/trurl.h>

#include "i18n.h"
#include "log.h"
#include "pkgfl.h"
#include "pkg.h"
#include "fileindex.h"

#define obstack_chunk_alloc n_malloc
#define obstack_chunk_free  n_free

struct file_ent {
    struct flfile *flfile;
    struct pkg *pkg;
};


static int fent_cmp(const void *a,  const void *b) 
{
    const struct file_ent *aa = a;
    const struct file_ent *bb = b;
    return strcmp(aa->flfile->basename, bb->flfile->basename);
}

static int fent_cmp2str(const void *a,  const void *b)
{
    const struct file_ent *aa = a;
    return strcmp(aa->flfile->basename, (char*)b);
}

static int fent_cmp_aspkg(const void *a,  const void *b)
{
    const struct file_ent *aa = a;
    const struct file_ent *bb = b;
    return pkg_cmp_name_evr(aa->pkg, bb->pkg);
}

int file_index_init(struct file_index *fi, int nelem)  
{
    fi->dirs = n_hash_new(nelem, (tn_fn_free)n_array_free);
    if (fi->dirs == NULL)
       return 0;
    
    n_hash_ctl(fi->dirs, TN_HASH_NOCPKEY);
    obstack_init(&fi->obs);
    obstack_chunk_size(&fi->obs) = 1024*128;
    obstack_alignment_mask(&fi->obs) = 0;
    return 1;
}


void file_index_destroy(struct file_index *fi) 
{
    n_hash_free(fi->dirs);
    fi->dirs = NULL;
    
    obstack_free(&fi->obs, NULL); /* freeing whole obstack */
    memset(fi, 0, sizeof(*fi));
}


void *file_index_add_dirname(struct file_index *fi, const char *dirname)
{
    static tn_array *last_files = NULL;
    static const char *last_dirname = NULL;
    
    tn_array *files;

    if (last_files != NULL && strcmp(dirname, last_dirname) == 0) {
        DBGMSG("HIT dirname = %s %s\n", dirname, last_dirname);
        files = last_files;
        
    } else {
        /* find directory */
        if ((files = n_hash_get(fi->dirs, dirname)) == NULL) {
            files = n_array_new(4, NULL, fent_cmp);
            n_hash_insert(fi->dirs, dirname, files);
        }
        
        last_files = files;
        last_dirname = dirname;
    }
    
    return files;
}


int file_index_add_basename(struct file_index *fi, void *fidx_dir,
                            struct flfile *flfile,
                            struct pkg *pkg) 
{
    tn_array *files = fidx_dir; 
    struct file_ent *fent;

    fent = obstack_alloc(&fi->obs, sizeof(*fent));
    fent->flfile = flfile;
    fent->pkg = pkg;
    n_array_push(files, fent);
    return 1;
}

static 
int findfile(struct file_index *fi, const char *dirname, const char *basename,
             struct pkg *pkgs[], int size) 
{
    tn_array *files;
    struct file_ent *entp;
    int i = 1, n;
    
    if ((files = n_hash_get(fi->dirs, dirname)) == NULL)
        return 0;
    
    if ((n = n_array_bsearch_idx_ex(files, basename, fent_cmp2str)) == -1)
        return 0;
    

    entp = n_array_nth(files, n);
    pkgs[0] = entp->pkg;
    i = 1;
    n++;
    
    while (n < n_array_size(files)) {
        entp = n_array_nth(files, n++);
        if (strcmp(entp->flfile->basename, basename) != 0)
            break;
        
        pkgs[i++] = entp->pkg;
        if (i == size)
            break;
    }
    
    return i;
}


int file_index_lookup(struct file_index *fi, char *path,
                      struct pkg *pkgs[], int size)
{
    int n;
    char *tmpdirname, *dirname, buf[2] = {'\0', '\0'}, *basename;
    
    if (*path != '/') 
        return 0;

    path++;
    dirname = buf;
    
    n_basedirnam((char*)path, &tmpdirname, &basename);
    
    if (tmpdirname && tmpdirname != '\0')
        dirname = tmpdirname;
    else 
        *dirname = '/';

    n = findfile(fi, dirname, basename, pkgs, size);
    
    if (basename != path) 
        *(basename - 1) = '/';
    return n;
}


static void sort_files(const char *key, void *data) 
{
    key = key;    
    n_array_sort(data);
}


void file_index_setup(struct file_index *fi) 
{
    n_hash_map(fi->dirs, sort_files); 
}


static void find_dups(const char *dirname, void *data, void *ht) 
{
    struct file_ent *prev_ent, *ent;
    char path[PATH_MAX], *p;
    int i, was_eq, avsize;
    tn_array *dups;


    p = NULL;
    was_eq = 0;
    avsize = 0;
    prev_ent = n_array_nth(data, 0);
    for (i=1; i<n_array_size(data); i++) {
        
        ent = n_array_nth(data, i);
        
#if 0
        {
            int qq = 0;     
            if (strcmp(ent->flfile->basename, prev_ent->flfile->basename) == 0) {
                print_cnfl_pair(&qq, dirname,
                                1, 
                                ent->flfile->basename,
                                ent, prev_ent, 0, 0);
            }
        }
        
#endif        

        if (strcmp(ent->flfile->basename, prev_ent->flfile->basename) == 0 &&
            !pkg_has_pkgcnfl(ent->pkg, prev_ent->pkg) &&
            !pkg_has_pkgcnfl(prev_ent->pkg, ent->pkg))
         {
             if (p == NULL) {
                 n_strncpy(path, dirname, sizeof(path));
                 path[ sizeof(path) - 1 ] = '\0';
                 p = &path[ strlen(path) ];
                 *p++ = '/';
                 avsize = sizeof(path) - 1 - (p - path);
             }
             
                    
             n_strncpy(p, prev_ent->flfile->basename, avsize);
             if ((dups = n_hash_get(ht, path)) == NULL) {
                 dups = n_array_new(2, NULL, (tn_fn_cmp)fent_cmp_aspkg);
                 n_hash_insert(ht, path, dups);
             }
            
             if (n_array_bsearch(dups, (void*)prev_ent) == NULL) {
                 n_array_push(dups, prev_ent);
                 n_array_sort(dups);
             }

             if (i == n_array_size(data) - 1) {
                 if (n_array_bsearch(dups, (void*)ent) == NULL) {
                     n_array_push(dups, ent);
                     n_array_sort(dups);
                 }
             }
             was_eq = 1;
            
         } else {
             if (was_eq) {
                 if (p == NULL) {
                     n_strncpy(path, dirname, sizeof(path));
                     path[ sizeof(path) - 1 ] = '\0';
                     p = &path[ strlen(path) ];
                     *p++ = '/';
                     avsize = sizeof(path) - 1 - (p - path);
                 }
                 n_strncpy(p, prev_ent->flfile->basename, avsize);
                 dups = n_hash_get(ht, path);
                 n_assert(dups);
        
                 if (n_array_bsearch(dups, (void*)prev_ent) == NULL) {
                     n_array_push(dups, prev_ent);
                     n_array_sort(dups);
                 }
                 was_eq = 0;
             }
         }
        
        prev_ent = ent;
    }
}


static int register_file_conflict(struct pkg *pkg1, struct pkg *pkg2,
                                  int *added1, int *added2) 
{
    char c1 = '\0', c2 = '\0';
    
    if (pkg1->cnfls == NULL)
        pkg1->cnfls = capreq_arr_new(0);
    
    if (pkg2->cnfls == NULL) 
        pkg2->cnfls = capreq_arr_new(0);
    
    if (pkg_add_pkgcnfl(pkg1, pkg2, 1))
        c1 = '>';
    
    if (pkg_add_pkgcnfl(pkg2, pkg1, 1))
        c2 = '<';
#if 0
    if ((c1 || c2) && verbose > 1) {
        char buf[256];
        pkg_snprintf(buf, sizeof(buf), pkg1);
        msgn(3, "add cnfl: %s %c-%c %s", buf, c2 ? :' ', c1 ? :' ', 
            pkg_snprintf_s(pkg2));
    }
#endif
    *added1 = c1;
    *added2 = c2;
    return (c1 || c2);
}



static void print_cnfl_pair(int *pathprinted, const char *path,
                            int verblev, 
                            const char *prefix,
                            struct file_ent *ent1, struct file_ent *ent2,
                            int added1, int added2)
{
    if (*pathprinted == 0) {
        msg(verblev, "\nPath: %s%s\n", *path == '/' ? "" : "/", path);
        *pathprinted = 1;
    }

    msg(verblev, " %-5s %s(%c m%o s%d) %c-%c %s(%c m%o s%d)\n", prefix,
        pkg_snprintf_s(ent1->pkg), S_ISDIR(ent1->flfile->mode) ? 'D' : 'F',
        ent1->flfile->mode, ent1->flfile->size,
        added2 ? '<' : ' ', added1 ? '>' : ' ',
        pkg_snprintf_s0(ent2->pkg),
        S_ISDIR(ent2->flfile->mode) ? 'D' : 'F',
        ent2->flfile->mode, ent2->flfile->size);
    
#if 0    
    if (ent1->pkg->cnfls) {
        int i;
        printf("1 %d: (", pkg_has_pkgcnfl(ent1->pkg, ent2->pkg));
        
        for (i=0; i<n_array_size(ent1->pkg->cnfls); i++)
            printf("%s, ", capreq_snprintf_s(n_array_nth(ent1->pkg->cnfls, i)));
        printf(")\n");
    }

    if (ent2->pkg->cnfls) {
        int i;
        printf("2 %d: (", pkg_has_pkgcnfl(ent2->pkg, ent1->pkg));
        for (i=0; i<n_array_size(ent2->pkg->cnfls); i++)
            printf("%s, ", capreq_snprintf_s(n_array_nth(ent2->pkg->cnfls, i)));
        printf(")\n");
    }
#endif    

}

   
static
void process_dups(const char *path, tn_array *fents, void *strictp)
{
    int pathprinted = 0;
    int i, j;
    

    for (i=0; i<n_array_size(fents); i++) {
        struct file_ent *ent1 = n_array_nth(fents, i);
        for (j=i+1; j<n_array_size(fents); j++) {
            struct file_ent *ent2 = n_array_nth(fents, j);

            if (flfile_cnfl(ent1->flfile, ent2->flfile, *(int*)strictp) != 0) {
                int rc;
                int added1, added2;
                
                rc = register_file_conflict(ent1->pkg, ent2->pkg, &added1,
                                            &added2);
                if (rc && verbose > 1)
                    print_cnfl_pair(&pathprinted, path, 2, "cnfl", ent1, ent2,
                                    added1, added2);
                
            } else if (verbose > 2) {
                if (S_ISDIR(ent1->flfile->mode)) {
                    print_cnfl_pair(&pathprinted, path, 2, "shrdir", ent1,
                                    ent2, 0, 0);
                } else if (verbose > 2) {
                    print_cnfl_pair(&pathprinted, path, 2, "shrd", ent1, 
                                    ent2, 0, 0);
                }
            }
        }
    }
}


int file_index_find_conflicts(const struct file_index *fi, int strict)
{
    tn_hash *ht;

    ht = n_hash_new(103, (tn_fn_free)n_array_free);
    n_hash_map_arg(fi->dirs, find_dups, ht);
    
    n_hash_map_arg(ht, (void (*)(const char*,void*, void*))process_dups,
                   &strict);
    n_hash_free(ht);
    
    return 1;
}

