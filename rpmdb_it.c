/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <rpm/rpmlib.h>
#ifdef HAVE_RPM_4_1
# include <rpm/rpmdb.h>
#endif

#define ENABLE_TRACE 0
#include "i18n.h"
#include "rpmdb_it.h"
#include "rpmadds.h"
#include "misc.h"
#include "log.h"

#ifdef HAVE_RPM_4_0

int rpmdb_it_init(rpmdb db, struct rpmdb_it *it, int tag, const char *arg)
{
    int rpmtag = 0;

    switch (tag) {
        case RPMITER_NAME:
            rpmtag = RPMTAG_NAME;
            break;
            
        case RPMITER_FILE:
            //rpmtag = RPMTAG_ORIGBASENAMES;
            rpmtag = RPMTAG_BASENAMES;
            break;

        case RPMITER_CAP:
            rpmtag = RPMTAG_PROVIDENAME;
            break;
            
        case RPMITER_REQ:
            rpmtag = RPMTAG_REQUIRENAME;
            break;
            
        case RPMITER_CNFL:
            rpmtag = RPMTAG_CONFLICTNAME;
            break;
            
        case RPMITER_OBSL:
            rpmtag = RPMTAG_OBSOLETENAME;
            break;
            
        default:
            die();
    }
    

    DBGF("%p, %p (%d)\n", it, db, db->nrefs);
    it->db = db;
    it->mi = rpmdbInitIterator(db, rpmtag, arg, 0);
    return rpmdbGetIteratorCount(it->mi);
}

#else  /* HAVE_RPM_4_0 */

int rpmdb_it_init(rpmdb db, struct rpmdb_it *it, int tag, const char *arg) 
{
    int rc, n;
    
    it->matches.count = 0;
    it->matches.recs = NULL;
    it->i = 0;
    it->db = db;
    it->dbrec.h = NULL;
    it->dbrec.recno = 0;
    
    switch (tag) {
        case RPMITER_NAME:
            rc = rpmdbFindPackage(db, arg, &it->matches);
            break;
            
        case RPMITER_FILE:
            rc = rpmdbFindByFile(db, arg, &it->matches);
            break;

        case RPMITER_CAP:
            rc = rpmdbFindByProvides(db, arg, &it->matches);
            break;
            
        case RPMITER_REQ:
            rc = rpmdbFindByRequiredBy(db, arg, &it->matches);
            break;
            
        case RPMITER_CNFL:
            rc = rpmdbFindByConflicts(db, arg, &it->matches);
            break;
            
        case RPMITER_OBSL:
            die();
            rc = rpmdbFindByConflicts(db, arg, &it->matches);
            break;
            
        default:
            die();
    }
    
    if (rc < 0)
        die();
    
    else if (rc != 0) {
        n = 0;
        it->matches.count = 0;
        
    } else if (rc == 0)
        n = it->matches.count;

    return n;
}
#endif /* HAVE_RPM_4_0 */



void rpmdb_it_destroy(struct rpmdb_it *it) 
{
    

#ifdef HAVE_RPM_4_0
    rpmdbFreeIterator(it->mi);
    it->mi = NULL;
    it->dbrec.h = NULL;
    DBGF("%p, %p (%d)\n", it, it->db, it->db->nrefs);
#else
    if (it->dbrec.h != NULL) {
        headerFree(it->dbrec.h);
        it->dbrec.h = NULL;
    }
    
    it->db = NULL;
    dbiFreeIndexRecord(it->matches);
    it->matches.count = 0;
    it->matches.recs = NULL;
#endif    
}



const struct dbrec *rpmdb_it_get(struct rpmdb_it *it)
{
#ifdef HAVE_RPM_4_0
    it->dbrec.h = rpmdbNextIterator(it->mi);
    DBGF("%p, %p (%d)\n", it, it->db, it->db->nrefs);
    
    if (it->dbrec.h == NULL)
        return NULL;

    it->dbrec.recno = rpmdbGetIteratorOffset(it->mi);
#else
    if (it->i == it->matches.count) {
        if (it->dbrec.h != NULL) 
            headerFree(it->dbrec.h);
        it->dbrec.h = NULL;
        it->i++;
        return NULL;
    }

    if (it->i > it->matches.count)
        die();

    if (it->dbrec.h != NULL)
        headerFree(it->dbrec.h);
    
    it->dbrec.recno = it->matches.recs[it->i].recOffset;
    it->dbrec.h = rpmdbGetRecord(it->db, it->dbrec.recno);
    it->i++;
    
    if (it->dbrec.h == NULL)
        die();
#endif /* HAVE_RPM_4_0 */

    return &it->dbrec;
}


int rpmdb_it_get_count(struct rpmdb_it *it)
{
#ifdef HAVE_RPM_4_0
    return rpmdbGetIteratorCount(it->mi);
#else
    return it->matches.count;
#endif /* HAVE_RPM_4_0 */
}


void dbrec_clean(struct dbrec *dbrec) 
{
    if (dbrec->h)
        headerFree(dbrec->h);
}


char *dbrec_snprintf(char *buf, size_t size, const struct dbrec *dbrec)
{
    if (dbrec->h)
        rpmhdr_snprintf(buf, size, dbrec->h);
    else 
        snprintf(buf, size, "(null dbrec->h)");
    return buf;
}


char *dbrec_snprintf_s(const struct dbrec *dbrec)
{
    static char buf[256];
    return dbrec_snprintf(buf, sizeof(buf), dbrec);
}
