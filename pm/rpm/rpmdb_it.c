/*
  Copyright (C) 2000 - 2004 Pawel A. Gajda <mis@k2.net.pl>

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

#include <rpm/rpmlib.h>
#ifdef HAVE_RPM_4_1
# include <rpm/rpmdb.h>
#endif

#define ENABLE_TRACE 0
#include "i18n.h"
#include "misc.h"
#include "log.h"

#include "pm_rpm.h"
#include "pm/pm.h"
#include "pm/mod.h"

/* remeber! don't touch any member */
struct rpmdb_it {
    int                  tag;
#ifdef HAVE_RPM_4_0
    rpmdbMatchIterator   mi;
#else
    dbiIndexSet          matches;
    int                  i;
    int                  recno;
#endif
    
    struct pm_dbrec      dbrec;
    rpmdb                db; 
};



#ifdef HAVE_RPM_4_0
static
int rpmdb_it_init(rpmdb db, struct rpmdb_it *it, int tag, const char *arg)
{
    int rpmtag = 0, argsize = 0;
    
    switch (tag) {
        case PMTAG_RECNO:
            rpmtag = RPMDBI_PACKAGES;
            if (arg)
                argsize = sizeof(int);
            break;
            
        case PMTAG_NAME:
            rpmtag = RPMTAG_NAME;
            break;
            
        case PMTAG_FILE:
            //rpmtag = RPMTAG_ORIGBASENAMES;
            rpmtag = RPMTAG_BASENAMES;
            break;

        case PMTAG_CAP:
            rpmtag = RPMTAG_PROVIDENAME;
            break;
            
        case PMTAG_REQ:
            rpmtag = RPMTAG_REQUIRENAME;
            break;
            
        case PMTAG_CNFL:
            rpmtag = RPMTAG_CONFLICTNAME;
            break;
            
        case PMTAG_OBSL:
            rpmtag = RPMTAG_OBSOLETENAME;
            break;
            
        default:
            die();
    }
    
    
    DBGF("%p, %p (%d)\n", it, db, db->nrefs);
    it->tag = tag;
    it->db = db;
    it->mi = rpmdbInitIterator(db, rpmtag, arg, argsize);
    return rpmdbGetIteratorCount(it->mi);
}

#else  /* HAVE_RPM_4_0 */

static
int rpmdb_it_init(rpmdb db, struct rpmdb_it *it, int tag, const char *arg) 
{
    int rc, n;
    
    it->matches.count = 0;
    it->matches.recs = NULL;
    it->i = 0;
    it->recno = 0;
    it->db = db;
    it->dbrec.hdr = NULL;
    it->dbrec.recno = 0;
    it->tag = tag;
    
    switch (tag) {
        case PMTAG_RECNO:
            it->recno = rpmdbFirstRecNum(db);
            if (recno == 0)
                return 0;
            if (recno < 0)
                die();
            break;
            
        case PMTAG_NAME:
            rc = rpmdbFindPackage(db, arg, &it->matches);
            break;
            
        case PMTAG_FILE:
            rc = rpmdbFindByFile(db, arg, &it->matches);
            break;

        case PMTAG_CAP:
            rc = rpmdbFindByProvides(db, arg, &it->matches);
            break;
            
        case PMTAG_REQ:
            rc = rpmdbFindByRequiredBy(db, arg, &it->matches);
            break;
            
        case PMTAG_CNFL:
            rc = rpmdbFindByConflicts(db, arg, &it->matches);
            break;
            
        case PMTAG_OBSL:
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


static
void rpmdb_it_destroy(struct rpmdb_it *it) 
{

#ifdef HAVE_RPM_4_0
    rpmdbFreeIterator(it->mi);
    it->mi = NULL;
    it->dbrec.hdr = NULL;
    DBGF("%p, %p (%d)\n", it, it->db, it->db->nrefs);
#else
    if (it->dbrec.hdr != NULL) {
        headerFree(it->dbrec.hdr);
        it->dbrec.hdr = NULL;
    }
    
    it->db = NULL;
    if (it->tag != PMTAG_RECNO)
        dbiFreeIndexRecord(it->matches);
    it->matches.count = 0;
    it->matches.recs = NULL;
#endif    
}


static
const struct pm_dbrec *rpmdb_it_get(struct rpmdb_it *it)
{
#ifdef HAVE_RPM_4_0
    it->dbrec.hdr = rpmdbNextIterator(it->mi);
    DBGF("%p, %p (%d)\n", it, it->db, it->db->nrefs);
    
    if (it->dbrec.hdr == NULL)
        return NULL;

    it->dbrec.recno = rpmdbGetIteratorOffset(it->mi);
#else
    
    if (it->tag == PMTAG_RECNO) {
        if (it->recno <= 0)
            return NULL;
        
        n_assert(it->recno);
        it->dbrec.recno = it->recno;
        it->dbrec.hdr = rpmdbGetRecord(it->db, it->recno);
        it->recno = rpmdbNextRecNum(db, it->recno);
        it->i++;
        return &it->dbrec;
    }

    if (it->i == it->matches.count) {
        if (it->dbrec.hdr != NULL) 
            headerFree(it->dbrec.hdr);
        it->dbrec.hdr = NULL;
        it->i++;
        return NULL;
    }

    if (it->i > it->matches.count)
        die();

    if (it->dbrec.hdr != NULL)
        headerFree(it->dbrec.hdr);
    
    it->dbrec.recno = it->matches.recs[it->i].recOffset;
    it->dbrec.hdr = rpmdbGetRecord(it->db, it->dbrec.recno);
    it->i++;
    
    if (it->dbrec.hdr == NULL)
        die();
#endif /* HAVE_RPM_4_0 */

    return &it->dbrec;
}

static
int rpmdb_it_get_count(struct rpmdb_it *it)
{
#ifdef HAVE_RPM_4_0
    return rpmdbGetIteratorCount(it->mi);
#else
    if (it->tag == PMTAG_RECNO)
        return it->recno > 0 ? 1000:0; /* TODO howto do dbcount() with rpm3 */
        
    return it->matches.count;
#endif /* HAVE_RPM_4_0 */
}

static 
int pm_rpm_db_it_get_count(struct pkgdb_it *it) 
{
    return rpmdb_it_get_count(it->_it);
}

static 
const struct pm_dbrec *pm_rpm_db_it_get(struct pkgdb_it *it) 
{
    return rpmdb_it_get(it->_it);
}

void pm_rpm_db_it_destroy(struct pkgdb_it *it)
{
    rpmdb_it_destroy(it->_it);
    n_free(it->_it);
    it->_it = NULL;
}


int pm_rpm_db_it_init(struct pkgdb_it *it, int tag, const char *arg)
{
    struct rpmdb_it *rpmit;

    rpmit = n_malloc(sizeof(*rpmit));
    rpmdb_it_init(it->_db->dbh, rpmit, tag, arg);
    it->_it = rpmit;
    it->_get = pm_rpm_db_it_get;
    it->_get_count = pm_rpm_db_it_get_count;
    it->_destroy = pm_rpm_db_it_destroy;
    return 1;
}


