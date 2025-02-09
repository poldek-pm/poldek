/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

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

#include "i18n.h"
#include "misc.h"
#include "log.h"

#include "pm_rpm.h"
#include "pm/pm.h"
#include "pm/mod.h"

/* remeber! don't touch any member */
struct rpmdb_it {
    int                  tag;
    rpmdbMatchIterator   mi;
    struct pm_dbrec      dbrec;
    rpmts                rpmts;
};

static
void rpmdb_it_init(struct rpmorg_db *db, struct rpmdb_it *it, int tag, const char *arg)
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

    //DBGF("%p, %p\n", it, db);
    it->tag = tag;
    it->mi = rpmdbInitIterator(rpmtsGetRdb(db->ts), rpmtag, arg, argsize);
}

static
void rpmdb_it_destroy(struct rpmdb_it *it)
{
    DBGF("%p\n", it);
    rpmdbFreeIterator(it->mi);
    it->mi = NULL;
    it->dbrec.hdr = NULL;
}


static
const struct pm_dbrec *rpmdb_it_get(struct rpmdb_it *it)
{
    it->dbrec.hdr = rpmdbNextIterator(it->mi);

    if (it->dbrec.hdr == NULL)
        return NULL;

    it->dbrec.recno = rpmdbGetIteratorOffset(it->mi);
    return &it->dbrec;
}

static
int rpmdb_it_get_count(struct rpmdb_it *it)
{
    return rpmdbGetIteratorCount(it->mi);
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
