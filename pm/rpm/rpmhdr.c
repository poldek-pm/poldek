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

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include <rpm/rpmlib.h>
#ifdef HAVE_RPM_4_1
# include <rpm/rpmts.h>
#endif

#include <trurl/nassert.h>

#include "i18n.h"
#include "log.h"
#include "pm_rpm.h"

#ifdef HAVE_RPM_4_0_4           /* missing prototypes in public headers */
int headerGetRawEntry(Header h, int_32 tag,
                      /*@null@*/ /*@out@*/ hTYP_t type,
                      /*@null@*/ /*@out@*/ hPTR_t * p, 
                      /*@null@*/ /*@out@*/ hCNT_t c);
char ** headerGetLangs(Header h);
#endif

int pm_rpmhdr_get_raw_entry(Header h, int32_t tag, void *buf, int32_t *cnt)
{
    return headerGetRawEntry(h, tag, 0, (void*)buf, cnt);
}

int pm_rpmhdr_loadfdt(FD_t fdt, Header *hdr, const char *path)
{
    int rc = 0;
    
#ifndef HAVE_RPM_4_1
    rc = rpmReadPackageHeader(fdt, hdr, NULL, NULL, NULL);
#else 
    rpmRC rpmrc;
    rpmts ts = rpmtsCreate();
    rpmtsSetVSFlags(ts, _RPMVSF_NODIGESTS | _RPMVSF_NOSIGNATURES);
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
        logn(LOGERR, "open %s: %s", path, rpmErrorString());
        
    } else {
        rc = pm_rpmhdr_loadfdt(fdt, hdr, path);
        Fclose(fdt);
    }
    
    return rc;
}

char **pm_rpmhdr_langs(Header h)
{
    return (char**)headerGetLangs(h);
}


int pm_rpmhdr_nevr(void *h, char **name,
                   int32_t *epoch, char **version, char **release)
{
    int type;
    int32_t *anepoch;
    
    *epoch = 0;
    headerNVR(h, (void*)name, (void*)version, (void*)release);
    if (*name == NULL || *version == NULL || *release == NULL) 
        return 0;
    
    if (headerGetEntry(h, RPMTAG_EPOCH, &type, (void *)&anepoch, NULL))
        *epoch = *anepoch;
    
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
    if (!headerGetEntry(h, tag, &ent->type, &ent->val, &ent->cnt)) {
        memset(ent, 0, sizeof(*ent));
        return 0;
    }
    
    return 1;
}

void pm_rpmhdr_ent_free(struct rpmhdr_ent *ent)
{
    if (ent->type == RPM_STRING_ARRAY_TYPE ||
        ent->type == RPM_I18NSTRING_TYPE) {
        
        n_assert(ent->val);
        free(ent->val);
        memset(ent, 0, sizeof(*ent));
    }
}

int pm_rpmhdr_ent_cp(struct rpmhdr_ent *ent, Header h, int32_t tag, Header toh)
{
    int rc;
    
    if (!headerGetEntry(h, tag, &ent->type, &ent->val, &ent->cnt)) {
        memset(ent, 0, sizeof(*ent));
        return 0;
    }

    rc = headerAddEntry(toh, tag, ent->type, ent->val, ent->cnt);
    pm_rpmhdr_ent_free(ent);
    return rc;
}

int pm_rpmhdr_issource(Header h) {
    return headerIsEntry((h), RPMTAG_SOURCEPACKAGE);
}


void *pm_rpmhdr_link(void *h)
{
    return headerLink(h);
}

void pm_rpmhdr_free(void *h)
{
    headerFree(h);
}
