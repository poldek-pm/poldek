/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@k2.net.pl>

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


#include <stdlib.h>
#include <string.h>

//#include <netinet/in.h>

#include <trurl/trurl.h>

#include "i18n.h"
#include "capreq.h"
#include "log.h"
#include "misc.h"
#include "pkgmisc.h"
#include "pkg_ver_cmp.h"

#define __NAALLOC   (1 << 7)
#define REL_RT_FLAGS __NAALLOC

static void capreq_store(struct capreq *cr, tn_buf *nbuf);
static struct capreq *capreq_restore(tn_alloc *na, tn_buf_it *nbufi);


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
int capreq_cmp_name(struct capreq *cr1, struct capreq *cr2) 
{
    return strcmp(capreq_name(cr1), capreq_name(cr2));
}

__inline__
int capreq_cmp2name(struct capreq *cr1, const char *name)
{
    return strcmp(capreq_name(cr1), name);
}

__inline__ static
int capreq_cmp_evr(struct capreq *cr1, struct capreq *cr2) 
{
    register int rc;
    
    if (capreq_versioned(cr1) && !capreq_versioned(cr2))
        return 1;

    if (!capreq_versioned(cr1) && capreq_versioned(cr2))
        return -1;

    if ((rc = (capreq_epoch(cr1) - capreq_epoch(cr2))))
        return rc;

    if ((rc = pkg_version_compare(capreq_ver(cr1), capreq_ver(cr2))))
        return rc;
    
    if ((rc = pkg_version_compare(capreq_rel(cr1), capreq_rel(cr2))))
        return rc;
    
    return cr1->cr_relflags - cr2->cr_relflags;
}

__inline__
int capreq_cmp_name_evr(struct capreq *cr1, struct capreq *cr2) 
{
    register int rc;
    
    if ((rc = strcmp(capreq_name(cr1), capreq_name(cr2))))
        return rc;

    return capreq_cmp_evr(cr1, cr2);
}

__inline__
int capreq_strcmp_evr(struct capreq *cr1, struct capreq *cr2) 
{
    register int rc;

    if ((rc = capreq_epoch(cr1) - capreq_epoch(cr2)))
        return rc;
        
    if ((rc = strcmp(capreq_ver(cr1), capreq_ver(cr2))))
        return rc;

    if ((rc = strcmp(capreq_rel(cr1), capreq_rel(cr2))))
        return rc;

    return (cr1->cr_relflags + cr1->cr_flags) -
        (cr2->cr_relflags + cr2->cr_flags);
}

__inline__
int capreq_strcmp_name_evr(struct capreq *cr1, struct capreq *cr2) 
{
    register int rc;

    if ((rc = strcmp(capreq_name(cr1), capreq_name(cr2))))
        return rc;

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
        n_assert(*capreq_ver(cr) == '\0');
        if (capreq_is_rpmlib(cr))
            n += n_snprintf(&s[n], size - n, "rpmlib(%s)", capreq_name(cr));
        else
            n += n_snprintf(&s[n], size - n, "%s", capreq_name(cr));
        
    } else {
        n_assert(*capreq_ver(cr));
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

uint8_t capreq_bufsize(const struct capreq *cr) 
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
    
    max_ofs += strlen(&cr->_buf[max_ofs]) + 1;
    //printf("sizeof %s = %d (5 + %d + (%s) + %d)\n", capreq_snprintf_s(cr),
    //       size, max_ofs, &cr->_buf[max_ofs], strlen(&cr->_buf[max_ofs]));

    poldek_die_ifnot(max_ofs < UINT8_MAX, "%s: exceeds %db limit (%d)",
                     capreq_snprintf_s(cr), UINT8_MAX, max_ofs);
    
    return max_ofs;
}

static
uint8_t capreq_sizeof(const struct capreq *cr) 
{
    size_t size;

    size = sizeof(*cr) + capreq_bufsize(cr);
    
    poldek_die_ifnot(size < UINT8_MAX, "%s: exceeds %db limit (%d)",
                     capreq_snprintf_s(cr), UINT8_MAX, size);
    return size;
}


char *capreq_snprintf_s(const struct capreq *cr) 
{
    static char str[256];
    capreq_snprintf(str, sizeof(str), cr);
    return str;
}

char *capreq_snprintf_s0(const struct capreq *cr) 
{
    static char str[256];
    capreq_snprintf(str, sizeof(str), cr);
    return str;
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
    
    len = 1 + name_len + 1;

    if (epoch) {
        if (version == NULL)
            return NULL;
        len += sizeof(epoch);
    }
        
    if (version) {
        version_len = strlen(version);
        n_assert(relflags != 0);
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

    cr->cr_flags = cr->cr_relflags = 0;
    cr->cr_ep_ofs = cr->cr_ver_ofs = cr->cr_rel_ofs = 0;
        
    buf = cr->_buf;
    *buf++ = '\0';          /* set buf[0] to '\0' */
    
    memcpy(buf, name, name_len);
    buf += name_len;
    *buf++ = '\0';
    
    if (epoch) {
        cr->cr_ep_ofs = buf - cr->_buf;
        memcpy(buf, &epoch, sizeof(epoch));
        buf += sizeof(epoch);
    }

    if (version != NULL) {
        cr->cr_ver_ofs = buf - cr->_buf;
        memcpy(buf, version, version_len);
        buf += version_len ;
        *buf++ = '\0';
    }
    
    if (release != NULL) {
        cr->cr_rel_ofs = buf - cr->_buf;
        memcpy(buf, release, release_len);
        buf += release_len ;
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


struct capreq *capreq_new_evr(const char *name, char *evr, int32_t relflags,
                              int32_t flags)
{
    const char *version = NULL, *release = NULL;
    int32_t epoch = 0;

    if (evr && !poldek_util_parse_evr(evr, &epoch, &version, &release))
        return NULL;
    
    return capreq_new(NULL, name, epoch, version, release, relflags, flags);
}

struct capreq *capreq_clone(tn_alloc *na, const struct capreq *cr) 
{
    uint8_t size;
    struct capreq *newcr;
    
    size = capreq_sizeof(cr);
    if (na)
        newcr = na->na_malloc(na, size);
    else
        newcr = n_malloc(size);
    
    memcpy(newcr, cr, size);
    if (na)
        newcr->cr_relflags |= __NAALLOC;
    else
        newcr->cr_relflags &= ~(__NAALLOC);
    
    return newcr;
}


int32_t capreq_epoch_(const struct capreq *cr)
{
    int32_t epoch;

    memcpy(&epoch, &cr->_buf[cr->cr_ep_ofs], sizeof(epoch));
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
    return n_array_bsearch_idx_ex(capreqs, name,
                                  (tn_fn_cmp)capreq_cmp2name);
}

static
void capreq_store(struct capreq *cr, tn_buf *nbuf) 
{
    int32_t epoch, nepoch;
    uint8_t size, bufsize;
    uint8_t cr_buf[5];
    uint8_t cr_flags = 0, cr_relflags = 0;

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
	
    bufsize = capreq_bufsize(cr) - 1; /* without '\0' */
    size = sizeof(cr_buf) + bufsize;
	
    n_buf_add_int8(nbuf, size);
    n_buf_add(nbuf, cr_buf, sizeof(cr_buf));

    if (cr->cr_ep_ofs) {
        epoch = capreq_epoch(cr);
        nepoch = n_hton32(epoch);
        memcpy(&cr->_buf[cr->cr_ep_ofs], &nepoch, sizeof(nepoch));
    }

    n_buf_add(nbuf, cr->_buf, bufsize);
    
    if (cr->cr_ep_ofs) 
        memcpy(&cr->_buf[cr->cr_ep_ofs], &epoch, sizeof(epoch)); /* restore epoch */

    if (cr_flags)
		cr->cr_flags = cr_flags;

    if (cr_relflags)
		cr->cr_relflags = cr_relflags;
}

static
struct capreq *capreq_restore(tn_alloc *na, tn_buf_it *nbufi) 
{
    struct capreq *cr;
    uint8_t size, *cr_bufp;
    uint8_t cr_buf[5];          /* placeholder,  for sizeof */
    
    
    n_buf_it_get_int8(nbufi, &size);
    cr_bufp = n_buf_it_get(nbufi, sizeof(cr_buf));
    if (cr_bufp == NULL)
        return NULL;

    size -= sizeof(cr_buf);
#if XXX_IGNORE_RPMLIB_DEPS
    if (cr_bufp[1] & CAPREQ_RPMLIB) {
        n_buf_it_get(nbufi, size);
        return NULL;
    }
#endif
    if (na)
        cr = na->na_malloc(na, sizeof(*cr) + size + 1);
    else
        cr = n_malloc(sizeof(*cr) + size + 1);
    
    cr->cr_relflags = cr_bufp[0];
    cr->cr_flags    = cr_bufp[1];
    cr->cr_ep_ofs   = cr_bufp[2];
    cr->cr_ver_ofs  = cr_bufp[3];
    cr->cr_rel_ofs  = cr_bufp[4];

    if (na)
        cr->cr_relflags |= __NAALLOC;

    cr->_buf[size] = '\0';
    cr_bufp = n_buf_it_get(nbufi, size);
    memcpy(cr->_buf, cr_bufp, size);

    if (cr->cr_ep_ofs) {
        int32_t epoch = n_ntoh32(capreq_epoch(cr));
        memcpy(&cr->_buf[cr->cr_ep_ofs], &epoch, sizeof(epoch));
    }
	//printf("cr %s: %d, %d, %d, %d, %d\n", capreq_snprintf_s(cr), 
	//cr_bufp[0], cr_bufp[1], cr_bufp[2], cr_bufp[3], cr_bufp[4]);
    //printf("REST* %s %d -> %d\n", capreq_snprintf_s(cr),
    //          strlen(capreq_snprintf_s(cr)), capreq_sizeof(cr));
    
    return cr;
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

/* XXX: Caution: every change of this function may broke pdir
   index format compatibility */
tn_buf *capreq_arr_store(tn_array *arr, tn_buf *nbuf, int n)
{
    int32_t size;
    int16_t arr_size;
    int i, off;

    poldek_die_ifnot(n_array_size(arr) < INT16_MAX,
                     "too many capabilities per package (max=%d)", INT16_MAX);
    
    arr_size = n;
    if (n == 0) {
        for (i=0; i < n_array_size(arr); i++) {
            struct capreq *cr = n_array_nth(arr, i);
            if (!capreq_is_bastard(cr))
                arr_size++;
        }
    }
    n_assert(arr_size);
    if (arr_size == 0)
        return NULL;
    
    n_array_isort_ex(arr, (tn_fn_cmp)capreq_strcmp_name_evr);

    if (nbuf == NULL)
        nbuf = n_buf_new(16 * arr_size);
    
    off = n_buf_tell(nbuf);
    n_buf_add_int16(nbuf, 0);   /* fake size */
    n_buf_add_int16(nbuf, arr_size);
    
    for (i=0; i < n_array_size(arr); i++) {
        struct capreq *cr = n_array_nth(arr, i);
        if (!capreq_is_bastard(cr)) {
            capreq_store(cr, nbuf);
            DBGF("store %s (len=%d, sizeof=%d)\n", capreq_snprintf_s(cr),
                 strlen(capreq_snprintf_s(cr)), capreq_sizeof(cr));
        }
    }

    n_buf_puts(nbuf, "\n");
    //printf("tells %d\n", n_buf_tell(nbuf));
    
    size = n_buf_tell(nbuf) - off - sizeof(uint16_t);

    poldek_die_ifnot(size < UINT16_MAX, "capabilities size exceeds 64K limit");
    
    n_buf_seek(nbuf, off, SEEK_SET);
    n_buf_add_int16(nbuf, size);
    n_buf_seek(nbuf, 0, SEEK_END);

    DBGF("capreq_arr_store %d (at %d), arr_size = %d\n",
         size, off, arr_size);

    return nbuf;
}

tn_array *capreq_arr_restore(tn_alloc *na, tn_buf *nbuf) 
{
    tn_array       *arr;
    struct capreq  *cr;
    tn_buf_it      nbufi;
    int16_t        arr_size;
    void           **cr_buf;
    register int   i, n;

    n_buf_it_init(&nbufi, nbuf);
    n_buf_it_get_int16(&nbufi, &arr_size);

    DBGF("%d, arr_size = %d\n", n_buf_size(nbuf), arr_size);
    
    //cr_buf = alloca(arr_size * sizeof(void*));
    cr_buf = n_malloc(arr_size * sizeof(void*));
    n = 0;
    for (i=0; i < arr_size; i++) {
        if ((cr = capreq_restore(na, &nbufi))) {
            if (capreq_is_bastard(cr))
                continue;
            cr_buf[n++] = cr;
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

    DBGF("AFTER capreq_arr_restore_f %lu, %lu\n", off,
         n_stream_tell(st));
    
    return rs.arr;
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

