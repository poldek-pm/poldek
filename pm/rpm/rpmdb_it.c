/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
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
#ifdef HAVE_RPMMI
    rpmmi                mi;
#else
    rpmdbMatchIterator   mi;
#endif /* HAVE_RPMMI */
    
    struct pm_dbrec      dbrec;
    rpmdb                db; 
};

static
int rpmdb_it_init(rpmdb db, struct rpmdb_it *it, int tag, const char *arg)
{
    int rpmtag = 0, argsize = 0;
    char path[PATH_MAX];
    
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

        case PMTAG_DIRNAME:
            rpmtag = RPMTAG_DIRNAMES;
            n_snprintf(path, sizeof(path), "%s/", arg);
            arg = path;
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
            n_assert(0);
            break;
    }
    
    DBGF("%p, %p\n", it, db);
    it->tag = tag;
    it->db = db;
#ifdef HAVE_RPMMI
    it->mi = rpmmiInit(db, rpmtag, arg, argsize);
    return rpmmiCount(it->mi);
#else
    it->mi = rpmdbInitIterator(db, rpmtag, arg, argsize);
    return rpmdbGetIteratorCount(it->mi);
#endif /* HAVE_RPMMI */
}

static
void rpmdb_it_destroy(struct rpmdb_it *it) 
{
#ifdef HAVE_RPMMI
    rpmdbFreeIterator(it->mi);
#else
    rpmmiFree(it->mi);
#endif /* HAVE_RPMMI */
    it->mi = NULL;
    it->dbrec.hdr = NULL;
    DBGF("%p, %p\n", it, it->db);
}


static
const struct pm_dbrec *rpmdb_it_get(struct rpmdb_it *it)
{
#ifdef HAVE_RPMMI
    it->dbrec.hdr = rpmmiNext(it->mi);
    
    if (it->dbrec.hdr == NULL)
        return NULL;

    it->dbrec.recno = rpmmiInstance(it->mi);
#else
    it->dbrec.hdr = rpmdbNextIterator(it->mi);
    
    if (it->dbrec.hdr == NULL)
        return NULL;

    it->dbrec.recno = rpmdbGetIteratorOffset(it->mi);
#endif /* HAVE_RPMMI */

    return &it->dbrec;
}

static
int rpmdb_it_get_count(struct rpmdb_it *it)
{
#ifdef HAVE_RPMMI
    return rpmmiCount(it->mi);
#else
    return rpmdbGetIteratorCount(it->mi);
#endif /* HAVE_RPMMI */
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


