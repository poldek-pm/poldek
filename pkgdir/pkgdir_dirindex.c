/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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

#include "compiler.h"
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

/* prefixes for non-path hash entries */
#define PREFIX_PKGKEY_REQDIR   '\\'
#define PREFIX_PKGKEY_OWNDIR   '~'
#define PREFIX_PKGKEY_NO       '`'

#define PREFIXLEN 2

const char *pkgdir_dirindex_basename = "dirindex";

struct pkgdir_dirindex {
    struct tndb *db;
    tn_alloc *na;
    tn_hash  *idmap;             /* { package_no => pkg } pairs */
    tn_hash  *keymap;            /* { package_key => package_no } */
};

static
const char **get_package_directories(struct tndb *db, const char *key, size_t klen,
                                     char **val, size_t *bytes_read, int *ndirs);

static tn_array *do_dirindex_get(const struct pkgdir_dirindex *dirindex,
                                 tn_array *pkgs, const char *path);

/* package_no as db key */
static int package_no_key(char *buf, int size, uint32_t package_no, int prefixed)
{
    if (prefixed)
        return n_snprintf(buf, size, "_%c%u", PREFIX_PKGKEY_NO, package_no);
    return n_snprintf(buf, size, "%u", package_no);
}

/* package as db key */
static int package_key(char *key, int size, const struct pkg *pkg, int prefix)
{
    int n;

    n_assert(size >= UINT8_MAX);

    key[0] = '_';
    key[1] = prefix;

    /*
     * add buildtime to pkgkey - enforces dirindex update when package was rebuilt
     * without release change
     */
    n = pndir_make_pkgkey(&key[PREFIXLEN], size - PREFIXLEN, pkg) + PREFIXLEN;
    n += n_snprintf(&key[n], size - PREFIXLEN - n, ":%u", pkg->btime);

    return n;
}

static tn_buf *dirarray_join(tn_buf *nbuf, tn_array *arr, char *sep)
{
    int i, size = n_array_size(arr);

    for (i=0; i < size; i++) {
        const char *dirname = n_array_nth(arr, i);

        n_buf_printf(nbuf, "%s%s%s", *dirname != '/' ? "/" : "",
    		     dirname, i < size - 1 ? sep : "");
    }

    return nbuf;
}

/* store { PREFIX[2] . package_no => pkg } pair */
static
void store_package_no(uint32_t package_no, struct tndb *db, const struct pkg *pkg)
{
    char key[512], val[512];
    int klen, vlen;

    klen = package_no_key(key, sizeof(key), package_no, 1);
    vlen = package_key(val, sizeof(val), pkg, PREFIX_PKGKEY_REQDIR);

    tndb_put(db, key, klen, val + PREFIXLEN, vlen - PREFIXLEN); /* without prefix */
}

/* build hash of path => package_no[] */
static
void add_to_path_index(tn_hash *path_index, const char *path, uint32_t package_no)
{
    char val[512];
    int vlen, klen = 0;
    unsigned khash = 0;
    tn_array *keys;

    if (strlen(path) > 255)
	return;

    if ((keys = n_hash_get_ex(path_index, path, &klen, &khash)) == NULL) {
        keys = n_array_new(16, free, (tn_fn_cmp)strcmp);
        n_hash_insert_ex(path_index, path, klen, khash, keys);
    }

    vlen = package_no_key(val, sizeof(val), package_no, 0);
    //printf("add %s: %s\n", path, val);
    n_array_push(keys, n_strdupl(val, vlen));
}


static int store_from_previous(uint32_t package_no, struct pkg *pkg, struct tndb *db,
                               tn_hash *path_index, struct pkgdir_dirindex *dirindex)
{
    const char **tl, **tl_save;
    char       key[512], *val = NULL;
    size_t     klen, vlen;
    int        found = 0, ndirs;

    n_assert(dirindex);

    klen = package_key(key, sizeof(key), pkg, PREFIX_PKGKEY_REQDIR);

    if (n_hash_exists(dirindex->keymap, &key[2])) { /* got it */
        DBGF("HIT %s %s\n", pkg_id(pkg), key);
        found = 1;
    }

    /* requires any directories? */
    if ((vlen = tndb_get_all(dirindex->db, key, klen, (void**)&val)) > 0) {
        tndb_put(db, key, klen, val, vlen);
	n_cfree(&val);
    }

    key[1] = PREFIX_PKGKEY_OWNDIR;

    tl = tl_save = get_package_directories(dirindex->db, key, klen, &val, &vlen,
                                           &ndirs);

    if (tl == NULL) /* without owned directories */
        return found;

    n_assert(vlen > 0);
    tndb_put(db, key, klen, val, vlen);

    while (*tl) {
        const char *dir = *tl;
        if (dir[1] != '\0')
    	    dir = dir + 1; /* skip '/' only when strlen(dir) > 1 */
        add_to_path_index(path_index, dir, package_no);
        tl++;
    }

    n_str_tokl_free(tl_save);
    n_free(val);

    return found;
}

/* process package files and add them to dirindex */
static
void store_package(uint32_t package_no, struct pkg *pkg, struct tndb *db,
                   tn_hash *path_index, tn_buf *nbuf, struct pkgdir_dirindex *prev)
{
    tn_array *required = NULL, *owned = NULL;
    struct pkgflist *flist;

    if (prev && store_from_previous(package_no, pkg, db, path_index, prev))
        return;

    if ((flist = pkg_get_flist(pkg)) == NULL)
        return;

    if (pkgfl_owned_and_required_dirs(flist->fl, &owned, &required) == 0) {
        DBGF("%s: NULL\n", pkg_id(pkg));
        pkgflist_free(flist);
        return;
    }

    msgn_i(3, 4, " new package %s\n", pkg_id(pkg));

    DBGF("%s: %d %d\n", pkg_id(pkg), owned ? n_array_size(owned): -1,
         required ? n_array_size(required): -1);

    if (owned) {
        int i;

        for (i=0; i < n_array_size(owned); i++) {
            const char *dir = n_array_nth(owned, i);
            add_to_path_index(path_index, dir, package_no);
        }
    }

    if (required || owned) { /* write { package_key => package.{r,o}dirs.join(':') } */
        char key[512];
        int klen;

        klen = package_key(key, sizeof(key), pkg, PREFIX_PKGKEY_REQDIR);

        if (owned)
            n_assert(n_array_size(owned) > 0);

        if (required)
            n_assert(n_array_size(required) > 0);

        if (required) {
            n_buf_clean(nbuf);
            nbuf = dirarray_join(nbuf, required, ":");
            tndb_put(db, key, klen, n_buf_ptr(nbuf), n_buf_size(nbuf));
        }

        if (owned) {
            n_buf_clean(nbuf);
            nbuf = dirarray_join(nbuf, owned, ":");

            /* ugly, but saves another package_key() call */
            key[1] = PREFIX_PKGKEY_OWNDIR;

            tndb_put(db, key, klen, n_buf_ptr(nbuf), n_buf_size(nbuf));
        }
    }

    n_array_cfree(&owned);
    n_array_cfree(&required);
    pkgflist_free(flist);
}


static int dirindex_create(const struct pkgdir *pkgdir, const char *path,
                           struct pkgdir_dirindex *prev_dirindex)
{
    struct tndb   *db;
    tn_buf        *nbuf;
    tn_hash       *path_index;
    tn_array      *directories;
    tn_alloc      *na;
    struct vflock *lock;
    int           i;
    char          *tmp, *dir;

    if (n_array_size(pkgdir->pkgs) == 0)
        return 1;

    MEMINF("START");
    msgn_i(2, 2, "%s directory index of %s...",
           prev_dirindex ? "Updating" : "Creating", pkgdir_idstr(pkgdir));

    n_strdupap(path, &tmp);
    dir = n_dirname(tmp);

    DBGF("mkdir %s %s\n", path, dir);
    DBGF("pkgdir %p, %d packages\n", pkgdir, n_array_size(pkgdir->pkgs));

    if ((lock = vf_lock_mkdir(dir)) == NULL)
        return 0;

    db = tndb_creat(path, 0, TNDB_SIGN_DIGEST);

    if (db == NULL) {
        logn(LOGERR, "%s: open failed (%m)\n", path);
        vf_lock_release(lock);
        return 0;
    }

    na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    path_index = n_hash_new_na(na, n_array_size(pkgdir->pkgs) * 16, (tn_fn_free)n_array_free);
    nbuf = n_buf_new(1024 * 16);

    tn_array *pkgs = pkgdir->_unsorted_pkgs;
    /* unsorted_pkgs are valid for non-patched repos only */
    if (pkgdir->flags & (PKGDIR_PATCHED | PKGDIR_CHANGED))
        pkgs = pkgdir->pkgs;

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        store_package_no(i, db, pkg);
        DBGF(" store pkgno %d %s\n", i, pkg_id(pkg));
    }

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        store_package(i, pkg, db, path_index, nbuf, prev_dirindex);

        if (i % 1000 == 0) {
            MEMINF("%d packages", i);
        }
    }

    /* store { path => packages_no[] } pairs */
    directories = n_hash_keys(path_index);
    msgn_i(3, 3, "Saving %d directories\n", n_array_size(directories));

    for (i=0; i < n_array_size(directories); i++) {
        const char *dir_path = n_array_nth(directories, i);
        tn_array *ids = n_hash_get(path_index, dir_path);
        int j;

        n_buf_clean(nbuf);
        for (j = 0; j < n_array_size(ids); j++) {
    	    n_buf_printf(nbuf, "%s%s", (char *)n_array_nth(ids, j),
    				       j < n_array_size(ids) - 1 ? ":" : "");
        }

        DBGF("  dir %s %s\n", dir_path, (char*)n_buf_ptr(nbuf));

        tndb_put(db, dir_path, strlen(dir_path), n_buf_ptr(nbuf), n_buf_size(nbuf));
    }

    n_array_free(directories);
    n_buf_free(nbuf);
    n_hash_free(path_index);
    n_alloc_free(na);
    tndb_close(db);

    struct utimbuf ut;
    ut.actime = ut.modtime = pkgdir_mtime(pkgdir);
    utime(path, &ut);

    vf_lock_release(lock);

    MEMINF("END");
    return 1;
}


/* build dirindex path based on pkgdir one */
static int dirindex_path(char *path, int size, const struct pkgdir *pkgdir)
{
    char tmp[PATH_MAX];
    char *ofpath, *suffix = NULL;
    int n;

    n = n_snprintf(tmp, sizeof(tmp), "%s", pkgdir_localidxpath(pkgdir));
    n_assert(n > 0);

    DBGF("localidxpath = %s, %s %s\n", tmp, pkgdir->type, pkgdir->compr);

    ofpath = tmp;
    if (ofpath[n - 1] == '/') {    /* directory */
        ofpath[n - 1] = '\0';

    } else if (!util__isdir(ofpath)) { /* not directory? */
        char *bn, *dn, *p, tstr[32];
        int tlen;

        n_basedirnam(ofpath, &dn, &bn);
        ofpath = dn;

        /* determine file custom suffix (rpmdbcache case) */
        DBGF("basename %s\n", bn);
        tlen = n_snprintf(tstr, sizeof(tstr), ".%s.", pkgdir->type);
        if ((p = strstr(bn, tstr))) {
            bn = p + tlen;
        }

        if (pkgdir->mod && pkgdir->mod->default_fn) {
            tlen = n_snprintf(tstr, sizeof(tstr), "%s.", pkgdir->mod->default_fn);
            if ((p = strstr(bn, tstr))) {
                bn = p + tlen;
            }

            DBGF("basename2 %s\n", bn);
        }

        if (pkgdir->compr) {    /* eat compr extension */
            if (n_str_eq(bn, pkgdir->compr)) {
                bn = "";
            } else {
                tlen = n_snprintf(tstr, sizeof(tstr), ".%s", pkgdir->compr);
                char *q = bn;
                while ((p = strstr(q, tstr)) != NULL) {
                    q = p + tlen;
                    if (*q == '\0') { /* ends with tstr */
                        *(p + 1) = '\0'; /* keep dot */
                        break;
                    }
                }
            }
        }
        suffix = bn;
        DBGF("basename3 %s\n", bn);
    }

    DBGF("path = %s\n", ofpath);
    n = vf_cachepath(path, size, ofpath);
    DBGF("cache path = %s\n", path);

    n_assert(n > 0);
    n += n_snprintf(&path[n], size - n, "/%s.%s.%stndb", pkgdir_dirindex_basename,
                    pkgdir->type, suffix);
    DBGF("result = %s\n", path);
    n_assert(n > 0);

    return n;
}

/* load { packages_key => package_no } into hash */
static tn_hash *load_keymap(struct tndb *db, int npackages)
{
    struct tndb_it  it;
    char            key[TNDB_KEY_MAX + 1] = {0}, *val = NULL;
    unsigned        nerr = 0, klen, vlen, vlen_max;
    tn_alloc        *na;
    tn_hash         *keymap;


    if (!tndb_it_start(db, &it))
        return NULL;

    na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    keymap = n_hash_new_na(na, 2 * npackages, NULL);

    vlen = vlen_max = 256;
    val = n_malloc(vlen);

    if (tndb_size(db) == 0) /* empty index */
        goto l_end;

    if (!tndb_it_rget(&it, key, &klen, (void**)&val, &vlen)) {
        logn(LOGERR, _("%s: invalid directory index"), tndb_path(db));
        nerr++;
        //msgn_i(2, 4, "%s: empty directory index", tndb_path(db));
        goto l_end;
    }

    while (*key == '_' && *(key + 1) == PREFIX_PKGKEY_NO) {
        char *id;

        val[vlen] = '\0';
        unsigned hash = n_hash_compute_hash(keymap, val, vlen);

        id = na->na_malloc(na, klen - PREFIXLEN + 1);
        memcpy(id, key + PREFIXLEN, klen - PREFIXLEN); /* skipping prefix */
        id[klen - PREFIXLEN] = '\0';

        DBGF("%s => %s\n", key, val);

        n_hash_hinsert(keymap, val, vlen, hash, id);

        if (vlen < vlen_max)   /* to avoid needless tndb_it_rget()'s reallocs */
            vlen = vlen_max;
        else
            vlen_max = vlen;

        if (!tndb_it_rget(&it, key, &klen, (void**)&val, &vlen)) {
            /* EOF is possible - packages without files */
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
const char **get_package_directories(struct tndb *db, const char *key, size_t klen,
                                     char **val, size_t *bytes_read, int *ndirs)
{
    const char **tl;
    char       *buf = NULL;
    size_t     vlen;

    if ((vlen = tndb_get_all(db, key, klen, (void**)&buf)) == 0)
        return NULL;

    buf[vlen] = '\0';
    *bytes_read = vlen;

    *ndirs = 0;
    tl = n_str_tokl_n(buf, ":", ndirs);

    if (tl && *ndirs == 0) {
        n_str_tokl_free(tl);
        tl = NULL;

	*bytes_read = 0;
        n_cfree(&buf);
    }

    *val = buf;

    return tl;
}

static
void verify_dirindex(struct pkgdir *pkgdir, struct pkgdir_dirindex *dirindex)
{
    const char **tl, **tl_save;
    int i;

    n_assert(dirindex);
    n_assert(dirindex->keymap);

    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        char key[512], *val = NULL;
        int ndirs;
        size_t klen, vlen;
        struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);

        klen = package_key(key, sizeof(key), pkg, PREFIX_PKGKEY_OWNDIR);
        n_assert (n_hash_exists(dirindex->keymap, &key[2]));

        tl = tl_save = get_package_directories(dirindex->db, key, klen,
                                               &val, &vlen, &ndirs);

        if (tl == NULL) /* without owned directories */
            continue;

        n_assert(vlen > 0);

        while (*tl) {
            const char *dir = *tl;
            tn_array *pkgs = NULL;
            pkgs = do_dirindex_get(dirindex, pkgs, dir);
            n_assert(pkgs);
            tl++;
        }

        n_str_tokl_free(tl_save);
        n_free(val);
    }
}

static
struct tndb *open_index_database(const struct pkgdir *pkgdir, const char *path,
                                 tn_hash **keymap)
{
    struct tndb     *db;
    tn_hash         *kmap = NULL;

    n_assert(n_array_size(pkgdir->pkgs)); /* XXX: tndb w/o */

    MEMINF("start");

    if ((db = tndb_open(path)) == NULL) {
        logn(LOGERR, "%s: open failed", path);
        goto l_error_end;
    }

    if (!tndb_verify(db)) {
        logn(LOGERR, _("%s: broken directory index"), path);
        goto l_error_end;
    }

    MEMINF("opened");

    if ((kmap = load_keymap(db, n_array_size(pkgdir->pkgs))) == NULL)
        goto l_error_end;

    MEMINF("keymap");

    *keymap = kmap;
    return db;

l_error_end:
    if (kmap)
        n_hash_free(kmap);

    if (db == NULL) {
        vf_unlink(path);

    } else {
        tndb_unlink(db);
        tndb_close(db);
    }
    return NULL;
}

/* load package dir-based requirements and add them to pkg */
static int update_pkdir_pkg(struct pkg *pkg, struct tndb *db,
                            const char *key, size_t klen)
{
    const char **tl, **tl_save;
    char       *val = NULL;
    size_t     vlen;
    int        ndirs = 0;

    n_assert(key[1] == PREFIX_PKGKEY_REQDIR);

    tl = tl_save = get_package_directories(db, key, klen, &val, &vlen, &ndirs);
    if (tl == NULL)
        return 0;

    if (pkg->reqs == NULL)
        pkg->reqs = capreq_arr_new(ndirs);

    while (*tl) {
        const char *dir = *tl;

        if (*dir) {
            struct capreq *req = capreq_new(pkg->na, dir, 0, NULL, NULL, 0,
                                            CAPREQ_BASTARD | CAPREQ_ISDIR);
            n_array_push(pkg->reqs, req);
        }
        tl++;
    }
    n_str_tokl_free(tl_save);
    n_free(val);

    if (ndirs > 10)
        n_array_sort(pkg->reqs);
    else
        n_array_isort(pkg->reqs);

    pkg->flags |= PKG_INCLUDED_DIRREQS;
    return ndirs;
}


#define UPDATE_IFNEEDED (1 << 0)

static
struct pkgdir_dirindex *load_dirindex(const struct pkgdir *pkgdir,
                                      const char *path, unsigned flags)
{
    struct tndb     *db;
    tn_alloc        *na = NULL;
    tn_hash         *idmap = NULL, *keymap = NULL;
    int             rc = 0, index_outdated = 0;
    int             i;
    struct pkgdir_dirindex *dirindex = NULL;

    msgn_i(2, 2, "Loading directory index of %s...", pkgdir_idstr(pkgdir));
    MEMINF("start");

    if ((db = open_index_database(pkgdir, path, &keymap)) == NULL)
        return NULL;

    rc = 0;

    na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    idmap = n_hash_new_na(na, n_array_size(pkgdir->pkgs), (tn_fn_free)pkg_free);

    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        struct pkg   *pkg = n_array_nth(pkgdir->pkgs, i);
        char         key[512], *pkg_no;

        package_key(key, sizeof(key), pkg, PREFIX_PKGKEY_REQDIR);
        pkg_no = n_hash_get(keymap, &key[PREFIXLEN]);

        DBGF("%s %s\n", pkg_no, &key[PREFIXLEN]);

        /* { package_no => package } map */
        if (pkg_no) {
            n_hash_replace(idmap, pkg_no, pkg_link(pkg));

        } else if (flags & UPDATE_IFNEEDED) {
            msgn_i(3, 4, "%s: missing package", pkg_id(pkg));
            index_outdated = 1;
            /* continue loading as it will be used to create
               updated version later */

        } else {
            logn(LOGWARN, _("%s: outdated directory index"), tndb_path(db));
            goto l_end;
        }

        DBGF("%s %s\n", pkg_no, pkg_id(pkg));
    }

    rc = 1;                     /* success */

l_end:
    if (rc) {                   /* OK */
        dirindex = na->na_malloc(na, sizeof(*dirindex));
        dirindex->db = db;
        dirindex->na = na;
        dirindex->idmap = idmap;
        dirindex->keymap = NULL;

        n_assert(keymap);
        if (index_outdated || (pkgdir->_ldflags & PKGDIR_LD_DIRINDEX) || poldek__is_in_testing_mode()) {
            dirindex->keymap = keymap;
        } else {
            n_hash_free(keymap);
        }

    } else {                    /* ERR */
        if (keymap)
            n_hash_free(keymap);

        tndb_unlink(db);
        tndb_close(db);

        if (idmap)
            n_hash_free(idmap);

        if (na)
            n_alloc_free(na);
    }
    MEMINF("end");

    if (index_outdated) {
        int created = dirindex_create(pkgdir, tndb_path(db), dirindex);
        pkgdir__dirindex_close(dirindex);
        dirindex = NULL;

        if (created) {
            dirindex = load_dirindex(pkgdir, path, 0);
        }
    }

    return dirindex;
}

static void update_pkgdir_packages(const struct pkgdir *pkgdir,
                                   struct pkgdir_dirindex *dirindex)
{
    int i;

    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        struct pkg   *pkg = n_array_nth(pkgdir->pkgs, i);
        char         key[512];
        int          klen;

        klen = package_key(key, sizeof(key), pkg, PREFIX_PKGKEY_REQDIR);
        update_pkdir_pkg(pkg, dirindex->db, key, klen);
    }
}

static
struct pkgdir_dirindex *open_dirindex(struct pkgdir *pkgdir,
                                      const char *path, int update_pkgs)
{
    struct pkgdir_dirindex *dirindex;
    time_t mtime = poldek_util_mtime(path);

    n_assert(pkgdir->ts > 0);

    if (mtime == 0)             /* not exists */
        dirindex_create(pkgdir, path, NULL);

    dirindex = load_dirindex(pkgdir, path, UPDATE_IFNEEDED);

    if (mtime != 0 && dirindex == NULL) {     /* broken? */
        if (dirindex_create(pkgdir, path, NULL))
            dirindex = load_dirindex(pkgdir, path, 0);
    }

    if (dirindex && update_pkgs)
        update_pkgdir_packages(pkgdir, dirindex);

    if (dirindex && poldek__is_in_testing_mode()) {
        msgn(1, "Verifying dirindex....");
        verify_dirindex(pkgdir, dirindex);
    }

    return dirindex;
}

struct pkgdir_dirindex *pkgdir__dirindex_open(struct pkgdir *pkgdir)
{
    char path[1024];
    dirindex_path(path, sizeof(path), pkgdir);

    return open_dirindex(pkgdir, path, 1);
}

void pkgdir__dirindex_close(struct pkgdir_dirindex *dirindex)
{
    tndb_close(dirindex->db);
    n_hash_free(dirindex->idmap);
    if (dirindex->keymap)
        n_hash_free(dirindex->keymap);

    n_alloc_free(dirindex->na);
    /* no free(dirindex), it is allocated by na */
}

void pkgdir__dirindex_update(struct pkgdir *pkgdir)
{
    struct pkgdir_dirindex *index;
    time_t idx_mtime, mtime;
    char path[1024];

    dirindex_path(path, sizeof(path), pkgdir);

    idx_mtime = pkgdir_mtime(pkgdir);
    mtime = poldek_util_mtime(path);

    if (mtime == idx_mtime) {
        return;
    }

    msgn_i(2, 2, "updating directory index of %s...", pkgdir_idstr(pkgdir));
    int verbosity = poldek_set_verbose(0);

    if ((pkgdir->flags & PKGDIR_LOADED) == 0) {
        pkgdir_load(pkgdir, 0, 0);
    }

    if (n_array_size(pkgdir->pkgs) == 0)
        goto l_end;

    if ((index = open_dirindex(pkgdir, path, 0))) {
        pkgdir__dirindex_close(index);

        /* set mtime to dirindex created by previous
           poldek versions */
        struct utimbuf ut;
        ut.actime = ut.modtime = idx_mtime;
        utime(path, &ut);
    }

 l_end:
    poldek_set_verbose(verbosity);
}

tn_array *get_package_directories_as_array(const struct pkgdir *pkgdir,
                                           const struct pkg *pkg, int prefix)
{
    const struct pkgdir_dirindex *dirindex = pkgdir->dirindex;
    const char  **tl, **tl_save;
    char        key[512], *val = NULL;
    size_t      klen, vlen;
    int         n = 0;
    tn_array    *dirs;

    if (dirindex == NULL)
        return NULL;

    klen = package_key(key, sizeof(key), pkg, prefix);
    tl = tl_save = get_package_directories(dirindex->db, key, klen,
                                           &val, &vlen, &n);
    if (tl == NULL)
        return NULL;

    dirs = n_array_new(n, free, (tn_fn_cmp)strcmp);
    while (*tl) {
        if (**tl)
            n_array_push(dirs, n_strdup(*tl));
        tl++;
    }

    n_str_tokl_free(tl_save);
    n_free(val);

    if (n_array_size(dirs) == 0)
        n_array_cfree(&dirs);

    return dirs;
}

tn_array *pkgdir_dirindex_get_required(const struct pkgdir *pkgdir,
                                       const struct pkg *pkg)
{
    return get_package_directories_as_array(pkgdir, pkg, PREFIX_PKGKEY_REQDIR);
}

tn_array *pkgdir_dirindex_get_provided(const struct pkgdir *pkgdir,
                                       const struct pkg *pkg)
{
    return get_package_directories_as_array(pkgdir, pkg, PREFIX_PKGKEY_OWNDIR);
}



static tn_array *do_dirindex_get(const struct pkgdir_dirindex *dirindex,
                                 tn_array *pkgs, const char *path)
{
    const char    **tl, **tl_save;
    char          val[8192];
    int           n, found, pkgs_passsed = 1;

    if (*path == '/' && path[1] != '\0')
        path++;

    if (!tndb_get_str(dirindex->db, path, (unsigned char *)val, sizeof(val)))
        return 0;

    tl = tl_save = n_str_tokl_n(val, ":", &n);
    DBGF("%s: FOUND %d (pkgs=%p)\n", path, n, pkgs ? pkgs : NULL);

    if (n) {
        if (pkgs == NULL) {
            pkgs = pkgs_array_new(4);
            pkgs_passsed = 0;
        }

        while (*tl) {
            const char *no = *tl;
            struct pkg *p;

            tl++;

            if (*no == '\0')
                continue;

            DBGF("no %s\n", no);

            if ((p = n_hash_get(dirindex->idmap, no)) == NULL)
                continue;

            n_array_push(pkgs, pkg_link(p));
        }
        n_str_tokl_free(tl_save);
        tl_save = NULL;
    }
    n_assert(tl_save == NULL);

    found = n_array_size(pkgs);

    if (found == 0) { /* patched pkgdir by diff without new packages */
        if (!pkgs_passsed)
            n_array_cfree(&pkgs);
    }

    return pkgs;
}

tn_array *pkgdir_dirindex_get(const struct pkgdir *pkgdir,
                              tn_array *pkgs, const char *path)
{
    if (pkgdir->dirindex == NULL)
        return NULL;

    return do_dirindex_get(pkgdir->dirindex, pkgs, path);
}

int pkgdir_dirindex_pkg_has_path(const struct pkgdir *pkgdir,
                                 const struct pkg *pkg, const char *path)
{
    const struct pkgdir_dirindex *dirindex = pkgdir->dirindex;
    const char    **tl, **tl_save;
    char          val[8192];
    int           n, found;

    if (dirindex == NULL)
        return 0;

    DBGF("%s %s\n", pkg_id(pkg), path);

    if (*path == '/' && path[1] != '\0')
        path++;

    if (!tndb_get_str(dirindex->db, path, (unsigned char *)val, sizeof(val)))
        return 0;

    tl = tl_save = n_str_tokl_n(val, ":", &n);
    DBGF("%s: FOUND %d\n", path, n);

    if (n) {
        while (*tl) {
            const char *no = *tl;
            struct pkg *p;

            tl++;

            if (*no == '\0')
                continue;

            DBGF("no %s\n", no);

            if ((p = n_hash_get(dirindex->idmap, no)) == NULL)
                continue;

            if (p == pkg) {
                found = 1;
                break;
            }
        }
        n_str_tokl_free(tl_save);
        tl_save = NULL;
    }
    n_assert(tl_save == NULL);

    return found;
}
