/*
  Copyright (C) 2000 - 2018 Pawel A. Gajda <mis@pld-linux.org>

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

int pm_rpmhdr_get_string(Header h, int32_t tag, char *value, int size)
{
    const char *v = headerGetString(h, tag);
    if (!v)
        return 0;

    n_snprintf(value, size, "%s", v);
    return 1;
}

int pm_rpmhdr_get_int(Header h, int32_t tag, uint32_t *value)
{
    uint32_t v = headerGetNumber(h, tag);
    if (v == 0)
        return 0;

    *value = v;
    return 1;
}

int pm_rpmhdr_get_entry(Header h, int32_t tag, void *buf, int32_t *type,
                        int32_t *cnt)
{
    rpmtd td = rpmtdNew();

    *(char **)buf = NULL;

    if (!headerGet(h, tag, td, HEADERGET_MINMEM)) {
        *(char **)buf = NULL;
        if (cnt)
            *cnt = 0;
        return 0;
    }

    *type = td->type;
    if (cnt)
        *cnt = td->count;
    *(char ***)buf = td->data;

    /* TODO: check td->flags - mem allocation? */
    //rpmtdFree(td);

    return 1;
}

int pm_rpmhdr_get_raw_entry(Header h, int32_t tag, void *buf, int32_t *cnt)
{
    int type;
    rpmtd td = rpmtdNew();

    if (!headerGet(h, tag, td, HEADERGET_MINMEM | HEADERGET_RAW)) {
        *(char **)buf = NULL;
        if (cnt)
            *cnt = 0;
        return 0;
    }

    type = td->type;
    if (cnt)
        *cnt = td->count;
    *(char ***)buf = td->data;
    /* TODO: check td->flags - mem allocation? */
    //rpmtdFree(td);

    if (tag == RPMTAG_GROUP && type == RPM_STRING_TYPE) { // build by old rpm
        char **g;

        n_assert(*cnt == 1);

        g = n_malloc(sizeof(*g) * 2);
        g[0] = *(char **)buf;
        g[1] = NULL;
        *(char ***)buf = g;
    }

    DBGF("%d type=%d, cnt=%d\n", tag, type, *cnt);
    return 1;
}

int pm_rpmhdr_loadfdt(FD_t fdt, Header *hdr, const char *path)
{
    int rc = 0;
    rpmRC rpmrc;
    rpmts ts = rpmtsCreate();
    rpmtsSetVSFlags(ts,
                    RPMVSF_NOSHA1HEADER |
                    //RPMVSF_NOMD5HEADER |
                    //RPMVSF_NOSHA1 |
                    RPMVSF_NOMD5 |
                    RPMVSF_NODSAHEADER |
                    RPMVSF_NORSAHEADER |
                    RPMVSF_NORSA |
                    RPMVSF_NODSA);
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
    return rc == 0;
}

int pm_rpmhdr_loadfile(const char *path, Header *hdr)
{
    FD_t  fdt;
    int   rc = 0;

    if ((fdt = Fopen(path, "r")) == NULL) {
        logn(LOGERR, "open %s: error", path); /* XXX */
    } else {
        rc = pm_rpmhdr_loadfdt(fdt, hdr, path);
        Fclose(fdt);
    }

    return rc;
}

Header pm_rpmhdr_readfdt(void *fdt)
{
    Header h;

    h = headerRead(fdt, HEADER_MAGIC_YES);
    return h;
}

/*
 * pm_rpmhdr_langs:
 *
 * Returns NULL when no langs found.
 */
tn_array *pm_rpmhdr_langs(Header h)
{
    tn_array *langs = NULL;
    struct rpmtd_s td;
    const char *lang;

    if (!headerGet(h, RPMTAG_HEADERI18NTABLE, &td, HEADERGET_MINMEM | HEADERGET_RAW))
        return NULL;

    if (rpmtdCount(&td) == 0)
        return NULL;

    langs = n_array_new(rpmtdCount(&td), free, (tn_fn_cmp)strcmp);
    while ((lang = rpmtdNextString(&td)) != NULL) {
        n_array_push(langs, n_strdup(lang));
    }

    rpmtdFreeData(&td);
    return langs;
}

int pm_rpmhdr_nevr(void *h, const char **name, int32_t *epoch,
                   const char **version, const char **release,
                   const char **arch, uint32_t *color)
{
    *name = headerGetString(h, RPMTAG_NAME);
    *epoch = headerGetNumber(h, RPMTAG_EPOCH);
    *version = headerGetString(h, RPMTAG_VERSION);
    *release = headerGetString(h, RPMTAG_RELEASE);

    if (arch)
        *arch = headerGetString(h, RPMTAG_ARCH);

    if (*name == NULL || *version == NULL || *release == NULL)
        return 0;

    if (color)
        *color = headerGetNumber(h, RPMTAG_HEADERCOLOR);

    return 1;
}


void pm_rpmhdr_free_entry(void *e, int type)
{
    if (e && (type == RPM_STRING_ARRAY_TYPE || type == RPM_I18NSTRING_TYPE))
        free(e);
}

/* struct rpmhdr_ent stuff */
int pm_rpmhdr_ent_get(struct rpmhdr_ent *ent, Header h, int32_t tag)
{
    rpmtd td = rpmtdNew();
    if (!headerGet(h, tag, td, HEADERGET_MINMEM)) {
        memset(ent, 0, sizeof(*ent));
        return 0;
    }

    if (rpmtdCount(td) == 0) {
        memset(ent, 0, sizeof(*ent));
        return 0;
    }

    ent->tag = rpmtdTag(td);
    ent->type = rpmtdType(td);
    ent->val = td->data;
    ent->cnt = rpmtdCount(td);
    ent->td = td;

    return 1;
}

void pm_rpmhdr_ent_free(struct rpmhdr_ent *ent)
{
    if (ent->td)
        rpmtdFree(ent->td);
}

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
