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
#include "rpmhdr.h"
#include "log.h"


int rpmhdr_loadfdt(FD_t fdt, Header *hdr, const char *path)
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
    
    if (rc != 0)
        logn(LOGERR, _("%s: read header failed"), path);
    
    return rc == 0;
}


int rpmhdr_loadfile(const char *path, Header *hdr)
{
    FD_t  fdt;
    int   rc = 0;
    
    
    if ((fdt = Fopen(path, "r")) == NULL) {
        logn(LOGERR, "open %s: %s", path, rpmErrorString());
        
    } else {
        rc = rpmhdr_loadfdt(fdt, hdr, path);
        Fclose(fdt);
    }
    
    return rc;
}

char **rpmhdr_langs(Header h)
{
    return headerGetLangs(h);
}


int rpmhdr_nevr(Header h, char **name,
                uint32_t **epoch, char **version, char **release)
{
    int type;


    headerNVR(h, (void*)name, (void*)version, (void*)release);
    if (*name == NULL || *version == NULL || *release == NULL) 
        return 0;
    
    if (!headerGetEntry(h, RPMTAG_EPOCH, &type, (void *)epoch, NULL))
        *epoch = NULL;
    
    return 1;
}

char *rpmhdr_snprintf(char *buf, size_t size, Header h) 
{
    char *name, *ver, *rel;
    uint32_t *epoch;

    if (rpmhdr_nevr(h, &name, &epoch, &ver, &rel))
        snprintf(buf, size, "%s-%s-%s", name, ver, rel);
    else
        snprintf(buf, size, "(bad hdr)");
    
    return buf;
}

void rpmhdr_free_entry(void *e, int type) 
{
    if (e && (type == RPM_STRING_ARRAY_TYPE || type == RPM_I18NSTRING_TYPE))
        free(e);
}


/* struct rpmhdr_ent stuff */

int rpmhdr_ent_get(struct rpmhdr_ent *ent, Header h, int32_t tag)
{
    if (!headerGetEntry(h, tag, &ent->type, &ent->val, &ent->cnt)) {
        memset(ent, 0, sizeof(*ent));
        return 0;
    }
    
    return 1;
}

void rpmhdr_ent_free(struct rpmhdr_ent *ent)
{
    if (ent->type == RPM_STRING_ARRAY_TYPE ||
        ent->type == RPM_I18NSTRING_TYPE) {
        
        n_assert(ent->val);
        free(ent->val);
        memset(ent, 0, sizeof(*ent));
    }
}

int rpmhdr_ent_cp(struct rpmhdr_ent *ent, Header h, int32_t tag, Header toh)
{
    int rc;
    
    if (!headerGetEntry(h, tag, &ent->type, &ent->val, &ent->cnt)) {
        memset(ent, 0, sizeof(*ent));
        return 0;
    }

    rc = headerAddEntry(toh, tag, ent->type, ent->val, ent->cnt);
    rpmhdr_ent_free(ent);
    return rc;
}

