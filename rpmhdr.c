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

#include "rpmhdr.h"
#include "log.h"

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
    if (ent->type == RPM_STRING_ARRAY_TYPE
        || ent->type == RPM_I18NSTRING_TYPE) {
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


char *rpmhdr_strnvr(char *buf, int size, Header h)
{
    char *n, *v, *r;
    
    headerNVR(h, (void*)&n, (void*)&v, (void*)&r);
    if (n == NULL || v == NULL || r == NULL) {
        log(LOGERR, "headerNVR failed\n");
        return NULL;
    }
    
    snprintf(buf, size, "%s-%s-%s", n, v, r);
    return buf;
}



int rpmhdr_fl_ld(struct rpmhdr_fl *hdrfl, Header h, const char *pkgname)
{
    int t1, t2, t3, t4, c1, c2, c3, c4;
    char **bnames = NULL, **dnames = NULL, **symlinks = NULL;
    int32_t   *diridxs;
    uint32_t  *sizes;
    uint16_t  *modes;
    int       i, j, nerr = 0;

    hdrfl->bnames = NULL;
    hdrfl->dnames = NULL;
    hdrfl->symlinks = NULL;
    hdrfl->diridxs = NULL;
    hdrfl->sizes = NULL;
    hdrfl->modes = NULL;
    hdrfl->h = NULL;

    if (pkgname == NULL)
        pkgname = "unknown";
    
    if (!headerGetEntry(h, RPMTAG_BASENAMES, (void*)&t1, (void*)&bnames, &c1))
        return 0;

    n_assert(t1 == RPM_STRING_ARRAY_TYPE);
    if (!headerGetEntry(h, RPMTAG_DIRNAMES, (void*)&t2, (void*)&dnames, &c2))
        goto l_err_end;
    
    n_assert(t2 == RPM_STRING_ARRAY_TYPE);
    if (!headerGetEntry(h, RPMTAG_DIRINDEXES, (void*)&t3,(void*)&diridxs, &c3))
    {
        log(LOGERR, "%s: no DIRINDEXES tag\n", pkgname);
        nerr++;
        goto l_err_end;
    }

    n_assert(t3 == RPM_INT32_TYPE);
    
    if (c1 != c3) {
        log(LOGERR, "%s: DIRINDEXES (%d) != BASENAMES (%d) tag\n", c3, c1,
            pkgname);
        nerr++;
        goto l_err_end;
    }
    
    if (!headerGetEntry(h, RPMTAG_FILEMODES, (void*)&t4, (void*)&modes, &c4)) {
        log_msg("%s: no FILEMODES tag\n", pkgname);
        nerr++;
        goto l_err_end;
    }

    if (c1 != c4) {
        log(LOGERR, "%s: FILEMODES (%d) != BASENAMES (%d) tag\n", c4, c1,
            pkgname);
        nerr++;
        goto l_err_end;
    }
    
    if (!headerGetEntry(h, RPMTAG_FILESIZES, (void*)&t4, (void*)&sizes, &c4)) {
        log_msg("%s: no FILESIZES tag\n", pkgname);
        nerr++;
        goto l_err_end;
    }

    if (c1 != c4) {
        log(LOGERR, "%s: FILESIZES (%d) != BASENAMES (%d) tag\n", c4, c1,
            pkgname);
        nerr++;
        goto l_err_end;
    }
    
    if (!headerGetEntry(h, RPMTAG_FILELINKTOS, (void*)&t4, (void*)&symlinks,
                        &c4)) {
        symlinks = NULL;
    }
    
    hdrfl->bnames = bnames;
    hdrfl->nbnames = c1;
    
    hdrfl->dnames = dnames;
    hdrfl->ndnames = c2;

    hdrfl->symlinks = symlinks;
    if (symlinks)
        hdrfl->nsymlinks = c1;
    
    hdrfl->diridxs = diridxs;
    hdrfl->ndiridxs = c1;
    
    hdrfl->sizes = sizes;
    hdrfl->nsizes = c1;
    
    hdrfl->modes = modes;
    hdrfl->nmodes = c1;
    hdrfl->h = headerLink(h);

    return 1;

l_err_end:
    
    if (c1 && bnames)
        free(bnames);

    if (c2 && dnames)
        free(dnames);

    if (symlinks)
        free(symlinks);

    return nerr ? 0 : 1;
}


void rpmhdr_fl_free(struct rpmhdr_fl *hdrfl) 
{
    
    if (hdrfl->bnames)
        free(hdrfl->bnames);
    
    if (hdrfl->dnames)
        free(hdrfl->dnames);
    
    if (hdrfl->symlinks)
        free(hdrfl->symlinks);

    if (hdrfl->h)
        headerFree(hdrfl->h);

    hdrfl->bnames = NULL;
    hdrfl->dnames = NULL;
    hdrfl->symlinks = NULL;
    hdrfl->diridxs = NULL;
    hdrfl->sizes = NULL;
    hdrfl->modes = NULL;
    hdrfl->h = NULL;
}

