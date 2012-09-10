/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <stdlib.h>
#include <string.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <trurl/trurl.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "pkgfl.h"
#include "pkgmisc.h"
#include "pkg.h"
#include "capreq.h"
#include "fileindex.h"
#include "pkgset-req.h"

extern int poldek_conf_MULTILIB;

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


struct file_index *file_index_new(int nelem)  
{
    tn_alloc *na;
    struct file_index *fi;

    na = n_alloc_new(32, TN_ALLOC_OBSTACK);

    fi = na->na_malloc(na, sizeof(*fi));
    memset(fi, 0, sizeof(*fi));
    
    fi->dirs = n_hash_new_na(na, nelem, (tn_fn_free)n_array_free);
    n_hash_ctl(fi->dirs, TN_HASH_NOCPKEY);
    
    fi->cnflh = NULL;
    fi->na = na;
    return fi;
}

void file_index_free(struct file_index *fi) 
{
    n_hash_free(fi->dirs);
    fi->dirs = NULL;
    if (fi->cnflh)
        n_hash_free(fi->cnflh);
    n_alloc_free(fi->na);
}

void *file_index_add_dirname(struct file_index *fi, const char *dirname)
{
    tn_array *files;
    int klen = 0;
    unsigned khash = 0;
    
    DBGF("%s\n", dirname);

    if ((files = n_hash_get_ex(fi->dirs, dirname, &klen, &khash)) == NULL) {
        files = n_array_new(4, NULL, fent_cmp);
        n_hash_insert_ex(fi->dirs, dirname, klen, khash, files);
    }
#if ENABLE_TRACE        
    if ((n_hash_size(fi->dirs) % 10) == 0) {
        DBGF("stats\n");
        n_hash_stats(fi->dirs);
    }
#endif

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
int findfile(const struct file_index *fi,
             const char *dirname, const char *basename,
             struct pkg *pkgs[], int size) 
{
    tn_array *files;
    struct file_ent *entp;
    int i = 1, n;

    n_assert(size > 0);
    
    if ((files = n_hash_get(fi->dirs, dirname)) == NULL) {
        DBGF("%s: directory not found\n", dirname);
        return 0;
    }
        
    
    if ((n = n_array_bsearch_idx_ex(files, basename, fent_cmp2str)) == -1) {
        DBGF("%s/%s: file not found\n", dirname, basename);
        return 0;
    }
    
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


int file_index_lookup(const struct file_index *fi,
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

    if (*path == '/')
        path++;          /* skip '/' */
    dirname = buf;
    
    n_basedirnam(path, &tmpdirname, &basename);
    
    if (tmpdirname && *tmpdirname != '\0')
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
        msgn(1, _("add cnfl: %s %c-%c %s"), buf, c2 ? :' ', c1 ? :' ', 
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
        DBGF("add conflict between %s and %s based on %s\n",
             pkg_id(ent1->pkg), pkg_id(ent2->pkg), path);

        /* do not add conflicts in multilib mode, file colors are used (seems to) */
        if (poldek_conf_MULTILIB) 
            rc = 1;
        else
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
                msgn(0, _("\nPath: %s%s"), *cnfl->path == '/' ? "" : "/",
                     cnfl->path);
                pathprinted = 1;
            }

            file_conflict_print(cnfl);
            nconflicts++;
        }
    }
    n_array_free(paths);
    msgn(0, _("%d file conflicts found"), nconflicts);
    return nconflicts;
}

static tn_array *get_pkg_dirs(struct pkg *pkg) 
{
    tn_hash *dirh;
    tn_array *dirs;
    int i;
        
    dirh = n_hash_new(3 * n_tuple_size(pkg->fl), NULL);
    for (i=0; i < n_tuple_size(pkg->fl); i++) {
        struct pkgfl_ent *flent = n_tuple_nth(pkg->fl, i);
        char tmpbuf[PATH_MAX], *p, *q;

        if (*flent->dirname == '/') /* / */
            continue;

        n_snprintf(tmpbuf, sizeof(tmpbuf), "/%s/", flent->dirname);
        q = tmpbuf;
        while ((p = strrchr(tmpbuf, '/')) && p != tmpbuf) {
            *p = '\0';
            if (!n_hash_exists(dirh, tmpbuf))
                n_hash_insert(dirh, tmpbuf, NULL);
        }
    }
    
    dirs = n_hash_keys_cp(dirh);
    n_hash_free(dirh);
    return dirs;
}


int file_index_report_orphans(const struct file_index *fi, tn_array *pkgs)
{
    struct pkg *result[2048];
    tn_array   *paths;
    tn_hash    *orphanh, *missreqh, *is_path_to_cache;
    int        i, j, norphans = 0, modv = 0;
    
    orphanh  = n_hash_new(n_array_size(pkgs)/100, (tn_fn_free)n_hash_free);
    missreqh = n_hash_new(n_array_size(pkgs), (tn_fn_free)n_array_free);
    is_path_to_cache = n_hash_new(n_array_size(pkgs)/100, NULL);

    if (n_array_size(pkgs) > 100) {
        modv = n_array_size(pkgs) / 100.0;
        modv = modv > 0 ? modv : 1;
    }
    
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg;
        tn_array *dirs;
        
        pkg = n_array_nth(pkgs, i);

        if (modv && i % modv == 0)
            msg_tty(1, "\r%.1lf%% done",
                    ((float)i / n_array_size(pkgs)) * 100.0);

        if (pkg->fl == NULL)
            continue;

        dirs = get_pkg_dirs(pkg);
        n_array_sort(dirs);
        
        for (j=0; j < n_array_size(dirs); j++) {
            char *dir = n_array_nth(dirs, j);
            int nfound;
            
            if (n_hash_exists(orphanh, dir)) {
                tn_hash *opkgh = n_hash_get(orphanh, dir);
                n_hash_replace(opkgh, pkg_snprintf_s0(pkg), pkg);
                continue;
            }
                
            nfound = file_index_lookup(fi, dir, strlen(dir), result, 2048);
            if (nfound == 0) {
                tn_hash *opkgh = n_hash_new(128, NULL);
                n_hash_insert(opkgh, pkg_id(pkg), pkg);
                n_hash_insert(orphanh, dir, opkgh);
            }
        }
        n_array_free(dirs);
    }
    if (modv)
        msg_tty(1, "\r          \r");
    
    paths = n_hash_keys(orphanh);
    n_array_sort(paths);

    for (i=0; i < n_array_size(paths); i++) {
        char *path = n_array_nth(paths, i);
        char pkgstr[PATH_MAX];
        tn_array *opkgs;
        int n = 0;
            
        opkgs = n_hash_keys(n_hash_get(orphanh, path));
        n_array_sort(opkgs);
        
        for (j=0; j < n_array_size(opkgs) && j < 5; j++)
            n += n_snprintf(&pkgstr[n], sizeof(pkgstr) - n,
                            "%s%s", (char*)n_array_nth(opkgs, j),
                            j < n_array_size(opkgs) - 1 ? ", " : "");
        
        if (n_array_size(opkgs) > 5)
            n += n_snprintf(&pkgstr[n], sizeof(pkgstr) - n,
                            _("[%d packages left]"), n_array_size(opkgs) - 5);
        logn(LOGERR, _("%s: orphaned directory from %s"), path, pkgstr);
    }
    norphans = n_array_size(paths);
    msgn(0, _("%d orphaned directories found"), norphans);
    n_array_free(paths);
    n_hash_free(orphanh);

    return norphans;
}


static
int is_path_to(tn_hash *is_path_to_cache, 
               struct pkgmark_set *pms,
               struct pkg *dest, struct pkg *pkg,
               int deep)
{
    int i;

    if (pkg_isset_mf(pms, pkg, PKGMARK_BLACK)) /* was there? */
        return 0;
    
    msgn_i(2, deep, "%s", pkg_id(pkg));
    deep += 2;

    pkg_set_mf(pms, pkg, PKGMARK_BLACK); /* was there */
    if (pkg->reqpkgs == NULL)
        return 0;

    for (i=0; i<n_array_size(pkg->reqpkgs); i++) {
        struct reqpkg *rpkg = n_array_nth(pkg->reqpkgs, i);
        char key[PATH_MAX];
        int yes;
        
        if (rpkg->pkg == dest)
            return 1;

        if (rpkg->pkg->reqpkgs == NULL)
            continue;

        n_snprintf(key, sizeof(key), "%s -> %s", pkg_id(rpkg->pkg), pkg_id(dest));
        if (n_hash_get(is_path_to_cache, key)) {
            DBGF("cached2 %s\n", key);
            return 1;
        }
        
        yes = is_path_to(is_path_to_cache, pms, dest, rpkg->pkg, deep);
#if 0       /* faster, but needs a lot of memory */
        n_hash_replace(is_path_to_cache, key, yes ? pkg : NULL);
#endif        
        if (yes)
            return 1;
    }
    
    return 0;
}

/* is any from ptab[] is required by pkg? */
static int is_required(tn_hash *is_path_to_cache, 
                       struct pkg *pkg, const char *path,
                       struct pkg *ptab[], int size) 
{
    struct pkgmark_set *pms;
    int i, yes = 0;
    
    for (i=0; i < size; i++) {
        if (ptab[i] == pkg) 
            return 1;
    }
    
    if (pkg->reqpkgs == NULL)
        return 0;

    pms = pkgmark_set_new(0, PKGMARK_SET_IDPTR);
    for (i=0; i < size; i++) {
        char key[PATH_MAX];
        
        msgn(2, _("Looking for path %s -> %s (%s)"), pkg_id(pkg), pkg_id(ptab[i]),
             path);

        n_snprintf(key, sizeof(key), "%s -> %s", pkg_id(pkg), pkg_id(ptab[i]));
        
        if (n_hash_exists(is_path_to_cache, key)) {
            yes = n_hash_get(is_path_to_cache, key) != NULL;
            break;
        }
        
        pkgmark_massset(pms, 0, PKGMARK_BLACK);
        yes = is_path_to(is_path_to_cache, pms, ptab[i], pkg, 0);
        n_hash_replace(is_path_to_cache, key, yes ? pkg : NULL);
        
        if (yes)
            break;
    }
    
    pkgmark_set_free(pms);
    return yes;
}

struct missing_req {
    char   path[PATH_MAX];
    int    ncandidates;
    struct pkg *candidates[0];
};

static
struct missing_req *missing_req_new(const char *path,
                                    struct pkg *ptab[], int size)
{
    struct missing_req *mreq;

    mreq = n_malloc(sizeof(*mreq) + size * sizeof(struct pkg*));
    n_snprintf(mreq->path, sizeof(mreq->path), "%s", path);
    mreq->ncandidates = size;
    memcpy(mreq->candidates, ptab, size * sizeof(struct pkg*));
    return mreq;
}

int file_index_report_semiorphans(const struct file_index *fi, tn_array *pkgs)
{
    struct pkg *result[2048];
    tn_array   *pkgids;
    tn_hash    *missreqh, *is_path_to_cache;
    int        i, j, norphans = 0, modv = 0;
    
    missreqh = n_hash_new(n_array_size(pkgs), (tn_fn_free)n_array_free);
    is_path_to_cache = n_hash_new(n_array_size(pkgs)/100, NULL);

    if (n_array_size(pkgs) > 50) {
        modv = n_array_size(pkgs) / 200.0;
        modv = modv > 0 ? modv : 1;
    }
    
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg;
        tn_array *dirs;
        
        pkg = n_array_nth(pkgs, i);
        if (modv && i % modv == 0)
            msg_tty(1, "\r%.1lf%% done",
                    ((float)i / n_array_size(pkgs)) * 100.0);
#if ENABLE_TRACE
        if (i % 100 == 0)
            DBGF("size = %d\n", n_hash_size(is_path_to_cache));
#endif        
        
        if (pkg->fl == NULL)
            continue;

        dirs = get_pkg_dirs(pkg);
        n_array_sort(dirs);
        
        DBGF("dirs %d\n", n_array_size(dirs));
        for (j=0; j < n_array_size(dirs); j++) {
            char *dir = n_array_nth(dirs, j);
            int nfound;
            
            nfound = file_index_lookup(fi, dir, strlen(dir), result, 2048);
            if (nfound == 0)    /* orphaned */
                continue;
            
            if (!is_required(is_path_to_cache, pkg, dir, result, nfound)) {
                tn_array *mreqarr;
                if ((mreqarr = n_hash_get(missreqh, pkg_id(pkg))) == NULL) {
                    mreqarr = n_array_new(128, free, NULL);
                    n_hash_insert(missreqh, pkg_id(pkg), mreqarr);
                }
                n_array_push(mreqarr, missing_req_new(dir, result, nfound));
            }
        }
        n_array_free(dirs);
    }
    n_hash_free(is_path_to_cache);
    
    if (modv)
        msg_tty(1, "\r          \r");
    
    norphans = 0;
    pkgids = n_hash_keys(missreqh);

    n_array_sort(pkgids);
    for (i=0; i < n_array_size(pkgids); i++) {
        char *id = n_array_nth(pkgids, i);
        tn_array *mreqarr = n_hash_get(missreqh, id);

        for (j=0; j < n_array_size(mreqarr); j++) {
            struct missing_req *mreq = n_array_nth(mreqarr, j);
            char pkgstr[PATH_MAX];
            int k, n = 0;
            
            for (k=0; k < mreq->ncandidates && k < 3; k++)
                n += n_snprintf(&pkgstr[n], sizeof(pkgstr) - n,
                                "%s%s", mreq->candidates[k]->name,
                                k < mreq->ncandidates - 1 ? "/" : "");
        
            if (mreq->ncandidates > 3)
                n += n_snprintf(&pkgstr[n], sizeof(pkgstr) - n,
                                "...", mreq->ncandidates - 3);
        
            logn(LOGERR, _("%s: %s: directory not in required packages "
                 "(missing Requires: %s?)"), id, mreq->path, pkgstr);
            norphans++;
        }
    }
    msgn(0, _("%d semi-orphaned directories found"), norphans);
    n_array_free(pkgids);
    n_hash_free(missreqh);
    return norphans;
}

