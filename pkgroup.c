/*
  Copyright (C) 2001 Pawel A. Gajda <mis@pld-linux.org>

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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <trurl/trurl.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "pkgu.h"
#include "pkgroup.h"
#include "misc.h"
#ifdef HAVE_RPMORG
# include "pm/rpmorg/pm_rpm.h"
#else
# include "pm/rpm/pm_rpm.h"
#endif

struct pkgroup_idx {
    tn_hash *ht;                /* name => struct pkgroup */
    tn_array *arr;
    int _refcnt;
};

struct tr {
    char lang[64];
    char name[0];
};

struct pkgroup {
    uint16_t  id;
    uint16_t  ntrs;
    tn_hash  *trs;                /* translations */
    char     name[0];
};

static
struct tr *tr_new(const char *lang, const char *name)
{
    struct tr *tr;
    int len;


    len = strlen(name) + 1;
    tr = n_malloc(sizeof(*tr) + len);
    n_strncpy(tr->lang, lang, sizeof(tr->lang));
    memcpy(tr->name, name, len);
    return tr;
}

static
int tr_cmp(struct tr *tr1, struct tr *tr2)
{
    return strcmp(tr1->lang, tr2->lang);
}


static
int tr_store(struct tr *tr, tn_buf *nbuf)
{
    int      len, n;
    uint8_t  nlen;
    char     buf[255];

    len = strlen(tr->lang) + strlen(tr->name) + 1 /*:*/;
    n_assert(len < UINT8_MAX);

    nlen = len;
    n_buf_write(nbuf, &nlen, sizeof(nlen));
    n = n_snprintf(buf, sizeof(buf), "%s:%s", tr->lang, tr->name);
    DBGF("%s\n", buf);
    n_assert(n == len);

    return n_buf_write(nbuf, buf, len) == len;
}

static
struct tr *tr_restore_st(tn_stream *st)
{
    int      len;
    uint8_t  nlen;
    char     buf[255], *p;

    if (n_stream_read(st, &nlen, sizeof(nlen)) != sizeof(nlen))
        return NULL;

    len = nlen;
    if (n_stream_read(st, buf, len) != len)
        return NULL;

    buf[len] = '\0';

    if ((p = strchr(buf, ':')) == NULL)
        return NULL;

    *p++ = '\0';

    return tr_new(buf, p);
}


static
struct tr *tr_restore(tn_buf_it *it)
{
    int      len;
    uint8_t  nlen;
    char     buf[255], *p;

    if (!n_buf_it_read_uint8(it, &nlen))
        return NULL;

    len = nlen;

    if (n_buf_it_read(it, buf, len) != len)
        return NULL;

    buf[len] = '\0';

    if ((p = strchr(buf, ':')) == NULL)
        return NULL;

    *p++ = '\0';

    return tr_new(buf, p);
}


static
struct pkgroup *pkgroup_new(int id, const char *name)
{
    struct pkgroup *gr;
    int len;

    n_assert(id < UINT16_MAX);

    len = strlen(name) + 1;

    if ((gr = n_malloc(sizeof(*gr) + len)) == NULL)
        return gr;

    gr->id = id;
    gr->ntrs = 0;
    memcpy(gr->name, name, len);

    gr->trs = n_hash_new(21, free);
    n_hash_ctl(gr->trs, TN_HASH_NOCPKEY);

    return gr;
}

static
void pkgroup_free(struct pkgroup *gr)
{
    if (gr->trs) {
        n_hash_free(gr->trs);
        gr->trs = NULL;
    }
    free(gr);
}

static
int pkgroup_cmp(const struct pkgroup *gr1, const struct pkgroup *gr2)
{
    return gr1->id - gr2->id;
}

static
int pkgroup_add_tr(struct pkgroup *gr, struct tr *tr)
{
    if (n_hash_exists(gr->trs, tr->lang))
        return 1;

    if (n_hash_insert(gr->trs, tr->lang, tr)) {
        gr->ntrs++;
        return 1;
    }

    return 0;
}

static
int pkgroup_add(struct pkgroup *gr, const char *lang, const char *name)
{
    struct tr *tr;


    if (n_hash_exists(gr->trs, lang))
        return 1;

    if ((tr = tr_new(lang, name))) {
        if (n_hash_insert(gr->trs, lang, tr)) {
            gr->ntrs++;
            return 1;
        }
    }

    return 0;
}


static
void map_fn_trs_keys(const char *lang, void *tr, void *array)
{
    lang = lang;
    n_array_push(array, tr);
}


static
int pkgroup_store(struct pkgroup *gr, tn_buf *nbuf)
{
    uint8_t  nlen;
    tn_array *arr;
    int      len, i;


    if (!n_buf_write_uint32(nbuf, gr->id))
        return 0;

    len = strlen(gr->name) + 1;
    n_assert(len < UINT8_MAX);

    nlen = len;

    if (!n_buf_write_uint8(nbuf, nlen))
        return 0;

    DBGF("%d %s\n", gr->id, gr->name);
    if (n_buf_write(nbuf, gr->name, len) != len)
        return 0;

    if (!n_buf_write_uint32(nbuf, gr->ntrs))
        return 0;

    arr = n_array_new(8, NULL, (tn_fn_cmp)tr_cmp);
    n_hash_map_arg(gr->trs, map_fn_trs_keys, arr);
    n_array_sort(arr);
    for (i=0; i < n_array_size(arr); i++)
        tr_store(n_array_nth(arr, i), nbuf);

    n_array_free(arr);
    return 1;
}


static
struct pkgroup *pkgroup_restore_st(tn_stream *st)
{
    int             nerr = 0, i;
    uint32_t        nid, ntrs;
	uint8_t         nlen;
    struct pkgroup  *gr;
    char            name[256];

    if (!n_stream_read_uint32(st, &nid))
        return 0;


    if (!n_stream_read_uint8(st, &nlen))
        return 0;

    if (n_stream_read(st, name, nlen) != nlen)
        return 0;

    name[nlen] = '\0';
    DBGF("gid %d, name[%d] = %s\n", nid, nlen, name);

    if (!n_stream_read_uint32(st, &ntrs))
        return 0;

    gr = pkgroup_new(nid, name);

    for (i=0; i < (int)ntrs; i++) {
        struct tr *tr;

        if ((tr = tr_restore_st(st)) == NULL)
            nerr++;
        else
            pkgroup_add_tr(gr, tr);
        //printf("gr tr %s %s\n", tr->lang, tr->name);
    }

    if (nerr > 0) {
        pkgroup_free(gr);
        gr = NULL;
    }

    return gr;
}


static
struct pkgroup *pkgroup_restore(tn_buf_it *it)
{
    int             nerr = 0, i;
    uint32_t        nid, ntrs;
	uint8_t         nlen;
    struct pkgroup  *gr;
    char            name[256], *p;

    if (!n_buf_it_read_uint32(it, &nid))
        return 0;

    if (!n_buf_it_read_uint8(it, &nlen))
        return 0;

    if ((p = n_buf_it_get(it, nlen)) == NULL)
        return 0;

    memcpy(name, p, nlen);
    name[nlen] = '\0';
    DBGF("gid %d, name[%d] = %s\n", nid, nlen, name);

    if (!n_buf_it_read_uint32(it, &ntrs))
        return 0;

    gr = pkgroup_new(nid, name);

    for (i=0; i < (int)ntrs; i++) {
        struct tr *tr;

        if ((tr = tr_restore(it)) == NULL)
            nerr++;
        else
            pkgroup_add_tr(gr, tr);
        //printf("gr tr %s %s\n", tr->lang, tr->name);
    }

    if (nerr > 0) {
        pkgroup_free(gr);
        gr = NULL;
    }

    return gr;
}


struct pkgroup_idx *pkgroup_idx_new(void)
{
    struct pkgroup_idx *idx;

    idx = n_malloc(sizeof(*idx));
    idx->ht = n_hash_new(512, NULL);
    n_hash_ctl(idx->ht, TN_HASH_NOCPKEY);
    idx->arr = n_array_new(256, (tn_fn_free)pkgroup_free,
                           (tn_fn_cmp)pkgroup_cmp);
    idx->_refcnt = 0;

    return idx;
}

void pkgroup_idx_free(struct pkgroup_idx *idx)
{
    if (idx->_refcnt > 0) {
        idx->_refcnt--;
        return;
    }

    if (idx->ht) {
        n_hash_free(idx->ht);
        idx->ht = NULL;
    }

    if (idx->arr) {
        n_array_free(idx->arr);
        idx->arr = NULL;
    }

    free(idx);
}

struct pkgroup_idx *pkgroup_idx_link(struct pkgroup_idx *idx)
{
    idx->_refcnt++;
    return idx;
}


int pkgroup_idx_store(struct pkgroup_idx *idx, tn_buf *nbuf)
{
    int i;

    n_array_sort(idx->arr);
    if (!n_buf_write_int32(nbuf, n_array_size(idx->arr)))
        return 0;

    for (i=0; i < n_array_size(idx->arr); i++)
        pkgroup_store(n_array_nth(idx->arr, i), nbuf);

    return 1;
}


struct pkgroup_idx *pkgroup_idx_restore_st(tn_stream *st, unsigned flags)
{
    uint32_t nsize;
    struct pkgroup_idx *idx;
    int i;

    flags = flags;
    if (!n_stream_read_uint32(st, &nsize))
        return NULL;

    idx = pkgroup_idx_new();

    for (i=0; i < (int)nsize; i++)
        n_array_push(idx->arr, pkgroup_restore_st(st));

//    if (flags & PKGROUP_MKNAMIDX)
//        ;
    n_array_sort(idx->arr);
    n_stream_seek(st, 1, SEEK_CUR); /* eat '\n' */
    return idx;
}

struct pkgroup_idx *pkgroup_idx_restore(tn_buf_it *it, unsigned flags)
{
    uint32_t nsize;
    struct pkgroup_idx *idx;
    int i;

    flags = flags;
    if (!n_buf_it_read_uint32(it, &nsize))
        return NULL;

    idx = pkgroup_idx_new();

    for (i=0; i < (int)nsize; i++)
        n_array_push(idx->arr, pkgroup_restore(it));

//    if (flags & PKGROUP_MKNAMIDX)
//        ;
    n_array_sort(idx->arr);
//    n_stream_seek(st, 1, SEEK_CUR); /* eat '\n' */
    return idx;
}


int pkgroup_idx_update_rpmhdr(struct pkgroup_idx *idx, void *rpmhdr)
{
    tn_array           *langs;
    char               **groups;
    int                i, ngroups = 0;
    struct pkgroup     *gr = NULL;
    Header             h;

    h = rpmhdr;
    if ((langs = pm_rpmhdr_langs(h)) == NULL)
        return 0;

    if (!pm_rpmhdr_get_raw_entry(h, RPMTAG_GROUP, (void*)&groups, &ngroups)) {
        n_array_free(langs);
        return 0;
    }

    n_assert(n_array_size(langs) >= ngroups);

    DBGF("ngroups %d, %d\n", ngroups, n_array_size(langs));
    for (i=0; i < ngroups; i++) {
        const char *lang = n_array_nth(langs, i);
        DBGF("   gr[%d of %d] %s\n", i, ngroups, groups[i]);

        if (n_str_eq(lang, "C")) {
            if ((gr = n_hash_get(idx->ht, groups[i])) == NULL) {
                gr = pkgroup_new(n_array_size(idx->arr) + 1, groups[i]);
                n_array_push(idx->arr, gr);
                n_hash_insert(idx->ht, gr->name, gr);
            }
            break;
        }
    }

    if (gr != NULL) {
        for (i=0; i < ngroups; i++) {
            const char *lang = n_array_nth(langs, i);

            if (n_str_eq(lang, "C") || *groups[i] == '\0')
                continue;

            pkgroup_add(gr, lang, groups[i]);
        }
    }

    free(groups);
    n_array_free(langs);

    if (gr)
        DBGF("gr_add %d %s\n", gr->id, gr->name);

    if (gr)
        return gr->id;
    return 0;
}


int pkgroup_idx_add(struct pkgroup_idx *idx, const char *group)
{
    struct pkgroup     *gr = NULL;

    if ((gr = n_hash_get(idx->ht, group)) == NULL) {
        gr = pkgroup_new(n_array_size(idx->arr) + 1, group);
        n_array_push(idx->arr, gr);
        n_hash_insert(idx->ht, gr->name, gr);
    }

    if (gr)
        DBGF("gr_add %d %s\n", gr->id, gr->name);

    if (gr)
        return gr->id;
    return 0;
}

int pkgroup_idx_add_i18n(struct pkgroup_idx *idx, int groupid,
                         const char *group, const char *lang)
{
    struct pkgroup *gr, tmpgr;

    n_assert(lang);
    n_assert(strcmp(lang, "C") != 0);

    tmpgr.id = groupid;
    if ((gr = n_array_bsearch(idx->arr, &tmpgr)) == NULL)
        return 0;

    pkgroup_add(gr, lang, group);
    return gr->id;
}


static const char *find_tr(const char *lang, const struct pkgroup *gr)
{
    struct tr *tr;
    const char **langs, **p;

    langs = n_str_tokl(lang, ":");

    p = langs;
    while (*p) {
        char   *l, *q, *sep = "@._";
        int    len;

        if ((tr = n_hash_get(gr->trs, *p)))
            return tr->name;

        len = strlen(*p) + 1;
        l = alloca(len + 1);
        memcpy(l, *p, len);

        while (*sep) {
            if ((q = strchr(l, *sep))) {
                *q = '\0';

                if ((tr = n_hash_get(gr->trs, l)))
                    return tr->name;
            }
            sep++;
        }

        p++;
    }

    n_str_tokl_free(langs);

    return gr->name;
}

const char *pkgroup(struct pkgroup_idx *idx, int groupid)
{
    struct pkgroup *gr, tmpgr;
    const char *lang, *group = NULL;

    tmpgr.id = groupid;
    if ((gr = n_array_bsearch(idx->arr, &tmpgr)) == NULL)
        return NULL;

    if (gr->trs == NULL)
        group = gr->name;

    else {
        lang = lc_messages_lang();
        group = find_tr(lang, gr);
    }

    if (group)
        return _(group);

    return group;
}

static int pkgroupid(struct pkgroup_idx *idx, const char *name)
{
    int i;

    for (i=0; i < n_array_size(idx->arr); i++) {
        struct pkgroup *gr = n_array_nth(idx->arr, i);
        if (strcmp(gr->name, name) == 0)
            return gr->id;
    }

    return -1;
}

#if ENABLE_TRACE
static void dumpidx(struct pkgroup_idx *idx, const char *prefix)
{
    struct pkgroup *gr;
    int i;

    for (i=0; i < n_array_size(idx->arr); i++) {
        gr = n_array_nth(idx->arr, i);
        printf("%s: %d %s\n", prefix, gr->id, gr->name);
    }
}
#endif


int pkgroup_idx_remap_groupid(struct pkgroup_idx *idx_to,
                              struct pkgroup_idx *idx_from,
                              int groupid, int merge)
{
    struct pkgroup *gr, tmpgr;
    int new_id;


    tmpgr.id = groupid;


    DBGF("%d\n", groupid);
    //dumpidx(idx_from, "FROM");
    //dumpidx(idx_to,   "TO  ");

    if ((gr = n_array_bsearch(idx_from->arr, &tmpgr)) == NULL) {
        logn(LOGERR, "%d: gid not found", groupid);
        n_assert(0);
    }


    if ((new_id = pkgroupid(idx_to, gr->name)) < 0) {
        if (!merge) {
            logn(LOGERR, "%s: group not found", gr->name);
            n_assert(0);

        } else {
            new_id = n_array_size(idx_to->arr) + 1;
            gr = pkgroup_new(new_id, gr->name);
            n_array_push(idx_to->arr, gr);
            n_array_sort(idx_to->arr);
        }
    }

    return new_id;
}
