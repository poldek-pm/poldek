/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <trurl/nassert.h>
#include <trurl/nmalloc.h>

#include "compiler.h"
#include "i18n.h"
#include "pkg.h"
#include "capreqidx.h"
#include "capreq.h"
#include "log.h"

static void capreq_ent_free(struct capreq_idx_ent *ent)
{
    DBGF("ent %p, %p %d, %d\n", ent, ent->crent_pkgs, ent->_size, ent->items);
    if (ent->_size > 1)
        free(ent->crent_pkgs);
}

int capreq_idx_init(struct capreq_idx *idx, unsigned type, int nelem)
{
    idx->flags = type;

    MEMINF("START");
    idx->na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    idx->ht = n_hash_new_na(idx->na, nelem, (tn_fn_free)capreq_ent_free);
    n_hash_ctl(idx->ht, TN_HASH_NOCPKEY);
    MEMINF("END");
    return 1;
}


void capreq_idx_destroy(struct capreq_idx *idx)
{
    n_hash_free(idx->ht);
    n_alloc_free(idx->na);
    memset(idx, 0, sizeof(*idx));
}

static int ent_transform_to_array(struct capreq_idx_ent *ent)
{
    struct pkg *tmp;

    n_assert(ent->_size == 1);   /* crent_pkgs is NOT allocated */
    tmp = ent->crent_pkg;
    ent->crent_pkgs = n_malloc(2 * sizeof(*ent->crent_pkgs));
    ent->crent_pkgs[0] = tmp;
    ent->_size = 2;
    return 1;
}

static inline void idx_ent_sort(struct capreq_idx_ent *ent)
{
    register size_t i, j;

    for (i = 1; i < ent->items; i++) {
        register struct pkg *tmp = ent->crent_pkgs[i];

        j = i;
        while (j > 0 && tmp - ent->crent_pkgs[j - 1] < 0) {
            ent->crent_pkgs[j] = ent->crent_pkgs[j - 1];
            j--;
        }
        ent->crent_pkgs[j] = tmp;
    }
}

static inline int idx_ent_contains(struct capreq_idx_ent *ent, const struct pkg *pkg)
{
    register size_t l, r, i;
    register int cmp_re;

    l = 0;
    r = ent->items;

    while (l < r) {
	i = (l + r) / 2;

	if ((cmp_re = ent->crent_pkgs[i] - pkg) == 0) {
	    return 1;

	} else if (cmp_re > 0) {
	    r = i;

	} else if (cmp_re < 0) {
	    l = i + 1;
	}
    }

    return 0;
}

/* avoid to index caps (about 150k index entries for TH) */
/* a) path-based requirements from docs dirs */
const char *skip_PREFIXES[] = {
    "/usr/share/doc",
    "/usr/share/info",
    "/usr/share/man",
    "/usr/share/locale",
    "/usr/share/icons",
    NULL
};

int skip_LENGTHS[] = {
    0,
    0,
    0,
    0,
    0
};

/* b) well-known, FHS directories and some generic rpm specific caps */
const char *skip_CAPS[] = {
    "/etc", "/bin",
    "/usr/bin", "/usr/sbin", "/usr/lib",
    "/usr/share", "/usr/include",
    "rtld(GNU_HASH)",
    "elf(buildid)"
};

tn_hash *skip_CAPS_H = NULL;

inline static int indexable_cap(const char *name, int len, unsigned raw_hash)
{
    if (!skip_CAPS_H) {
        skip_CAPS_H = n_hash_new(128, NULL);
        n_hash_ctl(skip_CAPS_H, TN_HASH_NOCPKEY);

        int i = 0;
        while (skip_CAPS[i] != NULL) {
            n_hash_insert(skip_CAPS_H, skip_CAPS[i], skip_CAPS[i]);
            i++;
        }
    }

    uint32_t hash = n_hash_compute_index_hash(skip_CAPS_H, raw_hash);
    if (n_hash_hexists(skip_CAPS_H, name, len, hash))
        return 0;

    if (*name ==  '/') {
        int i = 0;
        const char *prefix;

        while ((prefix = skip_PREFIXES[i]) != NULL) {
            if (skip_LENGTHS[i] == 0)
                skip_LENGTHS[i] = strlen(prefix);

            if (strncmp(name, prefix, skip_LENGTHS[i]) == 0)
                return 0;

            i++;
        }
    }

    return 1;
}

int capreq_idx_add(struct capreq_idx *idx, const char *capname, int capname_len,
                   struct pkg *pkg)
{
    struct capreq_idx_ent *ent;
    uint32_t raw_khash = n_hash_compute_raw_hash(capname, capname_len);

    if (!indexable_cap(capname, capname_len, raw_khash)) {
        DBGF("skip %s\n", capname);
        return 1;
    }

    uint32_t khash = n_hash_compute_index_hash(idx->ht, raw_khash);

    if ((ent = n_hash_hget(idx->ht, capname, capname_len, khash)) == NULL) {
        const struct capreq_name_ent *cent = capreq__alloc_name(capname, capname_len);

        ent = idx->na->na_malloc(idx->na, sizeof(*ent));
        ent->_size = 1;
        ent->items = 1;
        ent->crent_pkg = pkg;

        n_hash_hinsert(idx->ht, cent->name, cent->len, khash, ent);

#if ENABLE_TRACE
        if ((n_hash_size(idx->ht) % 1000) == 0)
            n_hash_stats(idx->ht);
#endif

    } else {
        if (ent->_size == 1) {    /* crent_pkgs is NOT allocated */
            ent_transform_to_array(ent);

            /* save some reallocs as many needs libc/m/pthread, micro optimization, XXX */
            if (*capname == 'l' && (strncmp(capname, "libc", 4) == 0 ||
                                    strncmp(capname, "libm", 4) == 0 ||
                                    strncmp(capname, "libpthread", 4) == 0)) {
                ent->_size = 4096;
                ent->crent_pkgs = n_realloc(ent->crent_pkgs,
                                            ent->_size * sizeof(*ent->crent_pkgs));
            }
        }

        /*
         * Sometimes, there are duplicates, especially in dotnet-* packages
         * which provides multiple versions of one cap. For example dotnet-mono-zeroconf
         * provides: mono(Mono.Zeroconf) = 1.0.0.0, mono(Mono.Zeroconf) = 2.0.0.0, etc.
         */
        if (idx->flags & CAPREQ_IDX_CAP) { /* check for duplicates */
            if (idx_ent_contains(ent, pkg))
                return 1;
        }

        if (ent->items == ent->_size) {
            ent->_size *= 2;
            ent->crent_pkgs = n_realloc(ent->crent_pkgs,
                                        ent->_size * sizeof(*ent->crent_pkgs));
        }

        ent->crent_pkgs[ent->items++] = pkg;

        if (idx->flags & CAPREQ_IDX_CAP) { /* sort to prevent duplicates */
            idx_ent_sort(ent);
        }
    }

    return 1;
}


void capreq_idx_remove(struct capreq_idx *idx, const char *capname,
                       struct pkg *pkg)
{
    struct capreq_idx_ent *ent;

    if ((ent = n_hash_get(idx->ht, capname)) == NULL)
        return;

    if (ent->_size == 1) {      /* no crent_pkgs */
        if (pkg_cmp_name_evr(pkg, ent->crent_pkg) == 0) {
            ent->items = 0;
            ent->crent_pkg = NULL;
        }
        return;
    }

    for (unsigned i=0; i < ent->items; i++) {
        if (pkg_cmp_name_evr(pkg, ent->crent_pkgs[i]) == 0) {
            if (i == ent->items - 1)
                ent->crent_pkgs[i] = NULL;
            else
                memmove(&ent->crent_pkgs[i], &ent->crent_pkgs[i + 1],
                        (ent->_size - 1 - i) * sizeof(*ent->crent_pkgs));
            ent->crent_pkgs[ent->_size - 1] = NULL;
            ent->items--;
        }
    }
}


void capreq_idx_stats(const char *prefix, struct capreq_idx *idx)
{
    tn_array *keys = n_hash_keys(idx->ht);
    int i, stats[100000];
    tn_hash_it it;
    struct capreq_idx_ent *ent;
    const char *key;

    n_hash_it_init(&it, idx->ht);
    char path[1024];
    snprintf(path, sizeof(path), "/tmp/poldek_%s_stats.txt", prefix);

    FILE *f = fopen(path, "w");
    while ((ent = n_hash_it_get(&it, &key)) != NULL) {
        fprintf(f, "%d %s %s\n", ent->items, key, prefix);
    }
    fclose(f);

    memset(stats, 0, sizeof(stats));

    for (i=0; i < n_array_size(keys); i++) {
        ent = n_hash_get(idx->ht, n_array_nth(keys, i));
        stats[ent->items]++;
    }
    n_array_free(keys);
    printf("CAPREQ_IDX %s %d\n", prefix, n_hash_size(idx->ht));

    for (i=0; i < 100000; i++) {
        if (stats[i])
            printf("%s: %d: %d\n", prefix, i, stats[i]);
    }
}


const
struct capreq_idx_ent *capreq_idx_lookup(struct capreq_idx *idx,
                                         const char *capname, int capname_len)
{
    struct capreq_idx_ent *ent;
    unsigned hash = n_hash_compute_hash(idx->ht, capname, capname_len);

    if ((ent = n_hash_hget(idx->ht, capname, capname_len, hash)) == NULL)
        return NULL;

    if (ent->items == 0)
        return NULL;

    if (ent->_size == 1)        /* return only transformed ents */
        ent_transform_to_array(ent);

    return ent;
}
