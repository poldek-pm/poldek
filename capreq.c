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

#include <netinet/in.h>

#include <rpm/rpmlib.h>
#include <trurl/narray.h>
#include <trurl/nassert.h>
#include <trurl/n_snprintf.h>

#include "i18n.h"
#include "rpmadds.h"
#include "capreq.h"
#include "log.h"
#include "misc.h"
#include "h2n.h"
#include "misc.h"

static void *(*capreq_alloc_fn)(size_t) = malloc;
static void (*capreq_free_fn)(void*) = free;

void set_capreq_allocfn(void *(*cr_allocfn)(size_t), void (*cr_freefn)(void*),
                         void **prev_alloc, void **prev_free)
{
    if (prev_alloc) {
        n_assert(prev_free);
        *prev_alloc = capreq_alloc_fn;
        *prev_free = capreq_free_fn;
    }
    
    capreq_alloc_fn = cr_allocfn;
    capreq_free_fn = cr_freefn;
}


void capreq_free(struct capreq *cr) 
{
    capreq_free_fn(cr);
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

__inline__
int capreq_cmp_name_evr(struct capreq *cr1, struct capreq *cr2) 
{
    register int rc;
    
    if ((rc = strcmp(capreq_name(cr1), capreq_name(cr2))))
        return rc;

    if ((rc = (capreq_epoch(cr1) - capreq_epoch(cr2))))
        return rc;
    
    if ((rc = (capreq_ver(cr1) - capreq_ver(cr2))))
        return rc;
    
    if ((rc = rpmvercmp(capreq_rel(cr1), capreq_rel(cr2))))
        return rc;
    
    return cr1->cr_relflags - cr2->cr_relflags;
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

int capreq_fprintf(FILE *stream, const struct capreq *cr) 
{
    char relstr[64], *p;
    
    p = relstr;
    *p = '\0';
    
    if (cr->cr_relflags & REL_LT) 
        *p++ = '<';
    else if (cr->cr_relflags & REL_GT) 
        *p++ = '>';

    if (cr->cr_relflags & REL_EQ) 
        *p++ = '=';

    *p = '\0';

    fprintf(stream, "%s%s%s%s", 
            capreq_is_bastard(cr) ? "!" : "",
            capreq_is_prereq(cr) ? "*" : "",
            capreq_is_prereq_un(cr) ? "^" : "",
            capreq_name(cr));
    
    if (p == relstr) {
        n_assert(*capreq_ver(cr) == '\0');
        
    } else {
        n_assert(*capreq_ver(cr));

        fprintf(stream, "(%s ", relstr);

        if (capreq_has_epoch(cr)) {
            fprintf(stream, "%u:", capreq_epoch(cr));
        }

        if (capreq_has_ver(cr)) {
            fprintf(stream, "%s", capreq_ver(cr));
        }

        if (capreq_has_rel(cr)) {
            n_assert(capreq_has_ver(cr));
            fprintf(stream, "-%s)", capreq_rel(cr));
        }
    }
    return 1;
}

static
int capreq_snprintf_(char *str, size_t size, const struct capreq *cr,
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

    if (p == relstr) {
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
    return capreq_snprintf_(str, size, cr, 0);
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
    
    n_assert (max_ofs < UINT8_MAX);
    return max_ofs;
}


uint8_t capreq_sizeof(const struct capreq *cr) 
{
    size_t size;

    size = sizeof(*cr) + capreq_bufsize(cr);
    n_assert (size < UINT8_MAX);
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


struct capreq *capreq_new(const char *name, int32_t epoch,
                          const char *version, const char *release,
                          int32_t relflags, int32_t flags) 
{
    int name_len = 0, version_len = 0, release_len = 0;
    struct capreq *cr;
    char *buf;
    int len, isrpmreq = 0;
    
    if (*name == 'r' && strncmp(name, "rpmlib(", 7) == 0) {
        char *p, *q, *nname;

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
        len += version_len + 1;
    }
        
    if (release) {
        if (version == NULL)
            return NULL;
            
        release_len = strlen(release);
        len += release_len + 1;
    }
    
    if ((cr = capreq_alloc_fn(sizeof(*cr) + len)) == NULL)
        return NULL;

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

    return cr;
}


struct capreq *capreq_new_evr(const char *name, char *evr, int32_t relflags, int32_t flags)
{
    char *version = NULL, *release = NULL;
    int32_t epoch = 0;

    if (evr && !parse_evr(evr, &epoch, &version, &release))
        return NULL;
    
    return capreq_new(name, epoch, version, release, relflags, flags);
}

struct capreq *capreq_new_capreq(const struct capreq *cr) 
{
    uint8_t size;
    struct capreq *new_cr;
    
    size = capreq_sizeof(cr);
    new_cr = capreq_alloc_fn(size);
    memcpy(new_cr, cr, size);
    return new_cr;
}

int32_t capreq_epoch_(const struct capreq *cr)
{
    int32_t epoch;

    memcpy(&epoch, &cr->_buf[cr->cr_ep_ofs], sizeof(epoch));
    return epoch;
}


tn_array *capreq_arr_new(int size) 
{
    tn_array *arr;
    arr = n_array_new(size > 0 ? size : 2, capreq_free_fn,
                      (tn_fn_cmp)capreq_cmp_name_evr);
    n_array_ctl(arr, TN_ARRAY_AUTOSORTED);
    return arr;
}

__inline__
int capreq_arr_find(tn_array *capreqs, const char *name)
{
    return n_array_bsearch_idx_ex(capreqs, name,
                                  (tn_fn_cmp)capreq_cmp2name);
}

tn_array *capreqs_get(tn_array *arr, const Header h, int crtype) 
{
    struct capreq *cr;
    int t1, t2, t3, c1 = 0, c2 = 0, c3 = 0;
    char **names, **versions, *label;
    int  *flags, *tags;
    int  i;

    int req_tags[3] = {
        RPMTAG_REQUIRENAME, RPMTAG_REQUIREVERSION, RPMTAG_REQUIREFLAGS
    };

    int prov_tags[3] = {
        RPMTAG_PROVIDENAME, RPMTAG_PROVIDEVERSION, RPMTAG_PROVIDEFLAGS
    };

    int cnfl_tags[3] = {
        RPMTAG_CONFLICTNAME, RPMTAG_CONFLICTVERSION, RPMTAG_CONFLICTFLAGS
    };

    int obsl_tags[3] = {
        RPMTAG_OBSOLETENAME, RPMTAG_OBSOLETEVERSION, RPMTAG_OBSOLETEFLAGS
    };
    
    n_assert(arr);

    switch (crtype) {
        case CRTYPE_CAP:
            tags = prov_tags;
            label = "prov";
            break;
            
        case CRTYPE_REQ:
            tags = req_tags;
            label = "req";
            break;
            
        case CRTYPE_CNFL:
            tags = cnfl_tags;
            label = "cnfl";
            break;

        case CRTYPE_OBSL:
            tags = obsl_tags;
            label = "cnfl";
            break;
            
        default:
            tags = NULL;
            label = NULL;
            n_assert(0);
            die();
    }

    names = NULL;
    if (!headerGetEntry(h, *tags, (void*)&t1, (void*)&names, &c1))
        return NULL;
    
    n_assert(names);

    
    tags++;
    versions = NULL;
    if (headerGetEntry(h, *tags, (void*)&t2, (void*)&versions, &c2)) {
        n_assert(t2 == RPM_STRING_ARRAY_TYPE);
        n_assert(versions);
        n_assert(c2);
        
    } else if (crtype == CRTYPE_REQ) { /* reqs should have version tag */
        rpm_headerEntryFree(names, t1);
        return 0;
    }
    
    
    tags++;
    flags = NULL;
    if (headerGetEntry(h, *tags, (void*)&t3, (void*)&flags, &c3)) {
        n_assert(t3 == RPM_INT32_TYPE);
        n_assert(flags);
        n_assert(c3);
        
    } else if (crtype == CRTYPE_REQ) {  /* reqs should have flags */
        rpm_headerEntryFree(names, t1);
        rpm_headerEntryFree(versions, t2);
        return 0;
    }

    if (c2) 
        if (c1 != c2) {
            logn(LOGERR, "read %s: nnames (%d) != nversions (%d), broken rpm",
                 label, c1, c2);
#if 0            
            for (i=0; i<c1; i++) 
                printf("n %s\n", names[i]);
            for (i=0; i<c2; i++) 
                printf("v %s\n", versions[i]);
#endif            
            goto l_err_endfunc;
        }
        
    if (c2 != c3) {
        logn(LOGERR, "read %s: nversions %d != nflags %d, broken rpm", label,
            c2, c3);
        goto l_err_endfunc;
    }

    for (i=0; i<c1 ; i++) {
        int epoch_len = 0, name_len = 0, version_len = 0, release_len = 0;
        unsigned int len = 1, has_ver, isrpmreq = 0;
        char *name, *version, *release;
        int32_t epoch;
        char *buf;

        name = names[i];
        
        if (*name == 'r' && strncmp(name, "rpmlib(", 7) == 0) {
            char *p, *q, *nname;

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

        len += name_len + 1;
        
        has_ver = c2 && *versions[i];
        
        if (has_ver) {
            parse_evr(versions[i], &epoch, &version, &release);
            
            if (epoch)
                epoch_len = sizeof(epoch);
            
            version_len = strlen(version);
            
            if (release != NULL)
                release_len = strlen(release);
            
            len += epoch_len + version_len + 1 + release_len + 1;
        }
        
        n_assert(len <= UINT8_MAX);
        cr = capreq_alloc_fn(sizeof(*cr) + len);
        if (cr == NULL) 
            goto l_err_endfunc;
        
        memset(cr, 0, sizeof(*cr) + len);
        
        buf = cr->_buf;
        *buf++ = '\0';          /* set buf[0] to '\0' */
        
        memcpy(buf, name, name_len);
        buf += name_len;
        *buf++ = '\0';
        
        if (has_ver) {
            n_assert(*versions[i]);
            
            if (epoch) {
                cr->cr_ep_ofs = name_len + 2;
                memcpy(buf, &epoch, sizeof(epoch));
                buf += sizeof(epoch);
            }
                
            cr->cr_ver_ofs = buf - cr->_buf;
            memcpy(buf, version, version_len);
            buf += version_len ;
            *buf++ = '\0';

            if (release != NULL) {
                cr->cr_rel_ofs = buf - cr->_buf;
                memcpy(buf, release, release_len);
                buf += release_len;
                *buf++ = '\0';
            }
        }
        
        if (c3) {               /* translate flags to poldek one */
            register uint32_t flag = flags[i];

            if (flag & RPMSENSE_LESS) 
                cr->cr_relflags |= REL_LT;
            
            if (flag & RPMSENSE_GREATER) 
                cr->cr_relflags |= REL_GT;
            
            if (flag & RPMSENSE_EQUAL) 
                cr->cr_relflags |= REL_EQ;

                
#ifndef HAVE_RPM_EXTDEPS
            if (flag & RPMSENSE_PREREQ) {
                n_assert(crtype == CRTYPE_REQ);
                cr->cr_flags |= CAPREQ_PREREQ | CAPREQ_PREREQ_UN;
            }
#else
            if (isLegacyPreReq(flag)) { /* prepared by rpm < 4.0.2  */
                n_assert(crtype == CRTYPE_REQ);
                cr->cr_flags |= CAPREQ_PREREQ | CAPREQ_PREREQ_UN;
                
            } else if (isInstallPreReq(flag)) {
                n_assert(crtype == CRTYPE_REQ);
                cr->cr_flags |= CAPREQ_PREREQ;
                
            } else if (isErasePreReq(flag)) {
                n_assert(crtype == CRTYPE_REQ);
                cr->cr_flags |= CAPREQ_PREREQ_UN;
            }
#endif /* HAVE_RPM_EXTDEPS */
        }
        

        if (crtype == CRTYPE_OBSL) 
            cr->cr_flags |= CAPREQ_OBCNFL;

        if (isrpmreq)
            cr->cr_flags |= CAPREQ_RPMLIB;
        
        msg(4, "%s%s: %s\n",
            cr->cr_flags & CAPREQ_PREREQ ?
              (crtype == CRTYPE_OBSL ? "obsl" : "pre" ):"", 
            label, capreq_snprintf_s(cr));
        n_array_push(arr, cr);
    }
    
    rpm_headerEntryFree(names, t1);
    rpm_headerEntryFree(versions, t2);
    rpm_headerEntryFree(flags, t3);

    return arr;
    
 l_err_endfunc:
    rpm_headerEntryFree(names, t1);
    rpm_headerEntryFree(versions, t2);
    rpm_headerEntryFree(flags, t3);
    return NULL;
}


tn_array *capreq_pkg(tn_array *arr, int32_t epoch, 
                     const char *name, int name_len, 
                     const char *version, int version_len, 
                     const char *release, int release_len) 
{
    struct capreq *cr;
    unsigned int len = 1, epoch_len = 0;
    char *buf;


    n_assert(arr);
    if (epoch)
        epoch_len = sizeof(epoch);
            
    len += name_len + 1 + epoch_len + version_len + 1 + release_len + 1;
    n_assert(len <= UINT8_MAX);
        
    cr = capreq_alloc_fn(sizeof(*cr) + len);
    if (cr == NULL)
        return NULL;
    
    memset(cr, 0, sizeof(*cr) + len);
    buf = cr->_buf;
    *buf++ = '\0';          /* set buf[0] to '\0' */

    strcat(buf, name);
    buf += name_len;
    *buf++ = '\0';
        
    if (epoch) {
        cr->cr_ep_ofs = name_len + 2;
        memcpy(buf, &epoch, sizeof(epoch));
        buf += sizeof(epoch);
    }
                
    cr->cr_ver_ofs = buf - cr->_buf;
    strcpy(buf, version);
    buf += version_len ;
    *buf++ = '\0';

    cr->cr_rel_ofs = buf - cr->_buf;
    strcpy(buf, release);
    buf += release_len ;
    *buf++ = '\0';
    
    cr->cr_relflags |= REL_EQ;

    n_array_push(arr, cr);
    return arr;
}

void capreq_store(struct capreq *cr, tn_buf *nbuf) 
{
    int32_t epoch, nepoch;
    uint8_t size, bufsize;
    uint8_t cr_buf[5];


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
        nepoch = hton32(epoch);
        memcpy(&cr->_buf[cr->cr_ep_ofs], &nepoch, sizeof(nepoch));
    }

    n_buf_add(nbuf, cr->_buf, bufsize);
    
    if (cr->cr_ep_ofs) 
        memcpy(&cr->_buf[cr->cr_ep_ofs], &epoch, sizeof(epoch)); /* restore epoch */
        
}

struct capreq *capreq_restore(tn_buf_it *nbufi) 
{
    struct capreq *cr;
    uint8_t size, *cr_bufp;
    uint8_t cr_buf[5];          /* placeholder,  for sizeof */
    
    
    n_buf_it_get_int8(nbufi, &size);
    cr_bufp = n_buf_it_get(nbufi, sizeof(cr_buf));
    if (cr_buf == NULL)
        return NULL;

    size -= sizeof(cr_buf);
    
    if ((cr = capreq_alloc_fn(sizeof(*cr) + size + 1)) == NULL)
        return NULL;

    cr->cr_relflags = cr_bufp[0];
    cr->cr_flags    = cr_bufp[1];
    cr->cr_ep_ofs   = cr_bufp[2];
    cr->cr_ver_ofs  = cr_bufp[3];
    cr->cr_rel_ofs  = cr_bufp[4];

    cr->_buf[size] = '\0';
    cr_bufp = n_buf_it_get(nbufi, size);
    memcpy(cr->_buf, cr_bufp, size);
    
    if (cr->cr_ep_ofs) {
        int32_t epoch = ntoh32(capreq_epoch(cr));
        memcpy(&cr->_buf[cr->cr_ep_ofs], &epoch, sizeof(epoch));
    }

    //printf("REST* %s %d -> %d\n", capreq_snprintf_s(cr),
    //          strlen(capreq_snprintf_s(cr)), capreq_sizeof(cr));
    
    return cr;
}


int capreq_arr_store(tn_array *arr, FILE *stream, const char *prefix)
{
    tn_buf *nbuf;
    int32_t size;
    int16_t size16, arr_size;
    int i, rc;

    if ((arr_size = n_array_size(arr)) == 0)
        return 1;
    n_assert(n_array_size(arr) < INT16_MAX);
    n_array_isort_ex(arr, (tn_fn_cmp)capreq_strcmp_name_evr);
    
    nbuf = n_buf_new(64 * arr_size);
    n_buf_add_int16(nbuf, arr_size);
    
    for (i=0; i<n_array_size(arr); i++) {
        struct capreq *cr = n_array_nth(arr, i);
        //printf("STORE %s %d -> %d\n", capreq_snprintf_s(cr),
        //     strlen(capreq_snprintf_s(cr)), capreq_sizeof(cr));
        capreq_store(cr, nbuf);
    }

    n_buf_add(nbuf, "\n", 1);
    size = n_buf_size(nbuf);
    n_assert(size < INT16_MAX);
    size16 = size;
    
    size16 = hton16(size16);
    
    if (prefix)
        fprintf(stream, "%s", prefix);
    
    fwrite(&size16, sizeof(size16), 1, stream);
    rc = fwrite(n_buf_ptr(nbuf), n_buf_size(nbuf), 1, stream);
    n_buf_free(nbuf);
    n_array_isort(arr);
    return rc;
}


tn_array *capreq_arr_restore(FILE *stream, int skip_bastards) 
{
    struct capreq  *cr;
    tn_array       *arr;
    tn_buf         *nbuf;
    tn_buf_it      nbufi;
    int16_t        size, arr_size;
    char           *buf;
    int i;

    if (fread(&size, sizeof(size), 1, stream) != 1)
        return NULL;
    
    size = ntoh16(size);
    n_assert(size);
    
    buf = alloca(size);
    
    if (fread(buf, size, 1, stream) != 1)
        return NULL;
    
    nbuf = n_buf_new(0);
    n_buf_init(nbuf, buf, size);
    n_buf_it_init(&nbufi, nbuf);

    n_buf_it_get_int16(&nbufi, &arr_size);

    arr = capreq_arr_new(arr_size);
    for (i=0; i<arr_size; i++) {
        if ((cr = capreq_restore(&nbufi))) {
//            printf("RESTORE %s %d -> %d\n", capreq_snprintf_s(cr),
            //             strlen(capreq_snprintf_s(cr)), capreq_sizeof(cr));
            
            if (skip_bastards && capreq_is_bastard(cr))
                continue;
            n_array_push(arr, cr);
        }
    }

    return arr;
}
