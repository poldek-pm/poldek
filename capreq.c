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


#include <stdlib.h>
#include <string.h>

//#include <netinet/in.h>

#include <trurl/trurl.h>

#include "compiler.h"
#include "i18n.h"
#include "capreq.h"
#include "log.h"
#include "misc.h"
#include "pkgmisc.h"
#include "pkg_ver_cmp.h"

/* utilize rel_flags as it have 5 bits unused */
#define __SPLITTED   (1 << 7) /* same as __NAALLOC (runtime only flag) */
#define __PART       (1 << 6)
#define __NAALLOC    (1 << 7)
#define REL_RT_FLAGS __NAALLOC

// pure rel flags
#define capreq_relflags(c) (c->cr_relflags & REL_ALL)

static int capreq_store(struct capreq *cr, tn_buf *nbuf);
static struct capreq *capreq_restore(tn_alloc *na, tn_buf_it *nbufi, int *splitted);

void capreq_free_na(tn_alloc *na, struct capreq *cr)
{
    n_assert(cr->cr_relflags & __NAALLOC);
    na->na_free(na, cr);
}

void capreq_free(struct capreq *cr)
{
    if ((cr->cr_relflags & __NAALLOC) == 0)
        free(cr);
}

__inline__
int capreq_cmp_name(const struct capreq *cr1, const struct capreq *cr2)
{
    if (cr1->name == cr2->name)
        return 0;

    return strcmp(capreq_name(cr1), capreq_name(cr2));
}

__inline__ static
int capreq_cmp2name(const struct capreq *cr1, const char *name)
{
    return strcmp(capreq_name(cr1), name);
}

__inline__ static
int capreq_cmp_evr(const struct capreq *cr1, const struct capreq *cr2)
{
    register int rc;
    const char *r1, *r2;

    if (!capreq_versioned(cr1) && !capreq_versioned(cr2))
        return 0;

    if (capreq_versioned(cr1) && !capreq_versioned(cr2))
        return 1;

    if (!capreq_versioned(cr1) && capreq_versioned(cr2))
        return -1;

    if ((rc = (capreq_epoch(cr1) - capreq_epoch(cr2))))
        return rc;

    if ((rc = pkg_version_compare(capreq_ver(cr1), capreq_ver(cr2))))
        return rc;

    r1 = capreq_rel(cr1);
    r2 = capreq_rel(cr2);
    if (*r1 == '\0' && *r2 == '\0')
        return 0;

    if ((rc = pkg_version_compare(r1, r2)))
        return rc;

    return capreq_relflags(cr1) - capreq_relflags(cr2);
}

__inline__
int capreq_cmp_name_evr(const struct capreq *cr1, const struct capreq *cr2)
{
    register int rc;

    if ((rc = strcmp(capreq_name(cr1), capreq_name(cr2))))
        return rc;

    return capreq_cmp_evr(cr1, cr2);
}

__inline__
int capreq_strcmp_evr(const struct capreq *cr1, const struct capreq *cr2)
{
    register int rc;

    if ((rc = capreq_epoch(cr1) - capreq_epoch(cr2)))
        return rc;

    if ((rc = strcmp(capreq_ver(cr1), capreq_ver(cr2))))
        return rc;

    if ((rc = strcmp(capreq_rel(cr1), capreq_rel(cr2))))
        return rc;

    return (capreq_relflags(cr1) + cr1->cr_flags) -
        (capreq_relflags(cr2) + cr2->cr_flags);
}

__inline__
int capreq_strcmp_name_evr(const struct capreq *cr1, const struct capreq *cr2)
{
    if (cr1->name != cr2->name) {
        register int rc;
        register int maxlen = cr1->namelen;
        if (cr2->namelen > maxlen)
            maxlen = cr2->namelen;

        if ((rc = strncmp(capreq_name(cr1), capreq_name(cr2), maxlen)))
            return rc;
    }

    return capreq_strcmp_evr(cr1, cr2);
}


static
int do_capreq_snprintf(char *str, size_t size, const struct capreq *cr,
                       int with_char_marks)
{
    int n = 0;
    char relstr[64], *p, *s;

    n_assert(size > 0);
    if (size < 32) {
        *str = '\0';
        return 0;
    }

    s = str;
    p = relstr;
    *p = '\0';

    if (cr->cr_relflags & REL_LT)
        *p++ = '<';
    else if (cr->cr_relflags & REL_GT)
        *p++ = '>';

    if (cr->cr_relflags & REL_EQ)
        *p++ = '=';

    *p = '\0';

    if (with_char_marks) {
        if (capreq_is_bastard(cr)) {
            *s++ = '!';
            n++;
        }

        if (capreq_is_prereq(cr) || capreq_is_prereq_un(cr)) {
            *s++ = '*';
            n++;
        }

        if (capreq_is_prereq_un(cr)) {
            *s++ = '$';
            n++;
        }
    }

    if (p == relstr) {          /* no relflags */
        if (capreq_is_rpmlib(cr))
            n += n_snprintf(&s[n], size - n, "rpmlib(%s)", capreq_name(cr));
        else
            n += n_snprintf(&s[n], size - n, "%s", capreq_name(cr));

    } else {
        if (capreq_is_rpmlib(cr))
            n += n_snprintf(&s[n], size - n, "rpmlib(%s) %s ", capreq_name(cr), relstr);
        else
            n += n_snprintf(&s[n], size - n, "%s %s ", capreq_name(cr), relstr);

        if (capreq_has_epoch(cr))
            n += n_snprintf(&s[n], size - n, "%d:", capreq_epoch(cr));

        if (capreq_has_ver(cr))
            n += n_snprintf(&s[n], size - n, "%s", capreq_ver(cr));

        if (capreq_has_rel(cr)) {
            n_assert(capreq_has_ver(cr));
            n += n_snprintf(&s[n], size - n, "-%s", capreq_rel(cr));
        }
    }

    return n;
}

int capreq_snprintf(char *str, size_t size, const struct capreq *cr)
{
    return do_capreq_snprintf(str, size, cr, 0);
}

static uint8_t capreq_bufsize(const struct capreq *cr)
{
    register int max_ofs = 0;

    if (cr->cr_ep_ofs > max_ofs)
        max_ofs = cr->cr_ep_ofs;

    if (cr->cr_ver_ofs > max_ofs)
        max_ofs = cr->cr_ver_ofs;

    if (cr->cr_rel_ofs > max_ofs)
        max_ofs = cr->cr_rel_ofs;

    if (max_ofs == 0)
        max_ofs = 1;
    else
        max_ofs += strlen(&cr->_buff[max_ofs]) + 1;

    //printf("sizeof %s = %d (5 + %d + (%s) + %d)\n", capreq_snprintf_s(cr),
    //       size, max_ofs, &cr->_buf[max_ofs], strlen(&cr->_buf[max_ofs]));

    poldek_die_ifnot(max_ofs < UINT8_MAX, "%s: exceeds %db limit (%d)",
                     capreq_snprintf_s(cr), UINT8_MAX, max_ofs);

    return max_ofs;
}

static uint8_t capreq_sizeof(const struct capreq *cr)
{
    size_t size;

    size = sizeof(*cr) + capreq_bufsize(cr);

    poldek_die_ifnot(size < UINT8_MAX, "%s: exceeds %db limit (%zu)",
                     capreq_snprintf_s(cr), UINT8_MAX, size);
    return size;
}

char *capreq_str(char *str, size_t size, const struct capreq *cr)
{
    if (capreq_snprintf(str, size, cr) > 0)
        return str;
    return NULL;
}

struct capreq *capreq_new(tn_alloc *na, const char *name, int32_t epoch,
                          const char *version, const char *release,
                          int32_t relflags, int32_t flags)
{
    int name_len = 0, version_len = 0, release_len = 0;
    struct capreq *cr;
    char *buf;
    int len, isrpmreq = 0;

    if (*name == 'r' && strncmp(name, "rpmlib(", 7) == 0) {
        char *p, *q, *nname;

#if XXX_IGNORE_RPMLIB_DEPS      /* experiment, won't work */
        return NULL;
#endif
        p = (char*)name + 7;
        if ((q = strchr(p, ')'))) {
            name_len = q - p;
            nname = alloca(name_len + 1);
            memcpy(nname, p, name_len);
            nname[name_len] = '\0';
            name = nname;

            isrpmreq = 1;

        } else {
            logn(LOGERR, _("%s: invalid rpmlib capreq"), name);
        }

    } else {
        name_len = strlen(name);
    }

    len = 1;
    if (na == NULL) {           /* then copy name into _buf */
        len += name_len + 1;
    }

    if (epoch) {
        if (version == NULL)
            return NULL;
        len += sizeof(epoch);
    }

    if (version) {
        if (relflags == 0)  /* no relation and version is presented, invalid */
            return NULL;

        n_assert(relflags != 0);

        version_len = strlen(version);
        len += version_len + 1;
    }

    if (release) {
        if (version == NULL)
            return NULL;

        release_len = strlen(release);
        len += release_len + 1;
    }

    if (na)
        cr = na->na_malloc(na, sizeof(*cr) + len);
    else
        cr = n_malloc(sizeof(*cr) + len);

    buf = cr->_buff;
    *buf++ = '\0';          /* set buf[0] to '\0' */

    cr->cr_flags = cr->cr_relflags = 0;
    cr->cr_ep_ofs = cr->cr_ver_ofs = cr->cr_rel_ofs = 0;

    if (na) {
        /* string dedup alloc */
        if (name_len > UINT8_MAX) {
            const tn_str16 *ent = na->na_alloc_str16(na, name, name_len);
            cr->name = ent->str;
            cr->namelen = ent->len;
        } else {
            const tn_str8 *ent = na->na_alloc_str8(na, name, name_len);
            cr->name = ent->str;
            cr->namelen = ent->len;
        }
    }

    if (epoch) {
        cr->cr_ep_ofs = buf - cr->_buff;
        memcpy(buf, &epoch, sizeof(epoch));
        buf += sizeof(epoch);
    }

    if (version != NULL) {
        cr->cr_ver_ofs = buf - cr->_buff;
        memcpy(buf, version, version_len);
        buf += version_len ;
        *buf++ = '\0';
    }

    if (release != NULL) {
        cr->cr_rel_ofs = buf - cr->_buff;
        memcpy(buf, release, release_len);
        buf += release_len ;
        *buf++ = '\0';
    }

    if (na == NULL) {                  /* put name at buffer end */
        memcpy(buf, name, name_len);
        cr->name = buf;
        cr->namelen = name_len;
        buf += name_len;
        *buf++ = '\0';
    }

    cr->cr_relflags = relflags;
    cr->cr_flags = flags;
    if (isrpmreq)
        cr->cr_flags |= CAPREQ_RPMLIB;

    if (na)
        cr->cr_relflags |= __NAALLOC;

    return cr;
}

struct capreq *capreq_new_evr(tn_alloc *na, const char *name, char *evr,
                              int32_t relflags, int32_t flags)
{
    const char *version = NULL, *release = NULL;
    int32_t epoch = 0;

    if (evr && !poldek_util_parse_evr(evr, &epoch, &version, &release))
        return NULL;

    return capreq_new(na, name, epoch, version, release, relflags, flags);
}

#define malloced(cr) (cr->cr_relflags & __NAALLOC) == 0

struct capreq *capreq_clone(tn_alloc *na, const struct capreq *cr)
{
    uint8_t size;
    struct capreq *newcr;

    if (malloced(cr) && na == NULL) {
        size = capreq_sizeof(cr) + cr->namelen + 1;
        newcr = n_malloc(size);
        memcpy(newcr, cr, size);
        newcr->name = &newcr->_buff[capreq_bufsize(newcr)];
        n_assert(*newcr->name == *cr->name);
        return newcr;
    }

    return capreq_new(na, capreq_name(cr),
                      capreq_epoch(cr), capreq_ver(cr), capreq_rel(cr),
                      cr->cr_relflags, cr->cr_flags & ~(REL_RT_FLAGS));
}

int32_t capreq_epoch_(const struct capreq *cr)
{
    int32_t epoch;

    memcpy(&epoch, &cr->_buff[cr->cr_ep_ofs], sizeof(epoch));
    return epoch;
}

tn_array *capreq_arr_new_ex(int size, void **data)
{
    tn_array *arr;
    arr = n_array_new_ex(size > 0 ? size : 2,
                         (tn_fn_free)capreq_free,
                         (tn_fn_cmp)capreq_cmp_name_evr,
                         data);
    n_array_ctl(arr, TN_ARRAY_AUTOSORTED);
    return arr;
}

tn_array *capreq_arr_new(int size)
{
    return capreq_arr_new_ex(size, NULL);
}

__inline__
int capreq_arr_find(tn_array *capreqs, const char *name)
{
    /* capreq_cmp2name is compilant with capreq_cmp_name_evr */
    if (!n_array_is_sorted(capreqs))
        n_array_sort(capreqs);

    return n_array_bsearch_idx_ex(capreqs, name,
                                  (tn_fn_cmp)capreq_cmp2name);
}

__inline__
int capreq_arr_contains(tn_array *capreqs, const char *name)
{
    if (!n_array_is_sorted(capreqs))
        n_array_sort(capreqs);  /* capreq_cmp2name */

    return n_array_bsearch_idx_ex(capreqs, name,
                                  (tn_fn_cmp)capreq_cmp2name) > -1;
}


tn_buf *capreq_arr_join(tn_array *capreqs, tn_buf *nbuf, const char *sep)
{
    int i, size = n_array_size(capreqs);

    if (sep == NULL)
        sep = ", ";

    if (nbuf == NULL)
        nbuf = n_buf_new(32 * n_array_size(capreqs));

    for (i=0; i < size; i++) {
        n_buf_printf(nbuf, "%s%s", capreq_snprintf_s(n_array_nth(capreqs, i)),
                     i < size - 1 ? sep  : "");
    }
    return nbuf;
}

/* split capreq into UINT8_MAX (pndir limit) chunks   */
static tn_array *split_capreq(struct capreq *cr, int actual_store_size)
{
    int base_size = actual_store_size - cr->namelen;
    unsigned namelen = cr->namelen;
    char *name;

    n_strdupapl(capreq_name(cr), namelen, &name);
    n_assert(strlen(name) == namelen);

    tn_array *parts = capreq_arr_new(2);

    while (*name) {
        int pos = UINT8_MAX - base_size;
        char *end = name;
        while (*end && end - name < pos) {
            end++;
        }
        pos = end - name;

        DBGF("part%d cut_at=%d left=[%s]\n", n_array_size(parts), pos, end);
        char c = *end;
        *end = '\0';

        struct capreq *cap = capreq_new(NULL, name,
                                        0, NULL, NULL,
                                        0, 0);
        n_array_push(parts, cap);

        *end = c;
        name = end;
        namelen -= pos;
        n_assert(strlen(name) == namelen);
    }
    DBGF("splitted %d into %d parts\n", cr->namelen, n_array_size(parts));

    return parts;
}

/* store format:

  0. size          uint8_t
  1. flags&offsets uint8_t[5]
  2. '\0'          char         // legacy, needless in fact
  3. name          char[]

  -- if versioned --
  4. '\0'
  5. epoch         uint32_t in network byte order
  6. '\0'
  7. version       char[]
  8. '\0'
  9. release       char[]
*/
static int capreq_store(struct capreq *cr, tn_buf *nbuf)
{
    register int i;
    uint32_t size;
    uint8_t bufsize;
    uint8_t cr_buf[5];
    uint8_t cr_flags = 0, cr_relflags = 0;
    tn_array *parts = NULL;

    /* do not store runtime flags */
    if (cr->cr_flags & CAPREQ_RT_FLAGS) {
        cr_flags = cr->cr_flags;
        cr->cr_flags &= ~CAPREQ_RT_FLAGS;
    }

    if (cr->cr_relflags & REL_RT_FLAGS) {
        cr_relflags = cr->cr_relflags;
        cr->cr_relflags &= ~REL_RT_FLAGS;
    }

    cr_buf[0] = cr->cr_relflags;
    cr_buf[1] = cr->cr_flags;
    cr_buf[2] = cr->cr_ep_ofs;
    cr_buf[3] = cr->cr_ver_ofs;
    cr_buf[4] = cr->cr_rel_ofs;

    bufsize = capreq_bufsize(cr) - 1; /* without last '\0' */
    size = sizeof(cr_buf) + bufsize;

    const char *cr_name = capreq_name(cr);
    uint16_t cr_namelen = cr->namelen;

    if (size + cr_namelen + 1 > UINT8_MAX) { /* over pndir UINT8_MAX limit */
        parts = split_capreq(cr, size + cr_namelen + 1);
        n_assert(parts);
        n_assert(n_array_size(parts));

        /* take short name from first part */
        struct capreq *cap = n_array_shift(parts);
        cr_name = cap->name;
        cr_namelen = cap->namelen;
        cr_buf[0] |= __SPLITTED; /* mark as splitted */
        capreq_free(cap);
    }

    size += cr_namelen + 1;       /* +1 for leading '\0' */
    DBGF("store size %d\n", size);
    n_assert(size <= UINT8_MAX);

    for (i=2; i < 5; i++) {         /* move offsets by name length */
        if (cr_buf[i])
            cr_buf[i] += cr_namelen + 1;
    }

    n_buf_add_int8(nbuf, (uint8_t)size);
    n_buf_add(nbuf, cr_buf, sizeof(cr_buf));
    n_buf_add_int8(nbuf, '\0');
    n_buf_add(nbuf, cr_name, cr_namelen);

    if (bufsize) {          /* versioned? */
        int32_t epoch = 0;

        if (cr->cr_ep_ofs) {
            epoch = capreq_epoch(cr);
            int32_t nepoch = n_hton32(epoch);
            memcpy(&cr->_buff[cr->cr_ep_ofs], &nepoch, sizeof(nepoch));
        }

        n_buf_add(nbuf, cr->_buff, bufsize);

        if (cr->cr_ep_ofs)      /* restore epoch */
            memcpy(&cr->_buff[cr->cr_ep_ofs], &epoch, sizeof(epoch));
    }

    if (cr_flags)
        cr->cr_flags = cr_flags;

    if (cr_relflags)
        cr->cr_relflags = cr_relflags;

    if (parts) {
        for (i = 0; i < n_array_size(parts); i++) {
            struct capreq *cap = n_array_nth(parts, i);
            cap->cr_relflags |= __PART;
            capreq_store(cap, nbuf);
        }
        n_array_free(parts);
    }

    return 1;
}

static struct capreq *capreq_restore_na(tn_alloc *na, tn_buf_it *nbufi, int *splitted)
{
    struct capreq *cr;
    register int i;
    uint8_t size = 0, *cr_buf, *buff;
    uint8_t phcr_buf[5];          /* placeholder,  for sizeof */
    unsigned char *p, *name = NULL;
    size_t name_len = 0;

    n_buf_it_get_int8(nbufi, &size);

    cr_buf = n_buf_it_get(nbufi, sizeof(phcr_buf));
    if (cr_buf == NULL)
        return NULL;

    size -= sizeof(phcr_buf);
    buff = n_buf_it_get(nbufi, size);

    buff++;                     /* skip '\0' */
    size--;
    if ((p = memchr(buff, '\0', size))) { /* versioned? */
        name = buff;
        name_len = p - buff;
        size -= name_len;
        buff = p + 1;

    } else {
        unsigned char *s = alloca(size + 1);
        memcpy(s, buff, size);  /* would be nice w/o memcpy */
        s[size] = '\0';
        name = s;
        name_len = size;
        size = 0;
    }

    cr = na->na_malloc(na, sizeof(*cr) + size + 1);

    cr->cr_relflags = cr_buf[0];

    if (splitted)
        *splitted = 0;

    if (cr->cr_relflags & __SPLITTED) {
        if (splitted)
            *splitted = 1;

        cr->cr_relflags &= ~__SPLITTED;
    }

    cr->cr_relflags |= __NAALLOC;
    cr->cr_flags = cr_buf[1];

    for (i=2; i < 5; i++) {
        if (cr_buf[i])
            cr_buf[i] -= name_len + 1;
    }

    cr->cr_ep_ofs   = cr_buf[2];
    cr->cr_ver_ofs  = cr_buf[3];
    cr->cr_rel_ofs  = cr_buf[4];

    if (name_len > UINT8_MAX) {
        const tn_str16 *ent = na->na_alloc_str16(na, (const char *)name, name_len);
        n_assert(name_len == ent->len);
        cr->name = ent->str;
        cr->namelen = ent->len;
    } else {
        const tn_str8 *ent = na->na_alloc_str8(na, (const char *)name, name_len);
        n_assert(name_len == ent->len);
        cr->name = ent->str;
        cr->namelen = ent->len;
    }

    cr->_buff[0] = '\0';
    if (size) {
        memcpy(&cr->_buff[1], buff, size);
        cr->_buff[size] = '\0';
    }

    if (cr->cr_ep_ofs) {
        int32_t epoch = n_ntoh32(capreq_epoch(cr));
        memcpy(&cr->_buff[cr->cr_ep_ofs], &epoch, sizeof(epoch));
    }
    DBGF("cr %s\n", capreq_snprintf_s(cr));

	//printf("cr %s: %d, %d, %d, %d, %d\n", capreq_snprintf_s(cr),
	//cr_bufp[0], cr_bufp[1], cr_bufp[2], cr_bufp[3], cr_bufp[4]);
    //printf("REST* %s %d -> %d\n", capreq_snprintf_s(cr),
    //          strlen(capreq_snprintf_s(cr)), capreq_sizeof(cr));

    return cr;
}

static struct capreq *capreq_restore_wo_na(tn_buf_it *nbufi, int *splitted)
{
    struct capreq *cr;
    register int i;
    uint8_t size = 0, *cr_buf, *buff;
    uint8_t phcr_buf[5];          /* placeholder,  for sizeof */
    unsigned char *p, *name = NULL;
    size_t name_len = 0;

    n_buf_it_get_int8(nbufi, &size);

    cr_buf = n_buf_it_get(nbufi, sizeof(phcr_buf));
    if (cr_buf == NULL)
        return NULL;

    size -= sizeof(phcr_buf);
    buff = n_buf_it_get(nbufi, size);

    cr = n_malloc(sizeof(*cr) + size + 1);
    cr->cr_relflags = cr_buf[0];

    if (splitted)
        *splitted = 0;

    if (cr->cr_relflags & __SPLITTED) {
        if (splitted)
            *splitted = 1;

        cr->cr_relflags &= ~__SPLITTED;
    }

    cr->_buff[0] = '\0';
    buff++;                     /* skip leading zero */
    size--;

    if (!capreq_versioned(cr)) {
        memcpy(&cr->_buff[1], buff, size);
        cr->_buff[1 + size] = '\0';

        cr->name = &cr->_buff[1];
        cr->namelen = size;

    } else {
        p = memchr(buff, '\0', size); /* end of name */
        n_assert(p);

        name = buff;
        name_len = p - buff;

        buff = p + 1;
        size -= (name_len + 1);

        int n = 1;
        memcpy(&cr->_buff[n], buff, size);
        n += size;
        cr->_buff[n++] = '\0';

        memcpy(&cr->_buff[n], name, name_len); /* put name at buf's end */
        cr->_buff[n + name_len] = '\0';

        cr->name = &cr->_buff[n];
        cr->namelen = name_len;
    }

    for (i=2; i < 5; i++) {
        if (cr_buf[i])
            cr_buf[i] -= cr->namelen + 1;
    }

    cr->cr_flags = cr_buf[1];
    cr->cr_ep_ofs   = cr_buf[2];
    cr->cr_ver_ofs  = cr_buf[3];
    cr->cr_rel_ofs  = cr_buf[4];


    if (cr->cr_ep_ofs) {
        int32_t epoch = n_ntoh32(capreq_epoch(cr));
        memcpy(&cr->_buff[cr->cr_ep_ofs], &epoch, sizeof(epoch));
    }
    DBGF("cr %s\n", capreq_snprintf_s(cr));

    return cr;
}

static struct capreq *capreq_restore(tn_alloc *na, tn_buf_it *nbufi, int *splitted)
{
    if (!na)
        return capreq_restore_wo_na(nbufi, splitted);

    return capreq_restore_na(na, nbufi, splitted);
}

int capreq_arr_store_n(tn_array *arr)
{
    register int i, n = 0;

    for (i=0; i < n_array_size(arr); i++) {
        struct capreq *cr = n_array_nth(arr, i);
        if (capreq_is_bastard(cr))
            continue;
        n++;
    }
    return n;
}

int capreq_arr_store(tn_array *arr, tn_buf *nbuf)
{
    uint32_t size;
    uint16_t arr_size;
    int i, off;

    poldek_die_ifnot(n_array_size(arr) < UINT16_MAX,
                     "too many capabilities per package (max=%u)", UINT16_MAX);

    for (i=0; i < n_array_size(arr); i++) {
        struct capreq *cr = n_array_nth(arr, i);
        n_assert(!capreq_is_bastard(cr));
    }

    arr_size = n_array_size(arr);
    if (arr_size == 0)
        return 0;

    n_array_isort_ex(arr, (tn_fn_cmp)capreq_strcmp_name_evr);

    if (nbuf == NULL)
        nbuf = n_buf_new(24 * arr_size);

    off = n_buf_tell(nbuf);
    n_buf_add_int16(nbuf, 0);   /* size placeholder */
    n_buf_add_int16(nbuf, arr_size);   /* array size placeholder */

    uint16_t real_arr_size = 0;
    for (i=0; i < n_array_size(arr); i++) {
        struct capreq *cr = n_array_nth(arr, i);

        int cr_off = n_buf_tell(nbuf);
        if (!capreq_store(cr, nbuf)) {
            logn(LOGWARN, "%s: store failed", capreq_snprintf_s(cr));
            n_buf_seek(nbuf, cr_off, SEEK_SET);
            break;
        }

        size = n_buf_tell(nbuf) - off - sizeof(uint16_t);

        /* 64K limit overflow => just skip the rest */
        if (size > UINT16_MAX - 1) {
            n_buf_seek(nbuf, cr_off, SEEK_SET);
            break;
        }
        real_arr_size++;

        DBGF("store %s (len=%zu, sizeof=%d)\n", capreq_snprintf_s(cr),
             strlen(capreq_snprintf_s(cr)), capreq_sizeof(cr));
    }

    n_buf_puts(nbuf, "\n");

    size = n_buf_tell(nbuf) - off - sizeof(uint16_t);
    poldek_die_ifnot(size <= UINT16_MAX, "capabilities size %u exceeds 64K limit", size);

    /* update size and if needed, arr_size at the beginning */
    int endoff = n_buf_tell(nbuf); /* remember last offset */

    n_buf_seek(nbuf, off, SEEK_SET);
    n_buf_add_int16(nbuf, size);
    if (real_arr_size != arr_size)
        n_buf_add_int16(nbuf, real_arr_size);

    n_buf_seek(nbuf, endoff, SEEK_SET);

    DBGF("%d (at %d), arr_size = %d, real arr size %d\n",
         size, off, arr_size, real_arr_size);

    return real_arr_size;
}

static struct capreq *restore_parts(tn_alloc *na, struct capreq *cr, tn_buf_it *nbufi, int *splitted)
{
    struct capreq *part = NULL;
    tn_buf *namebuf = n_buf_new(512);
    int nparts = 0;

    n_buf_write_z(namebuf, cr->name, cr->namelen);

    *splitted = 0;
    while ((part = capreq_restore(na, nbufi, splitted))) {
        if ((part->cr_relflags & __PART) == 0) { /* regular capreq restored */
            break;
        }

        DBGF("cont%d %d + %d\n", nparts, n_buf_size(namebuf), part->namelen);
        n_buf_write_z(namebuf, part->name, part->namelen);
        capreq_free(part);
        part = NULL;
        nparts++;
        n_assert(*splitted == 0);
    }
    n_assert(nparts > 0);

    DBGF("restored from %d parts, size %d\n", nparts + 1, n_buf_size(namebuf));
    const tn_str16 *ent = na->na_alloc_str16(na, n_buf_ptr(namebuf), n_buf_size(namebuf));
    cr->name = ent->str;
    cr->namelen = ent->len;

    n_buf_free(namebuf);

    /* part can be regular capreq */
    return part;
}

tn_array *capreq_arr_restore(tn_alloc *na, tn_buf *nbuf)
{
    tn_array       *arr;
    struct capreq  *cr;
    tn_buf_it      nbufi;
    uint16_t       arr_size = 0;
    void           **cr_buf;
    register int   i, n;

    n_buf_it_init(&nbufi, nbuf);
    n_buf_it_get_int16(&nbufi, &arr_size);

    //cr_buf = alloca(arr_size * sizeof(void*));
    cr_buf = n_malloc(arr_size * sizeof(void*));
    n = 0;
    for (i=0; i < arr_size; i++) {
        int splitted = 0;
        if ((cr = capreq_restore(na, &nbufi, &splitted)) == NULL)
            continue;

        if (capreq_is_bastard(cr)) /* should not happen */
            continue;

        cr_buf[n++] = cr;

        /* next capreq can be splitted too */
        while (splitted) {
            DBGF("SPLITTED[%d of %d] %s\n", i, arr_size, cr->name);

            struct capreq *part = restore_parts(na, cr, &nbufi, &splitted);
            poldek_die_if(part == NULL && splitted, "%s", "null splitted occured");

            if (part) {
                cr = part;
                cr_buf[n++] = cr;
                i++;
            }
        }
    }

    if (n == 0) {
        free(cr_buf);
        return NULL;
    }

    arr = capreq_arr_new_ex(n, cr_buf);
    return arr;
}

struct restore_struct {
    tn_alloc *na;
    tn_array *arr;
};

static int capreq_arr_restore_fn(tn_buf *nbuf, struct restore_struct *rs)
{
    rs->arr = capreq_arr_restore(rs->na, nbuf);
    return rs->arr != NULL;
}

tn_array *capreq_arr_restore_st(tn_alloc *na, tn_stream *st)
{
    struct restore_struct rs = { na, NULL };

    n_buf_restore_ex(st, NULL, TN_BUF_STORE_16B,
                     (int (*)(tn_buf *, void*))capreq_arr_restore_fn, &rs);

    return rs.arr;
}

int capreq_arr_restore_skip_st(tn_stream *st)
{
    n_buf_restore_skip(st, TN_BUF_STORE_16B);
    return 1;
}

static
int cmp_uniq(struct capreq *cr1, struct capreq *cr2)
{
    register int rc;
    if ((rc = capreq_cmp_name(cr1, cr2)) == 0)
        rc = capreq_versioned(cr2);

    if (rc == 0)
        logn(LOGNOTICE, "uniq: keep %s, removed %s %d",
             capreq_snprintf_s(cr1), capreq_snprintf_s0(cr2),
             capreq_versioned(cr2));

    return rc;
}

static
int capreq_cmp_name_evr_rev(struct capreq *cr1, struct capreq *cr2)
{
    register int rc;

    if ((rc = strcmp(capreq_name(cr1), capreq_name(cr2))))
        return rc;

    rc = -capreq_cmp_evr(cr1, cr2);
    DBGF("cmp %s %s = %d\n", capreq_snprintf_s(cr1), capreq_snprintf_s0(cr2), rc);
    return rc;
}

tn_array *capreq_arr_remove_redundant(tn_array *arr)
{
    n_array_sort_ex(arr, (tn_fn_cmp)capreq_cmp_name_evr_rev);

#if ENABLE_TRACE
    {
        int i;
        DBGF("start\n");
        for (i=0; i<n_array_size(arr); i++) {
            struct capreq *cr = n_array_nth(arr, i);
            DBGF(" %s\n", capreq_snprintf_s(cr));
        }
    }
#endif

    n_array_uniq_ex(arr, (tn_fn_cmp)cmp_uniq);
    return arr;
}
