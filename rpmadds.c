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
#include "misc.h"
#include "log.h"


int rpm_headerReadFD(FD_t fdt, Header *hdr, const char *path)
{
    int rc;
#ifdef HAVE_RPM_4_1
    rpmts ts = rpmtsCreate();

    rc = rpmReadPackageFile(ts, fdt, path, hdr);
#else
    rc = rpmReadPackageHeader(fdt, hdr, NULL, NULL, NULL);
#endif

    if (rc != 0)
        logn(LOGERR, _("%s: read header failed"), path);
    
#ifdef HAVE_RPM_4_1
    rpmtsFree(ts);
#endif

    return rc == 0;
}


int rpm_headerReadFile(const char *path, Header *hdr)
{
    FD_t  fdt;
    int   rc = 1;
    
    
    if ((fdt = Fopen(path, "r")) == NULL) {
        logn(LOGERR, "open %s: %s", path, rpmErrorString());
        
    } else {
        rc = rpm_headerReadFD(fdt, hdr, path);
        Fclose(fdt);
    }
    
    return rc == 0;
}


void rpm_headerEntryFree(void *e, int type) 
{
    if (e && (type == RPM_STRING_ARRAY_TYPE || type == RPM_I18NSTRING_TYPE))
        free(e);
}


int rpm_headerRSATSize(void *e, int count, int type) 
{
    char **s;
    int len = 0;
    
    n_assert(type == RPM_STRING_ARRAY_TYPE);
    
    s = (char **) e;
    while (count--) 
        len += strlen(*s++) + 1;
    
    return len;
}


int parse_evr(char *evrstr,
              int32_t *epoch, const char **version, const char **release)
{
    char *p;

    while (isspace(*evrstr))
        evrstr++;
    
    if (*evrstr == '\0')
        return 0;
    
    if ((p = strchr(evrstr, ':')) == NULL) 
        *epoch = 0;
    else {
        *p = '\0';
        *epoch = (int32_t)strtol(evrstr, (char **)NULL, 10);
        evrstr = p+1;
    }

    if ((p = strchr(evrstr, '-')) == NULL) {
        *version = evrstr;
        *release = NULL;
        
    } else {
        *p = '\0';
        *version = evrstr;
        *release = p+1;

        if (**version == '\0' || **release == '\0')
            return 0;
    }
    
    return 1;
}

int parse_nevr(char *nevrstr, const char **name,
               int32_t *epoch, const char **version, const char **release)
{
    char *p, *q;

    while (isspace(*nevrstr))
        nevrstr++;
    
    if (*nevrstr == '\0')
        return 0;
    
    if ((p = strrchr(nevrstr, '-')) == NULL) 
        return 0;
    
    q = p;
    *q = '\0';
    
    if ((p = strrchr(nevrstr, '-')) == NULL) 
        return 0;
    
    *q = '-';
    *p = '\0';
    p++;
    *name = nevrstr;
    return parse_evr(p, epoch, version, release);
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
