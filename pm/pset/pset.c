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
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>
#include <trurl/nmalloc.h>
#include <trurl/n_snprintf.h>


#include <vfile/vfile.h>

#include "sigint/sigint.h"
#include "i18n.h"

#include "depdirs.h"
#include "misc.h"
#include "log.h"
#include "pkg.h"
#include "capreq.h"
#include "pm_pset.h"
#include "pkgset.h"



void pm_pset_destroy(void *pm_pset) 
{
    struct pm_pset *pm = pm_pset;
    n_cfree(&pm->installer_path);
    pkgset_free(pm->ps);
    n_hash_free(pm->psh);
    free(pm);
}

#if 0
static struct pkgset *newps(
    if ((ps = pkgset_new(NULL)) == NULL)
        return NULL;
        
    if (!pkgset_add_pkgdir(ps, pkgdir)) {
        logn(LOGERR, _("no packages loaded"));
        pkgset_free(ps);
        return NULL;
    }

    pkgset_setup(ps, PSET_VRFY_MERCY);
#endif

void *pm_pset_init(struct source *src) 
{
    struct pm_pset *pm_pset;
    char path[PATH_MAX];
    struct pkgdir *dir;
    struct pkgset *ps;

    if (source_is_remote(src))
        return NULL;
    

    if ((dir = pkgdir_srcopen(src, 0)) == NULL)
        return NULL;
            
    if (!pkgdir_load(dir, 0, 0)) {
        pkgdir_free(dir);
        return NULL;
    }
    
    if ((ps = pkgset_new(NULL)) == NULL)
        return NULL;
        
    if (!pkgset_add_pkgdir(ps, dir)) {
        logn(LOGERR, _("no packages loaded"));
        pkgset_free(ps);
        pkgdir_free(dir);
        return NULL;
    }

    pkgset_setup(ps, PSET_VRFY_MERCY);
    pm_pset = n_malloc(sizeof(*pm_pset));
    pm_pset->ps = ps;
    if (poldek_lookup_external_command(path, sizeof(path), "packagecopy.sh"))
        pm_pset->installer_path = n_strdup(path);
    else
        pm_pset->installer_path = NULL;
    return pm_pset;
}

void *pm_pset_opendb(void *pm_pset, const char *rootdir, const char *dbpath,
                     mode_t mode)
{
    rootdir = rootdir;
    dbpath = dbpath;
    mode = mode;
    return pm_pset;
}

void pm_pset_closedb(void *pm_pset) 
{
    pm_pset = pm_pset;
    return;
}



/* remeber! don't touch any member */
struct psetdb_it {
    int                  tag;
    int                  i;
    struct pm_dbrec      dbrec;
    struct pm_pset       *pm_pset;
    tn_array             *pkgs;
};


static
int psetdb_it_init(struct pm_pset *pm_pset, struct psetdb_it *it,
                   int tag, const char *arg)
{
    int pstag = 0;
    struct pm_pset *pm = pm_pset;
    
    switch (tag) {
        case PMTAG_RECNO:
            pstag = PS_SEARCH_RECNO;
            break;
            
        case PMTAG_NAME:
            pstag = PS_SEARCH_NAME;
            break;
            
        case PMTAG_FILE:
            pstag = PS_SEARCH_FILE;
            break;

        case PMTAG_CAP:
            pstag = PS_SEARCH_CAP;
            break;
            
        case PMTAG_REQ:
            pstag = PS_SEARCH_REQ;
            break;
            
        case PMTAG_CNFL:
            pstag = PS_SEARCH_CNFL;
            break;
            
        case PMTAG_OBSL:
            pstag = PS_SEARCH_OBSL;
            break;
            
        default:
            die();
    }
    
    it->i = 0;
    it->tag = tag;
    it->pm_pset = pm;
    it->pkgs = pkgset_search(pm->ps, pstag, arg);
    return it->pkgs ? n_array_size(it->pkgs) : 0;
}

static
void psetdb_it_destroy(struct psetdb_it *it) 
{
    if (it->pkgs)
        n_array_free(it->pkgs);
}


static
const struct pm_dbrec *psetdb_it_get(struct psetdb_it *it)
{
    struct pkg *pkg;

    if (it->pkgs == NULL)
        return NULL;

    if (it->i == n_array_size(it->pkgs))
        return NULL;

    pkg = n_array_nth(it->pkgs, it->i++);
    it->dbrec.hdr = pkg;
    it->dbrec.recno = pkg->recno;
    return &it->dbrec;
}


static
int psetdb_it_get_count(struct psetdb_it *it)
{
    if (it->pkgs)
        return n_array_size(it->pkgs);
    return 0;
}

static 
int pm_pset_db_it_get_count(struct pkgdb_it *it) 
{
    return psetdb_it_get_count(it->_it);
}

static 
const struct pm_dbrec *pm_pset_db_it_get(struct pkgdb_it *it) 
{
    return psetdb_it_get(it->_it);
}


void pm_pset_db_it_destroy(struct pkgdb_it *it)
{
    psetdb_it_destroy(it->_it);
    n_free(it->_it);
    it->_it = NULL;
}


int pm_pset_db_it_init(struct pkgdb_it *it, int tag, const char *arg)
{
    struct psetdb_it *psit;
    
    psit = n_malloc(sizeof(*psit));
    psetdb_it_init(it->_db->dbh, psit, tag, arg);
    it->_it = psit;
    it->_get = pm_pset_db_it_get;
    it->_get_count = pm_pset_db_it_get_count;
    it->_destroy = pm_pset_db_it_destroy;
    return 1;
}



int pm_psethdr_nevr(void *h, char **name,
                    int32_t *epoch, char **version, char **release)
{
    struct pkg *pkg = h;
    
    *name = pkg->name;
    *epoch = pkg->epoch;
    *version = pkg->ver;
    *release = pkg->rel;
    return 1;
}


struct pkg *pm_pset_ldhdr(tn_alloc *na, void *hdr, const char *fname,
                          unsigned fsize, unsigned ldflags)
{
    struct pkg *pkg = hdr;
    na = na; fname = fname; fsize = fsize; ldflags = ldflags;
    return pkg_link(pkg);

}

tn_array *pm_pset_ldhdr_capreqs(tn_array *arr, void *hdr, int crtype) 
{
    tn_array *crs = NULL;
    struct pkg *pkg = hdr;
    int i;
    
    switch (crtype) {
        case PMCAP_CAP:
            crs = pkg->caps;
            break;
            
        case PMCAP_REQ:
            crs = pkg->reqs;
            break;
            
        case PMCAP_CNFL:
        case PMCAP_OBSL:
            crs = pkg->cnfls;
            break;
            
        default:
            n_die("%d: unknown type (internal error)", crtype);
    }

    if (crs == NULL)
        return NULL;

    for (i=0; i < n_array_size(crs); i++) {
        struct capreq *cr = n_array_nth(crs, i);
        switch (crtype) {
            case PMCAP_CAP:
            case PMCAP_REQ:
                n_array_push(arr, capreq_clone(NULL, cr));
                break;

            case PMCAP_CNFL:
                if (!capreq_is_obsl(cr))
                    n_array_push(arr, capreq_clone(NULL, cr));
                break;

            case PMCAP_OBSL:
                if (capreq_is_obsl(cr))
                    n_array_push(arr, capreq_clone(NULL, cr));
                break;
        }
    }
    
    return arr;
}



int pm_pset_packages_install(void *pm_pset,
                             tn_array *pkgs, tn_array *pkgs_toremove,
                             struct poldek_ts *ts) 
{
    struct pkgdir *pkgdir;
    struct pm_pset *pm = pm_pset;
    char path[PATH_MAX];
    int i;

    n_assert(pm->ps);
    n_assert(pm->ps->pkgdirs);
    n_assert(n_array_size(pm->ps->pkgdirs) == 1);
    

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        if (pkg_localpath(pkg, path, sizeof(path), ts->cachedir))
            msgn(0, "%%install %s %s", path, pkgdir->path);
    }

    pm_pset_packages_uninstall(pm, pkgs_toremove, ts);
    return 1;
}


int pm_pset_packages_uninstall(void *pm_pset, tn_array *pkgs,
                               struct poldek_ts *ts)
{
    struct pm_pset *pm = pm_pset;
    char path[PATH_MAX];
    int i;
    
    pm = pm;
    ts = ts;
    
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        if (pkg_path(pkg, path, sizeof(path)))
            msgn(0, "%%uninstall %s", path);
    }
    return 1;
}
