/*
  Copyright (C) 2000 - 2006 Pawel A. Gajda <mis@k2.net.pl>

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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>

#include <tndb/tndb.h>
#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>
#include <trurl/nmalloc.h>
#include <trurl/ntuple.h>

#include <vfile/vfile.h>

#include "i18n.h"
#include "log.h"
#include "pkgdir.h"
#include "pkgdir_intern.h"
#include "pkg.h"
#include "capreq.h"
#include "pkgu.h"
#include "pkgroup.h"
#include "pkgfl.h"
#include "misc.h"
#include "pndir/pndir.h"        /* for pndir_make_pkgkey() */
#include "pkgdir_dirindex.h"

#define KEY_REQDIR '.'
#define KEY_PKGID  'i'

struct pkgdir_dirindex {
    struct tndb *db;
    tn_alloc *na;
    tn_hash *idmap;
};

/* key prefixed by KEY_REQDIR */
static int req_pkgkey(char *key, int size, const struct pkg *pkg)
{
    n_assert(size >= UINT8_MAX);

    key[0] = '_';
    key[1] = KEY_REQDIR;
    return pndir_make_pkgkey(&key[2], size - 2, pkg) + 2;
}

static tn_buf *dirarray_join(tn_buf *nbuf, tn_array *arr, char *sep)
{
    int i;
    for (i=0; i < n_array_size(arr); i++) {
        n_buf_printf(nbuf, "%s%s", (char*)n_array_nth(arr, i),
                     i < n_array_size(arr) - 1 ? sep : "");
    }
    return nbuf;
}

static
int nth2str(char *buf, int size, uint32_t nth)
{
    return n_snprintf(buf, size, "_%c%u", KEY_PKGID, nth);
}

/* store nth => pkg pair  */
static
void index_nth(uint32_t nth, struct pkg *pkg, struct tndb *db)
{
    char key[512], val[512];
    int klen, vlen;
    
    vlen = pndir_make_pkgkey(val, sizeof(val), pkg);
    klen = nth2str(key, sizeof(key), nth);
    tndb_put(db, key, klen, val, vlen);
}

/* build hash of path => ids[] */
static
void add_to_hash(tn_hash *ht, const char *path, uint32_t nth)
{
    char val[512];
    int vlen;
    tn_array *keys;
    
    if ((keys = n_hash_get(ht, path)) == NULL) {
        keys = n_array_new(16, free, (tn_fn_cmp)strcmp);
        n_hash_insert(ht, path, keys);
    }
    vlen = nth2str(val, sizeof(val), nth);
    //printf("add %s: %s\n", path, val);
    
    n_array_push(keys, n_strdupl(val, vlen));
}


#if 0                           /* XXX not used */
static
void index_package_allfiles(uint32_t nth, struct pkg *pkg, struct tndb *db,
                            tn_hash *index)
{
    struct pkgflist *flist;
    int i, j;

    index_nth(nth, pkg, db);
    
    if ((flist = pkg_get_flist(pkg)) == NULL)
        return;

    for (i=0; i < n_tuple_size(flist->fl); i++) {
        struct pkgfl_ent *flent = n_tuple_nth(flist->fl, i);
        
        for (j=0; j < flent->items; j++) {
            struct flfile *f = flent->files[j];
            char buf[1024], *slash = "";
            int n;
            
                
            if (S_ISDIR(f->mode)) {
                if (*flent->dirname != '/')
                    slash = "/";
            }
            
            n = n_snprintf(buf, sizeof(buf), "%s%s%s%s%s",
                           *flent->dirname == '/' ? "":"/",
                           flent->dirname,
                           *flent->dirname == '/' ? "":"/",
                           f->basename, slash);
            n_assert(n < UINT8_MAX);
            add_to_hash(index, buf, nth);
        }
    }
    pkgflist_free(flist);
}
#endif

static
void index_package(uint32_t nth, struct pkg *pkg, struct tndb *db,
                   tn_hash *index, tn_buf *nbuf)
{
    tn_array *required = NULL, *owned = NULL;
    struct pkgflist *flist;
    int i;

    if ((flist = pkg_get_flist(pkg)) == NULL)
        return;

    //if (strcmp(pkg_id(pkg), "arachne-common-1.66b-2") != 0)
    //   return;
    
    if (pkgfl_owned_and_required_dirs(flist->fl, &owned, &required) == 0) {
        DBGF("%s: NULL\n", pkg_id(pkg));
        pkgflist_free(flist);
        return;
    }
    DBGF("%s: %d %d\n", pkg_id(pkg), owned ? n_array_size(owned): -1,
         required ? n_array_size(required): -1);
    
    if (owned) {
        for (i=0; i < n_array_size(owned); i++) {
            const char *dir = n_array_nth(owned, i);
            add_to_hash(index, dir, nth);
        }
        n_array_free(owned);
    }
    
    if (required) {
        if (n_array_size(required)) {
            char key[512];
            int klen;
            
            klen = req_pkgkey(key, sizeof(key), pkg);
            n_buf_clean(nbuf);
            n_buf_printf(nbuf, "/");
            nbuf = dirarray_join(nbuf, required, ":/");
            tndb_put(db, key, klen, n_buf_ptr(nbuf), n_buf_size(nbuf));
        }
        n_array_free(required);
    }
    
    pkgflist_free(flist);
}


static
int do_pkgdir_dirindex_create(struct pkgdir *pkgdir, const char *path)
{
    struct tndb   *db;
    tn_buf        *nbuf;
    tn_hash       *index;
    tn_array      *paths;
    tn_alloc      *na;
    struct vflock *lock;
    int           i;
    char          *tmp, *dir;

    if (n_array_size(pkgdir->pkgs) == 0)
        return 1;
    
    MEMINF("START");

    msgn(2, "Creating directory index of %s...", pkgdir_idstr(pkgdir));

    n_strdupap(path, &tmp);
    dir = n_dirname(tmp);

    DBGF("mkdir %s %s\n", path, dir);
    
    if ((lock = vf_lock_mkdir(dir)) == NULL)
        return 0;

    db = tndb_creat(path, 0, TNDB_SIGN_DIGEST);

    if (db == NULL) {
        logn(LOGERR, "%s: open failed (%m)\n", path);
        vf_lock_release(lock);
		return 0;
    }

    na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    index = n_hash_new_na(na, n_array_size(pkgdir->pkgs) * 16, (tn_fn_free)n_array_free);
    nbuf = n_buf_new(1024 * 16);

    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
        index_nth(i, pkg, db);
    }
    
    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);

        index_package(i, pkg, db, index, nbuf);
        
        if (i % 1000 == 0) {
            MEMINF("%d packages", i);
        }
    }

    /* store index to db */
    paths = n_hash_keys(index);
    msgn(3, "  saving %d paths\n", n_array_size(paths));
     
    for (i=0; i < n_array_size(paths); i++) {
        const char *path = n_array_nth(paths, i);
        tn_array *ids = n_hash_get(index, path);

        n_buf_clean(nbuf);
        nbuf = dirarray_join(nbuf, ids, ":");
        DBGF("%s %s\n", path,  n_buf_ptr(nbuf));
        
        tndb_put(db, path, strlen(path), n_buf_ptr(nbuf), n_buf_size(nbuf));
    }

    n_array_free(paths);
    n_buf_free(nbuf);
    n_hash_free(index);
    n_alloc_free(na);
    tndb_close(db);
    vf_lock_release(lock);
    
    MEMINF("END");
    return 1;
}

static int dirindex_path(char *path, int size, struct pkgdir *pkgdir)
{
    char tmp[PATH_MAX], tmp2[PATH_MAX];
    char *ofpath;
    int n;
    
    n = n_snprintf(tmp, sizeof(tmp), "%s", pkgdir_localidxpath(pkgdir));
    n_assert(n > 0);

    ofpath = tmp;
    if (ofpath[n - 1] == '/') {    /* directory */
        ofpath[n - 1] = '\0';
        
    } else if (!is_dir(ofpath)) { /* not directory? */
        char *dn = n_dirname(ofpath);
        ofpath = dn;
    }

    n_snprintf(tmp2, sizeof(tmp2), "%s/dirindex-of-%s.tndb", ofpath, pkgdir->type);
    n_snprintf(tmp2, sizeof(tmp2), "%s", ofpath);
    DBGF("path = %s\n", ofpath);
    n = vf_cachepath(path, size, ofpath);
    DBGF("cache path = %s\n", path);

    n_assert(n > 0);
    n += n_snprintf(&path[n], size - n, "/dirindex-of-%s.tndb", pkgdir->type);
    DBGF("result = %s\n", path);
    n_assert(n > 0);
    
    return n;
}


int pkgdir_dirindex_create(struct pkgdir *pkgdir)
{
    char path[1024];
    time_t mtime;

    dirindex_path(path, sizeof(path), pkgdir);
    
    mtime = poldek_util_mtime(path);
    n_assert(pkgdir->ts);
    
    if (mtime == 0 || mtime < pkgdir->ts) {
        if (do_pkgdir_dirindex_create(pkgdir, path)) {
            struct utimbuf ut;
            ut.actime = ut.modtime = pkgdir->ts;
            utime(path, &ut);
        }
    }
    
    return 1;
}

void pkgdir_dirindex_close(struct pkgdir_dirindex *dirindex)
{
    tndb_close(dirindex->db);
    n_hash_free(dirindex->idmap);
    n_alloc_free(dirindex->na);
    /* no free(dirindex), it is allocated by na */
}


static tn_hash *load_ids(struct tndb *db, int npackages) 
{
    struct tndb_it  it;
    char            key[TNDB_KEY_MAX + 1], *val = NULL;
    unsigned        nerr = 0, klen, vlen, vlen_max;
    tn_alloc        *na;
    tn_hash         *keymap;


    if (!tndb_it_start(db, &it))
        return NULL;
    
    na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    keymap = n_hash_new_na(na, 2 * npackages, NULL);
    
    vlen = vlen_max = 256;
    val = n_malloc(vlen);

    if (!tndb_it_rget(&it, key, &klen, (void**)&val, &vlen)) {
        logn(LOGERR, _("%s: invalid directory index"), tndb_path(db));
        nerr++;
        goto l_end;
    }

    while (*key == '_' && *(key + 1) == KEY_PKGID) {
        char *id;
        
        val[vlen] = '\0';

        id = na->na_malloc(na, klen);
        memcpy(id, key + 2, klen); /* key + 2 => skipping _KEY_PKGID */
        
        n_hash_replace(keymap, val, id);

        if (vlen < vlen_max)   /* to avoid needless tndb_it_rget()'s reallocs */
            vlen = vlen_max;
        else
            vlen_max = vlen;
        
        if (!tndb_it_rget(&it, key, &klen, (void**)&val, &vlen)) {
            logn(LOGERR, _("%s: invalid directory index"), tndb_path(db));
            nerr++;
            goto l_end;
        }
    }
    
l_end:
    free(val);
    n_alloc_free(na);

    if (nerr) {
        n_hash_free(keymap);
        keymap = NULL;
    }
    
    return keymap;
}

static
const char **get_req_directories(struct tndb *db, char *key, int klen,
                                 char *val, int vsize, int *n);
    
struct pkgdir_dirindex *pkgdir_dirindex_open(struct pkgdir *pkgdir)
{
    struct tndb     *db;
    char            path[PATH_MAX];
    int             i, rc = 0;
    tn_alloc        *na = NULL;
    tn_hash         *idmap = NULL, *keymap = NULL;
    struct pkgdir_dirindex *dirindex = NULL;

    n_assert(n_array_size(pkgdir->pkgs)); /* XXX: tndb w/o */
             
    dirindex_path(path, sizeof(path), pkgdir);
    
    msgn(2, "Opening directory index of %s...", pkgdir_idstr(pkgdir));
    MEMINF("start");

    rc = 0;
    if ((db = tndb_open(path)) == NULL) {
        logn(LOGERR, "%s: open failed", path);
        goto l_end;
    }

    if (!tndb_verify(db)) {
        logn(LOGERR, _("%s: broken directory index"), path);
        goto l_end;
    }

    MEMINF("opened");
    
    if ((keymap = load_ids(db, n_array_size(pkgdir->pkgs))) == NULL)
        goto l_end;
    

    MEMINF("keymap");
    
    na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    idmap = n_hash_new_na(na, n_array_size(pkgdir->pkgs), (tn_fn_free)pkg_free);
    
    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        struct pkg   *pkg = n_array_nth(pkgdir->pkgs, i);
        const char   **tl, **tl_save;
        char         key[512], *id, val[1024 * 4];
        int          klen, n = 0;

        klen = pndir_make_pkgkey(&key[2], sizeof(key), pkg);
        id = n_hash_get(keymap, &key[2]);

        if (id)
            n_hash_replace(idmap, id, pkg_link(pkg));
        else {
            logn(LOGERR, _("%s: outdated directory index"), path);
            goto l_end;
        }
        
        key[0] = '_';
        key[1] = KEY_REQDIR;

        tl = tl_save = get_req_directories(db, key, klen + 2,
                                           val, sizeof(val), &n);
        if (tl == NULL || n == 0) {
            if (tl)
                n_str_tokl_free(tl);
            continue;
        }
        
        if (!pkg->reqs)
            pkg->reqs = capreq_arr_new(n);
        
        while (*tl) {
            const char *dir = *tl;
            
            if (*dir && !n_array_bsearch_ex(pkg->reqs, dir,
                                            (tn_fn_cmp)capreq_cmp2name)) {
                
                struct capreq *req = capreq_new(pkg->na, dir, 0, NULL, NULL, 0,
                                                CAPREQ_BASTARD);
                n_array_push(pkg->reqs, req);
            }
            tl++;
        }
        n_str_tokl_free(tl_save);
    }
    
    rc = 1;                     /* success */
l_end:
    if (keymap)
        n_hash_free(keymap);

    if (rc) {                   /* OK */
        dirindex = na->na_malloc(na, sizeof(*dirindex));
        dirindex->db = db;
        dirindex->na = na;
        dirindex->idmap = idmap;
        
    } else {                    /* ERR */
        if (db == NULL) {
            vf_unlink(path);
        } else {
            tndb_unlink(db);
            tndb_close(db);
        }
        
        if (idmap)
            n_hash_free(idmap);
        
        if (na)
            n_alloc_free(na);
    }

    MEMINF("end");
    
    return dirindex;
}

static
const char **get_req_directories(struct tndb *db, char *key, int klen,
                                 char *val, int vsize, int *n)
{
    int         vlen;
    
    if ((vlen = tndb_get(db, key, klen, val, vsize)) == 0)
        return NULL;

    n_assert(vlen < vsize);
    val[vlen] = '\0';
    return n_str_tokl_n(val, ":", n);
}

tn_array *pkgdir_dirindex_get_reqdirs(const struct pkgdir_dirindex *dirindex,
                                      const struct pkg *pkg)
{
    const char  **tl, **tl_save;
    char        key[512], val[1024 * 4];
    int         klen, n = 0;
    tn_array    *dirs;
    
    
    klen = req_pkgkey(key, sizeof(key), pkg);
    tl = tl_save = get_req_directories(dirindex->db, key, klen, val, sizeof(val), &n);

    if (tl == NULL || n == 0)
        return NULL;
        
    dirs = n_array_new(n, free, (tn_fn_cmp)strcmp);
    while (*tl) {
        if (**tl) 
            n_array_push(dirs, n_strdup(*tl));
        tl++;
    }

    n_str_tokl_free(tl_save);

    if (n_array_size(dirs) == 0)
        n_array_cfree(&dirs);
    
    return dirs;
}

static
int do_pkgdir_dirindex_get(const struct pkgdir_dirindex *dirindex,
                           tn_array **pkgs_ptr, const struct pkg *pkg,
                           const char *path)
{
    const char    **tl, **tl_save;
    tn_array      *pkgs = NULL;
    unsigned char val[8192];
    int           n, found;
    
#if DEVEL    
    static int  xx = 0;
#endif    
    
    if (*path == '/')
        path++;
    
    if (!tndb_get_str(dirindex->db, path, val, sizeof(val))) {
        return 0;
    }
    
    tl = tl_save = n_str_tokl_n(val, ":", &n);

    DBGF("%s: FOUND %d %p\n", path, n, pkgs_ptr ? *pkgs_ptr : NULL);
    
    if (n) {
        if (pkgs_ptr)
            pkgs = *pkgs_ptr;
        
        if (pkgs == NULL)
            pkgs = pkgs_array_new(n);

        while (*tl) {
            const char *id = *tl;
            struct pkg *p;
        
            tl++;

            if (*id == '\0')
                continue;
        
            id += 2;            /* skipping _KEY_PKGID */
            if ((p = n_hash_get(dirindex->idmap, id)) == NULL)
                continue;
            
            if (pkgs) 
                n_array_push(pkgs, pkg_link(p));
                
            else if (p == pkg) {
                found = 1;
                break;
            }
        }
#if DEVEL        
        if (pkgs)
        {
            int i;
            xx++;
            printf("  xx %s %d\n", path, n_array_size(pkgs));
            if (strcmp(path, "usr/bin") == 0)
                for (i=0; i<n_array_size(pkgs); i++)
                    printf("       %s\n", pkg_id(n_array_nth(pkgs, i)));
        }
#endif        
        
    }

    n_str_tokl_free(tl_save);

    if (pkgs_ptr) {
        *pkgs_ptr = pkgs;
        found = 1;
        n_assert(pkgs);
        n_assert(n_array_size(pkgs));
    }
    
    return found;
}

tn_array *pkgdir_dirindex_get(const struct pkgdir_dirindex *dirindex,
                              tn_array *pkgs, const char *path)
{
    int pkgs_passsed = 1;
    
    if (pkgs == NULL) {
        pkgs = pkgs_array_new(4);
        pkgs_passsed = 0;
    }
    
    if (!do_pkgdir_dirindex_get(dirindex, &pkgs, NULL, path) && !pkgs_passsed)
        n_array_cfree(&pkgs);
    
    DBGF("ret %p %d\n", pkgs, pkgs ? n_array_size(pkgs): -1);
    
    return pkgs ? (n_array_size(pkgs) ? pkgs : NULL) : NULL;  
}

int pkgdir_dirindex_pkg_has_path(const struct pkgdir_dirindex *dirindex,
                                 const struct pkg *pkg, const char *path)
{
    DBGF("%s %s\n", pkg_id(pkg), path);
    return do_pkgdir_dirindex_get(dirindex, NULL, pkg, path);
}
