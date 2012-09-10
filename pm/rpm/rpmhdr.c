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

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include <trurl/nassert.h>

#include "i18n.h"
#include "log.h"
#include "pm_rpm.h"

#if defined(HAVE_RPM_4_0_4) || defined(HAVE_RPM_VERSION_GE_4_4_8)           /* missing prototypes in public headers */
#ifndef HAVE_RPM_5
int headerGetRawEntry(Header h, int_32 tag,
                      /*@null@*/ /*@out@*/ hTYP_t type,
                      /*@null@*/ /*@out@*/ hPTR_t * p, 
                      /*@null@*/ /*@out@*/ hCNT_t c);
char ** headerGetLangs(Header h);
#endif
#endif


int pm_rpmhdr_get_string(Header h, int32_t tag, char *value, int size)
{
    char *v;
    int type, n;
    
    if (!pm_rpmhdr_get_entry(h, tag, &v, &type, NULL))
        return 0;

    n_assert(type == RPM_STRING_TYPE);
    n = n_snprintf(value, size, "%s", v);
    pm_rpmhdr_free_entry(v, type);
    return 1;
}

int pm_rpmhdr_get_int(Header h, int32_t tag, uint32_t *value)
{
    uint32_t *v;
    int type;
    
    if (!pm_rpmhdr_get_entry(h, tag, &v, &type, NULL))
        return 0;
    
    n_assert(type == RPM_INT32_TYPE);
    *value = *v;
    pm_rpmhdr_free_entry(v, type);
    
    return 1;
}

int pm_rpmhdr_get_entry(Header h, int32_t tag, void *buf, int32_t *type,
                        int32_t *cnt)
{
    *(char **)buf = NULL;
    
#if HAVE_RPMPKGREAD             /* rpm5 >= 5.0 */
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));

    he->tag = tag;
    if (!headerGet(h, he, 0)) {
        *(char **)buf = NULL;
        if (cnt)
            *cnt = 0;
        return 0;
    }
    
    *type = he->t;
    if (cnt)
        *cnt = he->c;
    *(char ***)buf = he->p.ptr;
    
#else
    if (!headerGetEntry(h, tag, type, buf, cnt)) {
        *(char **)buf = NULL;
        if (cnt)
            *cnt = 0;
        return 0;
    }
#endif

    return 1;
}

int pm_rpmhdr_get_raw_entry(Header h, int32_t tag, void *buf, int32_t *cnt)
{
    int type;
    
#if HAVE_RPMPKGREAD             /* rpm5 >= 5.0 */
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));

    he->tag = tag;
    if (!headerGet(h, he, HEADERGET_NOI18NSTRING|HEADERGET_NOEXTENSION)) {
        buf = NULL;
        *cnt = 0;
        return 0;
    }
    type = he->t;
    *cnt = he->c;
    *(char ***)buf = he->p.ptr;
#else
    if (!headerGetRawEntry(h, tag, &type, (void*)buf, cnt)) {
        buf = NULL;
        *cnt = 0;
        return 0;
    }
#endif

#ifndef HAVE_RPM_5
    if (tag == RPMTAG_GROUP && type == RPM_STRING_TYPE) { // build by old rpm
        char **g;
	
        n_assert(*cnt == 1);

        g = n_malloc(sizeof(*g) * 2);
        g[0] = *(char **)buf;
        g[1] = NULL;
        *(char ***)buf = g;
    }
#endif

    DBGF("%d type=%d, cnt=%d\n", tag, type, *cnt);
    return 1;
}

int pm_rpmhdr_loadfdt(FD_t fdt, Header *hdr, const char *path)
{
    int rc = 0;
    
#ifndef HAVE_RPM_4_1
    rc = rpmReadPackageHeader(fdt, hdr, NULL, NULL, NULL);
#else 
    rpmRC rpmrc;
    rpmts ts = rpmtsCreate();
    rpmtsSetVSFlags(ts, RPMVSF_NOSHA1HEADER | RPMVSF_NOMD5HEADER |
                        RPMVSF_NOSHA1 | RPMVSF_NOMD5 |
                        RPMVSF_NODSAHEADER | RPMVSF_NORSAHEADER |
                        RPMVSF_NODSA | RPMVSF_NODSA);
    rpmrc = rpmReadPackageFile(ts, fdt, path, hdr);
    switch (rpmrc) {
        case RPMRC_NOTTRUSTED:
        case RPMRC_NOKEY:
        case RPMRC_OK:
            rc = 0;
            break;
            
        default:
            rc = 1;
    }
    rpmtsFree(ts);
#endif
    return rc == 0;
}


int pm_rpmhdr_loadfile(const char *path, Header *hdr)
{
    FD_t  fdt;
    int   rc = 0;
    
    if ((fdt = Fopen(path, "r")) == NULL) {
#ifdef HAVE_RPMERRORSTRING        
        logn(LOGERR, "open %s: %s", path, rpmErrorString());
#else        
        logn(LOGERR, "open %s: error", path); /* XXX */
#endif        
    } else {
        rc = pm_rpmhdr_loadfdt(fdt, hdr, path);
        Fclose(fdt);
    }
    
    return rc;
}

Header pm_rpmhdr_readfdt(void *fdt) 
{
    Header h;
    
#if HAVE_RPMPKGREAD             /* rpm >= 5 */
    if (rpmpkgRead("Header", fdt, &h, NULL) != RPMRC_OK)
        h = NULL;

#else  /* rpm < 5 */

# if HAVE_RPM_HEADER_MAGIC_YES 
    h = headerRead(fdt, HEADER_MAGIC_YES);
# else
    h = headerRead(fdt);
# endif

#endif
    return h;
}

/*
 * pm_rpmhdr_langs:
 *
 * Returns NULL when no langs found.
 */
tn_array *pm_rpmhdr_langs(Header h)
{
    tn_array *alangs = NULL;
    char **langs;
    int t, n = 0, i;

#ifndef HAVE_RPMPKGREAD         /* rpm < 5 */
    if ((langs = headerGetLangs(h)) == NULL)
	return NULL;

    while (langs[n])
        n++;
    t = t;
    
#else
    pm_rpmhdr_get_entry(h, RPMTAG_HEADERI18NTABLE, &langs, &t, &n);
#endif

    /* do it when langs are available */
    if (n > 0) {
	alangs = n_array_new(n, free, (tn_fn_cmp)strcmp);
	for (i=0; i < n ; i++)
    	    n_array_push(alangs, n_strdup(langs[i]));
    }
    
    free(langs);
    return alangs;
}


int pm_rpmhdr_nevr(void *h, const char **name, int32_t *epoch, const char **version,
                   const char **release, const char **arch, int *color)
{
    int32_t anepoch;
    
    *epoch = 0;
    
#if HAVE_HEADERNVR    
    headerNVR(h, (void*)name, (void*)version, (void*)release);
#elif HAVE_HEADERNEVRA          /* rpm >= 5 */
    headerNEVRA(h, name, NULL, version, release, arch ? arch : NULL);
#endif    

    if (*name == NULL || *version == NULL || *release == NULL) 
        return 0;
    
    if (pm_rpmhdr_get_int(h, RPMTAG_EPOCH, &anepoch))
        *epoch = anepoch;
    
#ifndef HAVE_RPMPKGREAD        /* rpm < 5 */
    if (arch) {
        int32_t type;
        
        *arch = NULL;
        headerGetEntry(h, RPMTAG_ARCH, &type, (void *)arch, NULL);
    }
#endif
    
    if (color) {
        *color = 0;
#ifdef HAVE_RPM_HGETCOLOR
        *color = hGetColor(h);
#endif
    }
    
    return 1;
}


void pm_rpmhdr_free_entry(void *e, int type) 
{
#if HAVE_RPMPKGREAD
    if (e)
        free(e);
#elif HAVE_HEADERFREEDATA
    if (e)
        headerFreeData(e, type);
#else
    if (e && (type == RPM_STRING_ARRAY_TYPE || type == RPM_I18NSTRING_TYPE))
        free(e);
#endif
}


/* struct rpmhdr_ent stuff */

int pm_rpmhdr_ent_get(struct rpmhdr_ent *ent, Header h, int32_t tag)
{
#if HAVE_RPMPKGREAD             /* rpm5 >= 5.0 */
    HE_t he = memset(alloca(sizeof(*he)), 0, sizeof(*he));

    he->tag = tag;
    if (!headerGet(h, he, 0)) {
        memset(ent, 0, sizeof(*ent));
        return 0;
    }
    
    ent->type = he->t;
    ent->cnt = he->c;
    ent->val = he->p.ptr;

#else  /* rpm < 5.0  */
    if (!headerGetEntry(h, tag, &ent->type, &ent->val, &ent->cnt)) {
        memset(ent, 0, sizeof(*ent));
        return 0;
    }
#endif    
    return 1;
}

void pm_rpmhdr_ent_free(struct rpmhdr_ent *ent)
{
#if HAVE_RPMPKGREAD             /* rpm5 >= 5.0 */
    free(ent->val);
#else
    if (ent->type == RPM_STRING_ARRAY_TYPE ||
        ent->type == RPM_I18NSTRING_TYPE) {
        
        n_assert(ent->val);
        free(ent->val);
        memset(ent, 0, sizeof(*ent));
    }
#endif    
}

#if 0
int pm_rpmhdr_ent_cp(struct rpmhdr_ent *ent, Header h, int32_t tag, Header toh)
{
    struct rpmhdr_ent e;
    int rc;
    
    //if (!pm_rpmhdr_ent_get(&e, h, tag));
    
        
    if (!headerGetEntry(h, tag, &ent->type, &ent->val, &ent->cnt)) {
        memset(ent, 0, sizeof(*ent));
        return 0;
    }

    rc = headerAddEntry(toh, tag, ent->type, ent->val, ent->cnt);
    pm_rpmhdr_ent_free(ent);
    return rc;
}
#endif

int pm_rpmhdr_issource(Header h)
{
    return !headerIsEntry(h, RPMTAG_SOURCERPM);
}

void *pm_rpmhdr_link(void *h)
{
    return headerLink(h);
}

void pm_rpmhdr_free(void *h)
{
    headerFree(h);
}
