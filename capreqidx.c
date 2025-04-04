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
    DBGF("ent %p, %p %d, %d\n", ent, ent->pkgs, ent->_size, ent->items);
    if (ent->_size > 1)
        free(ent->pkgs);
}

int capreq_idx_init(struct capreq_idx *idx, unsigned type, int nelem)
{
    idx->flags = type;

    MEMINF("START");
    idx->na = n_alloc_new(4, TN_ALLOC_OBSTACK);
    idx->ht = n_oash_new_na(idx->na, nelem, (tn_fn_free)capreq_ent_free);
    n_oash_ctl(idx->ht, TN_HASH_NOCPKEY | TN_HASH_REHASH);
    MEMINF("END");
    return 1;
}


void capreq_idx_destroy(struct capreq_idx *idx)
{
    n_oash_free(idx->ht);
    n_alloc_free(idx->na);
    memset(idx, 0, sizeof(*idx));
}

static int ent_transform_to_array(struct capreq_idx_ent *ent)
{
    struct pkg *tmp;
    n_assert(ent->_size == 1);   /* pkgs is NOT allocated */
    tmp = ent->pkg;
    ent->pkgs = n_malloc(2 * sizeof(*ent->pkgs));
    ent->pkgs[0] = tmp;
    ent->_size = 2;
    return 1;
}

static inline void idx_ent_sort(struct capreq_idx_ent *ent)
{
    register size_t i, j;

    for (i = 1; i < ent->items; i++) {
        register struct pkg *tmp = ent->pkgs[i];

        j = i;
        while (j > 0 && tmp - ent->pkgs[j - 1] < 0) {
            ent->pkgs[j] = ent->pkgs[j - 1];
            j--;
        }
        ent->pkgs[j] = tmp;
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

	if ((cmp_re = ent->pkgs[i] - pkg) == 0) {
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
const char skip_PREFIXES[][20] = {
    "/usr/share/doc",
    "/usr/share/info",
    "/usr/share/man",
    "/usr/share/locale",
    "/usr/share/icons"
};

int skip_LENGTHS[sizeof(skip_PREFIXES) / sizeof(skip_PREFIXES[0])] = { 0 };

/* b) well-known FHS directories and some generic rpm specific caps */
const char skip_CAPS[][16] = {
    "/etc",
    "/bin",
    "/usr/bin",
    "/usr/sbin",
    "/usr/lib",
    "/usr/share",
    "/usr/include",
    "elf(buildid)",
    "rtld(GNU_HASH)"
};

tn_hash *skip_CAPS_H = NULL;

static void skip_CAPS_free(void) {
    if (skip_CAPS_H != NULL) {
        n_hash_free(skip_CAPS_H);
        skip_CAPS_H = NULL;
    }
}

inline static int indexable_cap(const char *name, int len, unsigned raw_hash)
{
    if (!skip_CAPS_H) {
        skip_CAPS_H = n_hash_new(32, NULL);
        n_hash_ctl(skip_CAPS_H, TN_HASH_NOCPKEY);

        for (size_t i = 0; i < sizeof(skip_CAPS) / sizeof(skip_CAPS[0]); i++) {
            n_hash_insert(skip_CAPS_H, skip_CAPS[i], skip_CAPS[i]);
            i++;
        }

        for (size_t i = 0; i < sizeof(skip_PREFIXES) / sizeof(skip_PREFIXES[0]); i++) {
            skip_LENGTHS[i] = strlen(skip_PREFIXES[i]);
        }

        atexit(skip_CAPS_free);
    }

    uint32_t hash = n_hash_compute_index_hash(skip_CAPS_H, raw_hash);
    if (n_hash_hexists(skip_CAPS_H, name, len, hash))
        return 0;

    if (*name ==  '/') {
        for (size_t i = 0; i < sizeof(skip_PREFIXES) / sizeof(skip_PREFIXES[0]); i++) {
            if (strncmp(name, skip_PREFIXES[i], skip_LENGTHS[i]) == 0) {
                return 0;
            }
            i++;
        }
    }

    return 1;
}

static int cap_is_owned_by_pkg(const char *capname, const struct pkg *pkg) {
    int owned = 0;

    if (pkg->caps && capreq_arr_contains(pkg->caps, capname))
        owned = 1;
    else if (pkg->reqs && capreq_arr_contains(pkg->reqs, capname))
        owned = 1;
    else if (pkg->cnfls && capreq_arr_contains(pkg->cnfls, capname))
        owned = 1;

    return owned;
}

int capreq_idx_add(struct capreq_idx *idx,
                   const char *capname, int capname_len,
                   const struct pkg *pkg)
{

    uint32_t raw_khash = n_hash_compute_raw_hash(capname, capname_len);

    /* skip redundant/ needless requirements */
    if (idx->flags & CAPREQ_IDX_REQ) {
        if (!indexable_cap(capname, capname_len, raw_khash)) {
            DBGF("skip %s\n", capname);
            return 1;
        }
    } else if ((idx->flags & CAPREQ_IDX_CAP)) {
        if (strcmp(capname, "elf(buildid)") == 0)
            return 1;
    }

    /* costly check */
    /* n_assert(cap_is_owned_by_pkg(capname, pkg)); */

    /* do not copy capname, it should be allocated by pkg */
    void **entptr = n_oash_get_insert(idx->ht, capname, capname_len);
    n_assert(entptr);

    if (*entptr == NULL) {
        struct capreq_idx_ent *ent = idx->na->na_malloc(idx->na, sizeof(*ent));
        ent->_size = 1;
        ent->items = 1;
        ent->pkg = (struct pkg*)pkg; /* XXX const */
        *entptr = ent;

#if ENABLE_TRACE
        if ((n_oash_size(idx->ht) % 1000) == 0)
            n_oash_stats(idx->ht);
#endif

    } else {
        struct capreq_idx_ent *ent = *entptr;
        if (ent->_size == 1) {    /* crent_pkgs is NOT allocated */
            ent_transform_to_array(ent);

            /* save some reallocs as many needs libc/m/pthread, micro optimization, XXX */
            if (*capname == 'l' && (strncmp(capname, "libc", 4) == 0 ||
                                    strncmp(capname, "libm", 4) == 0 ||
                                    strncmp(capname, "libpthread", 4) == 0)) {
                ent->_size = 4096;
                ent->pkgs = n_realloc(ent->pkgs,
                                      ent->_size * sizeof(*ent->pkgs));
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
            ent->pkgs = n_realloc(ent->pkgs,
                                  ent->_size * sizeof(*ent->pkgs));
        }

        ent->pkgs[ent->items++] = (struct pkg*)pkg; /* XXX const */

        if (idx->flags & CAPREQ_IDX_CAP) { /* sort to prevent duplicates */
            idx_ent_sort(ent);
        }
    }

    return 1;
}


void capreq_idx_remove(struct capreq_idx *idx, const char *capname,
                       const struct pkg *pkg)
{
    struct capreq_idx_ent *ent;

    if ((ent = n_oash_get(idx->ht, capname)) == NULL)
        return;

    if (ent->_size == 1) {      /* no crent_pkgs */
        if (pkg_cmp_name_evr(pkg, ent->pkg) == 0) {
            ent->items = 0;
            ent->pkg = NULL;
        }
        return;
    }

    for (unsigned i=0; i < ent->items; i++) {
        if (pkg_cmp_name_evr(pkg, ent->pkgs[i]) == 0) {
            if (i == ent->items - 1)
                ent->pkgs[i] = NULL;
            else
                memmove(&ent->pkgs[i], &ent->pkgs[i + 1],
                        (ent->_size - 1 - i) * sizeof(*ent->pkgs));
            ent->pkgs[ent->_size - 1] = NULL;
            ent->items--;
        }
    }
}

void capreq_idx_stats(const char *prefix, struct capreq_idx *idx)
{
    tn_array *keys = n_oash_keys(idx->ht);
    int i, stats[100000];
    tn_oash_it it;
    struct capreq_idx_ent *ent;
    const char *key;

    n_oash_it_init(&it, idx->ht);
    char path[1024];
    snprintf(path, sizeof(path), "/tmp/poldek_%s_stats.txt", prefix);

    FILE *f = fopen(path, "w");
    while ((ent = n_oash_it_get(&it, &key)) != NULL) {
        fprintf(f, "%d %s %s\n", ent->items, key, prefix);
    }
    fclose(f);

    memset(stats, 0, sizeof(stats));

    for (i=0; i < n_array_size(keys); i++) {
        ent = n_oash_get(idx->ht, n_array_nth(keys, i));
        stats[ent->items]++;
    }
    n_array_free(keys);
    printf("CAPREQ_IDX %s %d\n", prefix, n_oash_size(idx->ht));

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
    unsigned hash = n_oash_compute_hash(idx->ht, capname, capname_len);

    if ((ent = n_oash_hget(idx->ht, capname, capname_len, hash)) == NULL)
        return NULL;

    if (ent->items == 0)
        return NULL;

    if (ent->_size == 1)        /* return only transformed ents */
        ent_transform_to_array(ent);

    return ent;
}
