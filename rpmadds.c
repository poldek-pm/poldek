/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include <rpm/rpmlib.h>
#include <trurl/nassert.h>

#include "misc.h"
#include "log.h"

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
    char *p;

    while (isspace(*nevrstr))
        nevrstr++;
    
    if (*nevrstr == '\0')
        return 0;
    
    if ((p = strrchr(nevrstr, '-')) == NULL) 
        return 0;
    
    p--;
    if ((p = strrchr(p, '-')) == NULL) 
        return 0;

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
