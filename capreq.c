/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>

#include <rpm/rpmlib.h>
#include <trurl/narray.h>
#include <trurl/nassert.h>

#include "rpmadds.h"
#include "capreq.h"
#include "log.h"
#include "misc.h"
#include "h2n.h"

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


int capreq_cmp_name(struct capreq *cr1, struct capreq *cr2) 
{
    return strcmp(capreq_name(cr1), capreq_name(cr2));
}

int capreq_cmp2name(struct capreq *cr1, const char *name)
{
    return strcmp(capreq_name(cr1), name);
}


int capreq_cmp_name_evr(struct capreq *cr1, struct capreq *cr2) 
{
    int rc;

    if ((rc = strcmp(capreq_name(cr1), capreq_name(cr2))))
        return rc;
    
    if ((rc = capreq_epoch(cr1) - capreq_epoch(cr2)))
        return rc;
        
    if ((rc = strcmp(capreq_ver(cr1), capreq_ver(cr2))))
        return rc;

    if ((rc = strcmp(capreq_rel(cr1), capreq_rel(cr2))))
        return rc;

    rc = cr1->cr_flags - cr2->cr_flags;

    return rc;
}


int capreq_fprintf(FILE *stream, const struct capreq *cr) 
{
    char relstr[64], *p;
    
    p = relstr;
    *p = '\0';
    
    if (cr->cr_flags & REL_LT) 
        *p++ = '<';
    else if (cr->cr_flags & REL_GT) 
        *p++ = '>';

    if (cr->cr_flags & REL_EQ) 
        *p++ = '=';

    *p = '\0';

    fprintf(stream, "%s%s%s", 
            capreq_is_bastard(cr) ? "!" : "",
            capreq_is_prereq(cr) ? "*" : "",
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


char *capreq_snprintf(char *str, size_t size, const struct capreq *cr) 
{
    int nwritten;
    char relstr[64], *p, *s;

    s = str;
    p = relstr;
    *p = '\0';
    
    if (cr->cr_flags & REL_LT) 
        *p++ = '<';
    else if (cr->cr_flags & REL_GT) 
        *p++ = '>';

    if (cr->cr_flags & REL_EQ) 
        *p++ = '=';

    *p = '\0';

    if (capreq_is_bastard(cr)) {
        *s++ = '!';
        size--;
    }
    
    if (capreq_is_prereq(cr)) {
        *s++ = '*';
        size--;
    }
    
    if (p == relstr) {
        n_assert(*capreq_ver(cr) == '\0');
        if (capreq_is_rpmlib(cr))
            nwritten = snprintf(s, size, "rpmlib(%s)", capreq_name(cr));
        else
            nwritten = snprintf(s, size, "%s", capreq_name(cr));
        
        size -= nwritten;
        s += nwritten;
        
    } else {
        n_assert(*capreq_ver(cr));
        if (capreq_is_rpmlib(cr))
            nwritten = snprintf(s, size, "rpmlib(%s) %s ", capreq_name(cr), relstr);
        else
            nwritten = snprintf(s, size, "%s %s ", capreq_name(cr), relstr);
        s += nwritten;
        size -= nwritten;
        
        if (capreq_has_epoch(cr)) {
            nwritten = snprintf(s, size, "%d:", capreq_epoch(cr));
            s += nwritten;
            size -= nwritten;
        }

        if (capreq_has_ver(cr)) {
            nwritten = snprintf(s, size, "%s", capreq_ver(cr));
            s += nwritten;
            size -= nwritten;
        }

        if (capreq_has_rel(cr)) {
            n_assert(capreq_has_ver(cr));
            snprintf(s, size, "-%s", capreq_rel(cr));
        }
    }
    
    return str;
}


int16_t capreq_sizeof(const struct capreq *cr) 
{
    size_t size = sizeof(*cr);
    int max_ofs = 0;
    
    if (cr->cr_ep_ofs > max_ofs)
        max_ofs = cr->cr_ep_ofs;

    if (cr->cr_ver_ofs > max_ofs)
        max_ofs = cr->cr_ver_ofs;
    
    if (cr->cr_rel_ofs > max_ofs)
        max_ofs = cr->cr_rel_ofs;
    
    n_assert(max_ofs);
    size += max_ofs + strlen(&cr->_buf[max_ofs]) + 1;
    n_assert (size < INT16_MAX);
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
                          int32_t flags) 
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
            log(LOGERR, "%s: invalid rpmlib capreq\n", name);
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

    cr->cr_ep_ofs = cr->cr_ver_ofs = cr->cr_rel_ofs = cr->cr_flags = 0;
        
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

    cr->cr_flags = flags;
    if (isrpmreq)
        cr->cr_flags |= CAPREQ_RPMLIB;
    return cr;
}


struct capreq *capreq_new_evr(const char *name, char *evr, int32_t flags)
{
    char *version = NULL, *release = NULL;
    int32_t epoch = 0;

    if (evr && !parse_evr(evr, &epoch, &version, &release))
        return NULL;
    
    return capreq_new(name, epoch, version, release, flags);
}


tn_array *capreq_arr_new(void) 
{
    tn_array *arr;
    arr = n_array_new(2, capreq_free_fn, (tn_fn_cmp)capreq_cmp_name);
    n_array_ctl(arr, TN_ARRAY_AUTOSORTED);
    return arr;
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
            log(LOGERR, "read%ss: nnames %d != nversions %d, broken rpm\n",
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
        log(LOGERR, "read%ss: nversions %d != nflags %d, broken rpm\n", label,
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
                log(LOGERR, "%s: invalid rpmlib capreq\n", name);
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
            register int flag = flags[i];

            if (flag & RPMSENSE_LESS) 
                cr->cr_flags |= REL_LT;
            
            if (flag & RPMSENSE_GREATER) 
                cr->cr_flags |= REL_GT;
            
            if (flag & RPMSENSE_EQUAL) 
                cr->cr_flags |= REL_EQ;
            
            if (flag & RPMSENSE_PREREQ) {
//                printf("prtype = %d\n", prtype);
                n_assert(crtype == CRTYPE_REQ);
                cr->cr_flags |= CAPREQ_PREREQ;
            }
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
    
    cr->cr_flags |= REL_EQ;

    n_array_push(arr, cr);
    return arr;
}


void capreq_store(struct capreq *cr, tn_buf *nbuf) 
{
    int32_t epoch, nepoch;
    int16_t size = hton16(capreq_sizeof(cr));
    

    n_buf_add(nbuf, &size, sizeof(size));
    if (cr->cr_ep_ofs) {
        epoch = capreq_epoch(cr);
        nepoch = hton32(epoch);
        memcpy(&cr->_buf[cr->cr_ep_ofs], &nepoch, sizeof(nepoch));
    }

    n_buf_add(nbuf, cr, size);
    memcpy(&cr->_buf[cr->cr_ep_ofs], &epoch, sizeof(epoch));
}


struct capreq *capreq_restore(tn_buf_it *nbufi) 
{
    struct capreq *cr;
    int16_t size;
    char *p;
    
    p = n_buf_it_get(nbufi, sizeof(size));
    if (p == NULL)
        return NULL;
    
    size = ntoh16(*(int16_t*)p);
    p = n_buf_it_get(nbufi, size);
    if (p == NULL)
        return NULL;
    
    if ((cr = capreq_alloc_fn(size)) == NULL)
        return NULL;
    
    memcpy(cr, p, size);

    if (cr->cr_ep_ofs) {
        int32_t epoch = ntoh32(capreq_epoch(cr));
        memcpy(&cr->_buf[cr->cr_ep_ofs], &epoch, sizeof(epoch));
    }
    
    return cr;
}
