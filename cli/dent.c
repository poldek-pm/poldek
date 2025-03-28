/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fnmatch.h>
#include <sys/param.h>

#include <trurl/trurl.h>

#include "compiler.h"
#include "pkgdir/pkgdir.h"
#include "i18n.h"
#include "log.h"
#include "cli.h"
#include "arg_packages.h"
#include "poldek_util.h"

static inline
struct pkg_dent *pkg_dent_new(struct poclidek_ctx *cctx, const char *name,
                              struct pkg *pkg, int flags, const char *dirpath)
{
    struct pkg_dent *ent;
    int name_len = 0, dirpath_at = 0, dirpath_len = 0, len = 0;

    if (name) {
        while (*name == '/')
            name++;

        name_len = strlen(name);
        len += name_len + 1;
        n_assert(flags & PKG_DENT_DIR);
        n_assert(dirpath);

        dirpath_at = len;
        dirpath_len = strlen(dirpath);

        len += dirpath_len + 1;
    }

    ent = cctx->_dent_alloc(cctx, sizeof(*ent) + len);
    ent->_refcnt = 0;
    ent->flags = flags;
    ent->parent = NULL;

    if (name) {
        char *p;

        memcpy(ent->_buf, name, name_len + 1);
        ent->name = ent->_buf;

        if (dirpath) {
            memcpy(&ent->_buf[dirpath_at], dirpath, dirpath_len + 1);
            ent->path = &ent->_buf[dirpath_at];
        }

        p = ent->_buf;
        if (strchr(p, '/'))
            p = n_dirname(p);

        while (*p) {
            if (isspace(*p) || *p == '/')
                *p = '.';
            p++;
        }
    }

    if (flags & PKG_DENT_DIR) {
        ent->pkg_dent_ents = n_array_new(128, (tn_fn_free)pkg_dent_free,
                                         (tn_fn_cmp)pkg_dent_cmp);
        n_array_ctl(ent->pkg_dent_ents, TN_ARRAY_AUTOSORTED);

    } else {
        ent->name = pkg_id(pkg);
        ent->pkg_dent_pkg = pkg_link(pkg);
    }

    return ent;
}
#define pkg_dent_new_pkg(cctx, pkg) pkg_dent_new(cctx, NULL, pkg, 0, NULL)
#define pkg_dent_new_dir(cctx, name, dirpath) pkg_dent_new(cctx, name, NULL, PKG_DENT_DIR, dirpath)


struct pkg_dent *pkg_dent_link(struct pkg_dent *ent)
{
    ent->_refcnt++;
    return ent;
}

void pkg_dent_free(struct pkg_dent *ent)
{
    DBGF("%s, refcnt %d\n", ent->name, ent->_refcnt);

    if (ent->_refcnt > 0) {
        ent->_refcnt--;
        return;
    }

    if (ent->flags & PKG_DENT_DIR) {
        n_assert(ent->pkg_dent_ents);
        n_array_free(ent->pkg_dent_ents);
        ent->pkg_dent_ents = NULL;

    } else {
        pkg_free(ent->pkg_dent_pkg);
        ent->pkg_dent_pkg = NULL;
    }


    ent->flags |= PKG_DENT_DELETED;
    //free(ent); - obstacked
}

static inline struct pkg *pkg_dent_getpkg(struct pkg_dent *ent)
{
    if (ent->flags & PKG_DENT_DIR)
        return NULL;
    return ent->pkg_dent_pkg;
}

int pkg_dent_cmp(struct pkg_dent *e1, struct pkg_dent *e2)
{
    return strcmp(e1->name, e2->name);
}

int pkg_dent_strncmp(struct pkg_dent *ent, const char *name)
{
    DBGF("cmp(%s, %s) %zu\n", ent->name, name, strlen(name));
    return strncmp(ent->name, name, strlen(name));
}

int pkg_dent_strcmp(struct pkg_dent *ent, const char *name)
{
    return strcmp(ent->name, name);
}

static
int pkg_dent_cmp_ptr(struct pkg_dent *e1, struct pkg_dent *e2)
{
    return e1 != e2;
}


int pkg_dent_cmp_btime(struct pkg_dent *ent1, struct pkg_dent *ent2)
{
    int cmprc;
    int32_t btime1, btime2;

    btime1 = (ent1->flags & PKG_DENT_DIR) ? 0 : ent1->pkg_dent_pkg->btime;
    btime2 = (ent2->flags & PKG_DENT_DIR) ? 0 : ent2->pkg_dent_pkg->btime;

    cmprc = btime1 - btime2;
    if (cmprc)
        return cmprc;

    return strcmp(ent1->name, ent2->name);
}

int pkg_dent_cmp_bday(struct pkg_dent *ent1, struct pkg_dent *ent2)
{
    int cmprc, gmt_off;
    int32_t btime1, btime2;

    btime1 = (ent1->flags & PKG_DENT_DIR) ? 0 : ent1->pkg_dent_pkg->btime;
    btime2 = (ent2->flags & PKG_DENT_DIR) ? 0 : ent2->pkg_dent_pkg->btime;

    gmt_off = poldek_util_get_gmt_offs();

    cmprc = ((btime1 + gmt_off) / 86400) - ((btime2 + gmt_off) / 86400);
    cmprc = btime1 - btime2;

    if (cmprc)
        return cmprc;

    return strcmp(ent1->name, ent2->name);
}



struct pkg_dent *pkg_dent_add_pkg(struct poclidek_ctx *cctx,
                                  struct pkg_dent *dent, struct pkg *pkg)
{
    struct pkg_dent *ent;

    ent = pkg_dent_new_pkg(cctx, pkg);
    n_array_push(dent->pkg_dent_ents, ent);
    n_array_sort(dent->pkg_dent_ents);
    return ent;
}

void pkg_dent_remove_pkg(struct pkg_dent *dent, struct pkg *pkg)
{
    struct pkg_dent tmp;

    n_array_sort(dent->pkg_dent_ents);
    tmp.name = pkg_id(pkg);
    n_array_remove(dent->pkg_dent_ents, &tmp);
}



inline
int pkg_dent_add_pkgs(struct poclidek_ctx *cctx,
                      struct pkg_dent *dent, tn_array *pkgs)
{
    int i;
    struct pkg_dent *ent;

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        if (pkg_is_scored(pkg, PKG_IGNORED))
            continue;
        ent = pkg_dent_new_pkg(cctx, pkg);
        n_array_push(dent->pkg_dent_ents, ent);
    }
    n_array_sort(dent->pkg_dent_ents);
    return 1;
}

static
int pkg_dent_replace_pkgs(struct poclidek_ctx *cctx, struct pkg_dent *dent,
                          tn_array *pkgs)
{
    int i, n;
    struct pkg_dent *ent;
    tn_array *ents;


    ents = n_array_dup(dent->pkg_dent_ents, (tn_fn_dup)pkg_dent_link);
    n_array_clean(dent->pkg_dent_ents);

    n = 0;
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        if (pkg_is_scored(pkg, PKG_IGNORED))
            continue;

        if (n == n_array_size(ents))
            break;

        ent = n_array_nth(ents, n);
        n++;

        ent->name = pkg_id(pkg);
        pkg_free(ent->pkg_dent_pkg);
        ent->pkg_dent_pkg = pkg_link(pkg);
        n_array_push(dent->pkg_dent_ents, pkg_dent_link(ent));
    }

    for (; i < n_array_size(pkgs); i++) { /* the rest */
        struct pkg *pkg = n_array_nth(pkgs, i);

        if (pkg_is_scored(pkg, PKG_IGNORED))
            continue;
        ent = pkg_dent_new_pkg(cctx, pkg);
        n_array_push(dent->pkg_dent_ents, ent);
    }

    n_array_sort(dent->pkg_dent_ents);
    n_array_free(ents);
    return 1;
}

static
char *dent_dirpath(char *path, int size, const struct pkg_dent *dent, const char *child)
{
    tn_array *stack;

    stack = n_array_new(4, NULL, NULL);

    if (child) {
        if (*child == '/')
            child++;

        n_array_push(stack, (char*)child);
    }

    while (dent->parent) {
        n_array_push(stack, (char*)dent->name);
        dent = dent->parent;
    }

    path[0] = '/';
    path[1] = '\0';

    int n = 1;
    while (n_array_size(stack))
        n += n_snprintf(&path[n], size - n, "%s/", n_array_shift(stack));

    if (n > 1)
        path[n - 1] = '\0';     /* eat last '/' */

    n_array_free(stack);
    return path;
}


struct pkg_dent *pkg_dent_add_dir(struct poclidek_ctx *cctx,
                                  struct pkg_dent *parent, const char *name)
{
    char path[PATH_MAX];
    struct pkg_dent *ent;

    if (parent)
        dent_dirpath(path, sizeof(path), parent, name);
    else                        /* root dir */
        n_snprintf(path, sizeof(path), "%s", name);

    ent = pkg_dent_new_dir(cctx, name, path);
    DBGF("adddir %s, %s\n", name, path);

    if (parent) {
        ent->parent = parent;
        n_array_push(parent->pkg_dent_ents, ent);
        n_array_sort(parent->pkg_dent_ents);
    }
    return ent;
}


char *poclidek_dent_dirpath(char *path, int size, const struct pkg_dent *dent)
{
    tn_array *stack;
    int n = 0;

    stack = n_array_new(4, NULL, NULL);
    while (dent->parent) {
        n_array_push(stack, (char*)dent->name);
        dent = dent->parent;
    }

    path[0] = '/';
    path[1] = '\0';
    n = 1;
    while (n_array_size(stack))
        n += n_snprintf(&path[n], size - n, "%s/", n_array_shift(stack));

    if (n > 1)
        path[n - 1] = '\0';     /* eat last '/' */
    n_array_free(stack);
    return path;
}


static void dent_sort(const char *foo, void *dent)
{
    struct pkg_dent *ent = dent;
    foo = foo;
    n_assert(pkg_dent_isdir(ent));
    n_array_sort(ent->pkg_dent_ents);
}

struct pkg_dent *poclidek_dent_setup(struct poclidek_ctx *cctx,
                                     const char *path, tn_array *pkgs,
                                     int force)
{
    struct pkgdir    *curr_pkgdir = NULL;
    struct pkg_dent  *dest = NULL, *curr_ent = NULL;
    tn_hash          *dent_ht;
    int i, add = 0, add_subdirs = 0, replace = 0;


    if (n_str_eq(path, POCLIDEK_INSTALLEDDIR))
        add = 1;
    else if (n_str_eq(path, POCLIDEK_AVAILDIR))
        add = add_subdirs = 1;
    else
        n_die("%s: unknown dir", path);

    n_assert(add);


    if ((dest = poclidek_dent_find(cctx, path)) == NULL)
        dest = pkg_dent_add_dir(cctx, cctx->rootdir, path);
    else if (force)
        replace = 1;
    else if (!pkg_dent_isstub(dest))
        n_die("%s: duplicate directory", path);

    n_assert(dest);
    pkg_dent_clr_isstub(dest);
    if (replace)
        pkg_dent_replace_pkgs(cctx, dest, pkgs);
    else
        pkg_dent_add_pkgs(cctx, dest, pkgs);

    if (!add_subdirs)
        return dest;

    if (cctx->currdir == NULL)
        cctx->currdir = dest;

    dent_ht = n_hash_new(32, NULL);
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        if (pkg->pkgdir != curr_pkgdir) {
            struct pkg_dent *dent;
            char idbuf[256];
            const char *id;

            n_assert(pkg->pkgdir);
            curr_pkgdir = pkg->pkgdir;

            id = pkgdir_idstr(pkg->pkgdir, idbuf, sizeof(idbuf));
            if ((dent = n_hash_get(dent_ht, id)) == NULL) {
                char name[256], *p;
                n_snprintf(name, sizeof(name), "%s", id);
                p = name;
                while (*p) {
                    if (!isprint(*p)) *p = '.';
                    p++;
                }
                dent = pkg_dent_add_dir(cctx, cctx->rootdir, name);
                n_hash_insert(dent_ht, id, dent);
            }

            curr_ent = dent;
        }
        n_array_push(curr_ent->pkg_dent_ents, pkg_dent_new_pkg(cctx, pkg));
    }

    n_hash_map(dent_ht, dent_sort);
    n_hash_free(dent_ht);
    return dest;
}


static
struct pkg_dent *get_dir_dent(struct poclidek_ctx *cctx,
                              struct pkg_dent *currdir, const char *path)
{
    const char **tl, **tl_save, *p;
    struct pkg_dent *ent = NULL;

    if (currdir == NULL)
        currdir = cctx->rootdir;

    n_assert(currdir);

    if ((p = strchr(path, '/')) == NULL) {
        ent = n_array_bsearch_ex(currdir->pkg_dent_ents, path,
                                 (tn_fn_cmp)pkg_dent_strcmp);
        return ent;
    }

    if (*path == '/')
        currdir = cctx->rootdir;

    tl = tl_save = n_str_tokl(path, "/");

    while (*tl) {
        if (n_str_ne(*tl, "")) {
            if ((ent = get_dir_dent(cctx, currdir, *tl)) == NULL)
                break;
            currdir = ent;
        }

        tl++;
    }
    n_str_tokl_free(tl_save);
    return ent;
}


int poclidek_chdir(struct poclidek_ctx *cctx, const char *path)
{
    const char **tl, **tl_save, *p;
    struct pkg_dent *ent;
    int rc = 0;

    DBGF("path %s\n", path);
    n_assert(cctx->currdir);
    n_assert(path);

    if (n_str_eq(path, "."))
        return 1;

    if (n_str_eq(path, "..")) {
        if (cctx->currdir->parent)
            cctx->currdir = cctx->currdir->parent;
        return 1;
    }

    if ((p = strchr(path, '/')) == NULL) {
        ent = n_array_bsearch_ex(cctx->currdir->pkg_dent_ents, path,
                                 (tn_fn_cmp)pkg_dent_strcmp);
        if (ent) {
            cctx->currdir = ent;
            return 1;

        } else {
            return 0;
        }
    }

    if (*path == '/')
        cctx->currdir = cctx->rootdir;

    tl = tl_save = n_str_tokl(path, "/");
    rc = 1;
    while (*tl) {
        if (n_str_ne(*tl, ""))
            if (!(rc = poclidek_chdir(cctx, *tl)))
                break;
        tl++;
    }
    n_str_tokl_free(tl_save);
    return rc;
}


int poclidek_chdirent(struct poclidek_ctx *cctx, const struct pkg_dent *dent)
{
    char path[PATH_MAX];

    poclidek_dent_dirpath(path, sizeof(path), dent);
    return poclidek_chdir(cctx, path);
}


struct pkg_dent *poclidek_dent_ldfind(struct poclidek_ctx *cctx, const char *path, unsigned flags)
{
    struct pkg_dent *dent;

    DBGF("path %s, currdir=%s\n", path, cctx->currdir ? cctx->currdir->name : NULL);

    if ((dent = poclidek_dent_find(cctx, path)) != NULL) {
        DBGF("dent %s, stub %d, ents %d\n", dent->name, pkg_dent_isstub(dent),
             n_array_size(dent->pkg_dent_ents));

        n_assert(pkg_dent_isdir(dent));

        if (flags & PKG_DENT_LDFIND_STUBSONLY) {
            if (pkg_dent_isstub(dent) && n_array_size(dent->pkg_dent_ents) > 0)
                return dent;

            return NULL;
        }

        if (!pkg_dent_isstub(dent))
            return dent;

        if ((flags & PKG_DENT_LDFIND_STUBSOK) && n_array_size(dent->pkg_dent_ents) > 0) /* have package stubs */
            return dent;
    }

    if (flags & PKG_DENT_LDFIND_STUBSONLY) {
        return NULL;
    }

    if (dent) {
        path = dent->path;
    } else {
        n_assert(cctx->homedir);
        if (path == NULL)
            path = POCLIDEK_HOMEDIR;
    }

    DBGF("ld path %s\n", path);
    n_assert(path);

    unsigned ldflags = 0;
    if (n_str_eq(path, POCLIDEK_INSTALLEDDIR))
        ldflags |= POCLIDEK_LOAD_INSTALLED;
    else if (n_str_eq(path, POCLIDEK_AVAILDIR))
        ldflags |= POCLIDEK_LOAD_AVAILABLE;
    else
        n_die("load packages from %s??\n", path);

    DBGF("ld path %s, flags %u\n", path, ldflags);
    if (ldflags) {
        poclidek_load_packages(cctx, ldflags); /* will call poclidek_dent_setup */
    }

    return poclidek_dent_find(cctx, path);
}


struct pkg_dent *poclidek_dent_find(struct poclidek_ctx *cctx, const char *path)
{

    if (path == NULL || n_str_eq(path, ".") || n_str_eq(path, ""))
        return cctx->currdir;

    if (n_str_eq(path, "/"))
        return cctx->rootdir;

    return get_dir_dent(cctx, cctx->currdir, path);
}

struct pkg_dent *poclidek_dent_root(struct poclidek_ctx *cctx)
{
    return cctx->rootdir;
}


tn_array *poclidek_get_dent_ents(struct poclidek_ctx *cctx, const char *path, unsigned flags)
{
    struct pkg_dent *ent;

    DBGF("path %s, %d\n", path, flags);
    if ((ent = poclidek_dent_ldfind(cctx, path, flags)))
        return ent->pkg_dent_ents;

    return NULL;
}


tn_array *poclidek_get_dent_packages(struct poclidek_ctx *cctx, const char *dir, unsigned flags)
{
    tn_array *pkgs, *ents;
    register int i;

    if ((ents = poclidek_get_dent_ents(cctx, dir, flags)) == NULL)
        return NULL;

    pkgs = pkgs_array_new_ex(n_array_size(ents), pkg_cmp_name_evr_rev);

    for (i=0; i < n_array_size(ents); i++) {
        struct pkg *pkg;

        if ((pkg = pkg_dent_getpkg(n_array_nth(ents, i))))
            n_array_push(pkgs, pkg_link(pkg));
    }
    n_array_sort(pkgs);
    return pkgs;
}


static
tn_array *do_resolve(struct arg_packages *aps, tn_array *ents,
                     unsigned flags);


tn_array *poclidek_resolve_dents(const char *path,
                                 struct poclidek_ctx *cctx,
                                 struct poldek_ts *ts,
                                 unsigned resolve_flags,
                                 unsigned flags)
{
    tn_array *ents;

    //DBGF("path %s, %d\n", path, flags);

    if ((ents = poclidek_get_dent_ents(cctx, path, flags)) == NULL)
        return NULL;

    //DBGF("ents %p\n", ents);

    if (poldek_ts_get_arg_count(ts) == 0)
        return n_ref(ents);

    return do_resolve(ts->aps, ents, resolve_flags);
}


static
tn_array *do_resolve(struct arg_packages *aps,
                     tn_array *ents, unsigned flags)
{
    tn_array *ments = NULL, *masks;
    int i, j, nmasks;
    int *matches, *matches_bycmp;

    masks = arg_packages_get_masks(aps, 0);
    nmasks = n_array_size(masks);

    for (i=0; i < nmasks; i++) {
        char *mask = n_array_nth(masks, i);
        if (*mask == '*' && *(mask + 1) == '\0') {
            n_array_free(masks);
            return n_ref(ents);
        }
    }

    matches = alloca(nmasks * sizeof(*matches));
    memset(matches, 0, nmasks * sizeof(*matches));

    matches_bycmp = alloca(nmasks * sizeof(*matches_bycmp));
    memset(matches_bycmp, 0, nmasks * sizeof(*matches_bycmp));

    ments = n_array_clone(ents);
    for (i=0; i < n_array_size(ents); i++) {
        struct pkg_dent *ent = n_array_nth(ents, i);
        struct pkg *pkg = NULL;

        if (!pkg_dent_isdir(ent))
            pkg = ent->pkg_dent_pkg;

        for (j=0; j < nmasks; j++) {
            char *mask = n_array_nth(masks, j);
            //int  skip = 0;

            switch (*mask) {
                case '~':
                case '!':           /* for backward compatybility */
                    //skip = 1;       /* optional package */
                    break;

                case '@':
                    mask++;
                    break;
            }

            /* ls poldek */
            if (pkg && strcmp(mask, pkg->name) == 0) {
                n_array_push(ments, pkg_dent_link(ent));
                matches_bycmp[j]++;
                matches[j]++;
                continue;
            } /* else */

            /* ls poldek* */
            if (fnmatch(mask, ent->name, 0) == 0) {
                DBGF("fnmatch %s %s\n", mask, ent->name);
                n_array_push(ments, pkg_dent_link(ent));
                matches[j]++;
                continue;
            } /* else */

            /* ls poldek*devel */
            if (pkg && fnmatch(mask, pkg->name, 0) == 0) {
                n_array_push(ments, pkg_dent_link(ent));
                matches[j]++;
            }
        }
    }


    for (j=0; j < n_array_size(masks); j++) {
        const char *mask = n_array_nth(masks, j);

        if (matches[j] == 0 && (flags & ARG_PACKAGES_RESOLV_MISSINGOK) == 0) {
            logn(LOGERR, _("%s: no such package or directory"), mask);
            if (!(flags & ARG_PACKAGES_RESOLV_WARN_ONLY)) n_array_clean(ments);
        }

        if ((flags & ARG_PACKAGES_RESOLV_UNAMBIGUOUS) == 0) {
            if (matches_bycmp[j] > 1 && flags & ARG_PACKAGES_RESOLV_EXACT) {
                logn(LOGERR, _("%s: ambiguous name"), mask);
                if (!(flags & ARG_PACKAGES_RESOLV_WARN_ONLY)) n_array_clean(ments);
            }
        }
    }


    n_array_sort(ments);
    n_array_uniq_ex(ments, (tn_fn_cmp)pkg_dent_cmp_ptr);
    n_array_free(masks);

    //if (flags & ARG_PACKAGES_RESOLV_UNAMBIGUOUS)
    //    n_array_uniq_ex(pkgs, (tn_fn_cmp)pkg_cmp_name_uniq);

    if (n_array_size(ments) == 0) {
        n_array_free(ments);
        ments = NULL;
    }

    return ments;
}


tn_array *poclidek_resolve_packages(const char *path, struct poclidek_ctx *cctx,
                                    struct poldek_ts *ts, unsigned resolve_flags,
                                    unsigned flags)
{
    tn_array *avpkgs, *pkgs = NULL;

    if ((avpkgs = poclidek_get_dent_packages(cctx, path, flags)) == NULL)
        return NULL;

    if (arg_packages_resolve(ts->aps, avpkgs, NULL, resolve_flags)) {
        pkgs = arg_packages_get_resolved(ts->aps);
        if (n_array_size(pkgs) == 0)
            n_array_cfree(&pkgs);

        /* just make sure its the same comparator as in dent */
        n_array_ctl_set_cmpfn(pkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev);
    }
    n_array_free(avpkgs);

    return pkgs;
}

const char *poclidek_pwd(struct poclidek_ctx *cctx)
{
    if (cctx->currdir == NULL)
        return NULL;

    return cctx->currdir->path;
}
