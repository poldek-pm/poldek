/*
  Copyright (C) 2000 - 2005 Pawel A. Gajda <mis@k2.net.pl>

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
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <trurl/trurl.h>

#include "i18n.h"
#include "log.h"
#include "pkgfl.h"
#include "pkg.h"
#include "capreq.h"
#include "fileindex.h"

struct file_ent {
    struct flfile *flfile;
    struct pkg *pkg;
};

#define FILE_CONFLICT_CNFL (1 << 0)
#define FILE_CONFLICT_SHRD (1 << 1)
#define FILE_CONFLICT_ADDED_LEFT  (1 << 4)
#define FILE_CONFLICT_ADDED_RIGTH (1 << 5)

struct file_conflict {
    int flags;
    struct file_ent e1;
    struct file_ent e2;
    char path[0];
};

static struct file_conflict *file_conflict_new(const char *path, int flags) 
{
    struct file_conflict *cnfl;
    int len;
    len = strlen(path);
    
    cnfl = n_malloc(sizeof(*cnfl) + len + 1);
    memset(cnfl, 0, sizeof(*cnfl));
    cnfl->flags = flags;
    memcpy(cnfl->path, path, len + 1);
    cnfl->flags = flags;
    return cnfl;
}

static void file_conflict_free(struct file_conflict *cnfl)
{
    free(cnfl->e1.flfile);
    pkg_free(cnfl->e1.pkg);

    free(cnfl->e2.flfile);
    pkg_free(cnfl->e2.pkg);
    free(cnfl);
}

static void add_file_conflict(tn_hash *cnflh, struct file_conflict *cnfl) 
{
    tn_array *cnfls;
    
    if ((cnfls = n_hash_get(cnflh, cnfl->path)) == NULL) {
        cnfls = n_array_new(8, (tn_fn_free)file_conflict_free, NULL);
        n_hash_insert(cnflh, cnfl->path, cnfls);
    }
    n_array_push(cnfls, cnfl);
}


struct map_struct {
    int strict;
    tn_hash *cnflh;
    int nfiles;
};

    
#if 0
static int register_file_conflict(struct pkg *pkg1, struct pkg *pkg2,
                                  int *added1, int *added2);

static
void print_cnfl_pair(int *pathprinted, const char *path,
                     int verblev, 
                     const char *prefix,
                     const struct file_ent *ent1,
                     const struct file_ent *ent2,
                     int added1, int added2);
#endif 

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


int file_index_init(struct file_index *fi, int nelem)  
{
    fi->dirs = n_hash_new_na(NULL, nelem, (tn_fn_free)n_array_free);
    if (fi->dirs == NULL)
       return 0;
    fi->cnflh = NULL;
    n_hash_ctl(fi->dirs, TN_HASH_NOCPKEY);
    fi->na = n_alloc_new(128, TN_ALLOC_OBSTACK);
    return 1;
}


void file_index_destroy(struct file_index *fi) 
{
    n_hash_free(fi->dirs);
    fi->dirs = NULL;
    n_alloc_free(fi->na);
    memset(fi, 0, sizeof(*fi));
}


void *file_index_add_dirname(struct file_index *fi, const char *dirname)
{
    static tn_array *last_files = NULL;
    static const char *last_dirname = NULL;
    
    tn_array *files;
    DBGF("%s\n", dirname);
    if (last_files != NULL && strcmp(dirname, last_dirname) == 0) {
        DBGF("HIT dirname = %s %s\n", dirname, last_dirname);
        files = last_files;
        
    } else {
        /* find directory */
        if ((files = n_hash_get(fi->dirs, dirname)) == NULL) {
            files = n_array_new(4, NULL, fent_cmp);
            n_hash_insert(fi->dirs, dirname, files);
        }
#if ENABLE_TRACE        
        if ((n_hash_size(fi->dirs) % 10) == 0) {
            printf("dupa: ");
            n_hash_stats(fi->dirs);
        }
#endif        
        last_files = files;
        last_dirname = dirname;
    }
    
    return files;
}

void file_index_setup_idxdir(void *files) 
{
    n_array_sort(files);
}


int file_index_add_basename(struct file_index *fi, void *fidx_dir,
                            struct flfile *flfile,
                            struct pkg *pkg) 
{
    tn_array *files = fidx_dir; 
    struct file_ent *fent;

    fent = fi->na->na_malloc(fi->na, sizeof(*fent));
    fent->flfile = flfile;
    fent->pkg = pkg;
    n_array_push(files, fent);
    //if (strstr(flfile->basename, "DOM.pm")) {
    //    printf("%p %d %s -> %s\n", files, n_array_size(files), pkg_snprintf_s(pkg), flfile->basename);
    //}
    
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

int file_index_remove(struct file_index *fi, const char *dirname,
                      const char *basename,
                      struct pkg *pkg) 
{
    tn_array *files;
    struct file_ent *entp;
    int n;
    
    if ((files = n_hash_get(fi->dirs, dirname)) == NULL)
        return 0;
    
    if ((n = n_array_bsearch_idx_ex(files, basename, fent_cmp2str)) == -1)
        return 0;

    entp = n_array_nth(files, n);
    if (pkg_cmp_name_evr(pkg, entp->pkg) == 0) {
        DBGF("%s/%s: %s\n", dirname, basename, pkg_snprintf_s(pkg));
        n_array_remove_nth(files, n);
        return 1;
    }
    
    n++;
    while (n < n_array_size(files)) {
        entp = n_array_nth(files, n);
        if (strcmp(entp->flfile->basename, basename) != 0)
            break;
            
        if (pkg_cmp_name_evr(pkg, entp->pkg) == 0) {
            DBGF("%s/%s: %s\n", dirname, basename, pkg_snprintf_s(pkg));
            n_array_remove_nth(files, n);
            return 1;
        }
        
        n++;
    }
    return 0;
}


int file_index_lookup(struct file_index *fi,
                      const char *apath, int apath_len, 
                      struct pkg *pkgs[], int size)
{
    char *tmpdirname, *dirname, buf[2] = {'\0', '\0'}, *basename, *path;
    
    if (*apath != '/') 
        return 0;

    if (apath_len == 0)
        apath_len = strlen(apath);
    apath_len++;
    path = alloca(apath_len);
    memcpy(path, apath, apath_len);
    
    path++;
    dirname = buf;
    
    n_basedirnam(path, &tmpdirname, &basename);
    
    if (tmpdirname && tmpdirname != '\0')
        dirname = tmpdirname;
    else 
        *dirname = '/';

    return findfile(fi, dirname, basename, pkgs, size);
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
    if ((c1 || c2) && verbose > 0) {
        char buf[256];
        pkg_snprintf(buf, sizeof(buf), pkg1);
        msgn(1, "add cnfl: %s %c-%c %s", buf, c2 ? :' ', c1 ? :' ', 
            pkg_snprintf_s(pkg2));
    }
#endif
    *added1 = c1;
    *added2 = c2;
    return (c1 || c2);
}

static
void process_dup(const char *path,
                 struct file_ent *ent1, struct file_ent *ent2,
                 struct map_struct *ms) 
{
    struct file_conflict *cnfl;
    
    if (flfile_cnfl(ent1->flfile, ent2->flfile, ms->strict) == 0) {
        cnfl = file_conflict_new(path, FILE_CONFLICT_SHRD);

        cnfl->e1.flfile = flfile_clone(ent1->flfile);
        cnfl->e1.pkg = pkg_link(ent1->pkg);
        cnfl->e2.flfile = flfile_clone(ent2->flfile);
        cnfl->e2.pkg = pkg_link(ent2->pkg);
        
        add_file_conflict(ms->cnflh, cnfl);
        
    } else {
        int rc, added1 = 0, added2 = 0;
        
        rc = register_file_conflict(ent1->pkg, ent2->pkg, &added1, &added2);
        if (rc && (added1 || added2)) {
            cnfl = file_conflict_new(path, FILE_CONFLICT_CNFL);

            cnfl->e1.flfile = flfile_clone(ent1->flfile);
            cnfl->e1.pkg = pkg_link(ent1->pkg);
            cnfl->e2.flfile = flfile_clone(ent2->flfile);
            cnfl->e2.pkg = pkg_link(ent2->pkg);
            
            if (added1)
                cnfl->flags |= FILE_CONFLICT_ADDED_LEFT;
            
            if (added2)
                cnfl->flags |= FILE_CONFLICT_ADDED_RIGTH;
            n_assert(cnfl->flags & (FILE_CONFLICT_ADDED_RIGTH | FILE_CONFLICT_ADDED_LEFT));
            
            add_file_conflict(ms->cnflh, cnfl);
        }
    }
}

static
void verify_dups(int from, int to, const char *path, tn_array *fents,
                 struct map_struct *ms)
{
    struct file_ent   *ent1, *ent2;
    int               i, j;
    
    for (i = from; i < to; i++) {
        ent1 = n_array_nth(fents, i);
        
        for (j = i + 1; j < to; j++) {
            ent2 = n_array_nth(fents, j);
            
            //n_assert(strcmp(ent1->flfile->basename, ent2->flfile->basename) == 0);
            if (pkg_has_pkgcnfl(ent1->pkg, ent2->pkg) || 
                pkg_has_pkgcnfl(ent2->pkg, ent1->pkg))
                continue;
            
            process_dup(path, ent1, ent2, ms);
        }
    }
}

static
void find_dups(const char *dirname, void *data, void *ms_) 
{
    struct file_ent *prev_ent, *ent;
    int i, ii, from;
    char path[PATH_MAX];
    struct map_struct *ms = ms_;
      
    prev_ent = n_array_nth(data, 0);
    from = 0;
    ms->nfiles += n_array_size(data);
    for (i=1; i < n_array_size(data); i++) {
        ent = n_array_nth(data, i);
        ii = i;
        while (strcmp(prev_ent->flfile->basename, ent->flfile->basename) == 0) {
            if (ii + 1 == n_array_size(data))
                break;
            ii++;
            ent = n_array_nth(data, ii);
        }

        if (ii != i) {
            snprintf(path, sizeof(path), "%s/%s", dirname,
                     prev_ent->flfile->basename);
            verify_dups(from, ii, path, data, ms);
        }
        
        prev_ent = ent;
        from = ii;
        i = ii;
    }
}

int file_index_find_conflicts(const struct file_index *fi, int strict)
{
    struct map_struct ms;
    ms.strict = strict;
    ms.nfiles = 0;
    ms.cnflh = n_hash_new(64, (tn_fn_free)n_array_free);
    n_hash_ctl(ms.cnflh, TN_HASH_NOCPKEY);
    n_hash_map_arg(fi->dirs, find_dups, &ms);
    
    ((struct file_index*)fi)->cnflh = ms.cnflh;
    DBGF("%d dirnames, %d files\n", n_hash_size(fi->dirs), ms.nfiles);
    return 1;
}

static
void file_conflict_print(struct file_conflict *cnfl)
{
    char *prefix = "cnfl";
    
    if (cnfl->flags & FILE_CONFLICT_SHRD)
        prefix = "shr";

    //n_assert(cnfl->flags & (FILE_CONFLICT_ADDED_RIGTH | FILE_CONFLICT_ADDED_LEFT));
    
    msg(0, " %-5s %s(%c m%o s%d) %c-%c %s(%c m%o s%d)\n", prefix,
        pkg_snprintf_s(cnfl->e1.pkg),
        S_ISDIR(cnfl->e1.flfile->mode) ? 'D' : 'F',
        cnfl->e1.flfile->mode, cnfl->e1.flfile->size,
        (cnfl->flags & FILE_CONFLICT_ADDED_LEFT) ? '<' : ' ',
        (cnfl->flags & FILE_CONFLICT_ADDED_RIGTH) ? '>' : ' ',
        pkg_snprintf_s0(cnfl->e2.pkg),
        S_ISDIR(cnfl->e2.flfile->mode) ? 'D' : 'F',
        cnfl->e2.flfile->mode, cnfl->e2.flfile->size);
    
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


int file_index_report_conflicts(const struct file_index *fi, tn_array *pkgs)
{
    tn_array *paths;
    int i, j, nconflicts = 0;
    
    paths = n_hash_keys(fi->cnflh);
    n_array_sort(paths);
    
    for (i=0; i < n_array_size(paths); i++) {
        tn_array *conflicts;
        int pathprinted = 0;
        char *path;

        path = n_array_nth(paths, i);
        conflicts = n_hash_get(fi->cnflh, path);
        for (j = 0; j < n_array_size(conflicts); j++) {
            struct file_conflict *cnfl = n_array_nth(conflicts, j);
            if (pkgs) {
                if (!n_array_bsearch(pkgs, cnfl->e1.pkg) ||
                    !n_array_bsearch(pkgs, cnfl->e1.pkg))
                    continue;
            }
            if (pathprinted == 0) {
                msgn(0, "\nPath: %s%s", *cnfl->path == '/' ? "" : "/",
                     cnfl->path);
                pathprinted = 1;
            }

            file_conflict_print(cnfl);
            nconflicts++;
        }
    }
    
    msgn(0, "%d file conflicts found", nconflicts);
    return nconflicts;
}
