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

#include <rpm/rpmlib.h>
#include <rpm/rpmio.h>
#include <rpm/rpmurl.h>
#include <rpm/rpmmacro.h>

#ifdef HAVE_RPM_4_1
# include <rpm/rpmdb.h>
#endif

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
#include "dbpkg.h"
#include "capreq.h"

#include "rpm.h"
#include "rpmdb_it.h"
#define POLDEK_RPM_INTERNAL
#include "rpm_pkg_ld.h"

static int initialized = 0;
int rpmlib_verbose = 0;

static
int header_evr_match_req(Header h, const struct capreq *req);
static
int header_cap_match_req(Header h, const struct capreq *req, int strict);


int rpm_initlib(tn_array *macros) 
{
    if (initialized == 0) {
        if (rpmReadConfigFiles(NULL, NULL) != 0) {
            logn(LOGERR, "failed to read rpmlib configs");
            return 0;
        }
        initialized = 1;
    }
    
    if (macros) {
        int i;
        
        for (i=0; i<n_array_size(macros); i++) {
            char *def, *macro;
            
            if ((macro = n_array_nth(macros, i)) == NULL)
                continue;
            
            if ((def = strchr(macro, ' ')) == NULL && 
                (def = strchr(macro, '\t')) == NULL) {
                logn(LOGERR, _("%s: invalid macro definition"), macro);
                return 0;
                
            } else {
                char *sav = def;
                
                *def = '\0';
                def++;
                while(isspace(*def))
                    def++;
                msg(4, "addMacro %s %s\n", macro, def);
                addMacro(NULL, macro, NULL, def, RMIL_DEFAULT);
                *sav = ' ';
            }
        }
    }
    
    return 1;
}

void rpm_define(const char *name, const char *val) 
{
    addMacro(NULL, name, NULL, val, RMIL_DEFAULT);
}


rpmdb rpm_opendb(const char *dbpath, const char *rootdir, mode_t mode) 
{
    rpmdb db = NULL;
    
    if (dbpath)
        addMacro(NULL, "_dbpath", NULL, dbpath, RMIL_DEFAULT);
    
    if (rpmdbOpen(rootdir ? rootdir : "/", &db, mode, 0) != 0) {
        db = NULL;
        logn(LOGERR, _("%s%s: open rpm database failed"),
             rootdir ? rootdir:"", dbpath ? dbpath : RPM_DBPATH);
    }
    
#if ENABLE_TRACE    
    DBGF("%p %d\n", db, db->nrefs);
    system("ls -l /proc/$(echo `ps aux | grep poldek | grep -v grep` | awk '{print $2}')/fd/ | grep rpm");
    sleep(3);
#endif
    
    return db;
}

char *rpm_get_dbpath(char *path, size_t size)
{
    char *p;

    rpm_initlib(NULL);
    p = (char*)rpmGetPath("%{_dbpath}", NULL);
    if (p == NULL || *p == '%')
        n_snprintf(path, size, "%s", RPM_DBPATH);
    else
        n_snprintf(path, size, "%s", p);

    if (p)
        free(p);
        
    return path;
}

time_t rpm_dbmtime(const char *dbfull_path) 
{
    const char *file = "packages.rpm";
    char path[PATH_MAX];
    struct stat st;
    
#ifdef HAVE_RPM_4_0
    file = "Packages";
#endif
    
    snprintf(path, sizeof(path), "%s/%s", dbfull_path, file);
     
    if (stat(path, &st) != 0)
        return 0;

    return st.st_mtime;
}

void rpm_closedb(rpmdb db) 
{
    DBGF("DB %p close %d\n", db, db->nrefs);
    
    rpmdbClose(db);
    
#if ENABLE_TRACE        
    system("ls -l /proc/$(echo `ps aux | grep poldek | grep -v grep` | awk '{print $2}')/fd/ | grep rpm");
    sleep(3);
#endif
    
    db = NULL;
}

tn_array *rpm_get_file_conflicted_dbpkgs(rpmdb db, const char *path,
                                         tn_array *cnfldbpkgs, 
                                         tn_array *unistdbpkgs, unsigned ldflags)
{
    const struct dbrec *dbrec;
    struct rpmdb_it it;

    if (cnfldbpkgs == NULL)
        cnfldbpkgs = dbpkg_array_new(4);
    
    rpmdb_it_init(db, &it, RPMITER_FILE, path);
    while((dbrec = rpmdb_it_get(&it)) != NULL) {
        if (unistdbpkgs && dbpkg_array_has(unistdbpkgs, dbrec->recno))
            continue;
        if (cnfldbpkgs && dbpkg_array_has(cnfldbpkgs, dbrec->recno))
            continue;
        
        n_array_push(cnfldbpkgs, dbpkg_new(dbrec->recno, dbrec->h, ldflags));
        break;	
    }
    rpmdb_it_destroy(&it);
    
    if (n_array_size(cnfldbpkgs) == 0) {
        n_array_free(cnfldbpkgs);
        cnfldbpkgs = NULL;
    }
    
    return cnfldbpkgs;
}


static
int lookup_pkg(rpmdb db, const struct capreq *req, tn_array *unistdbpkgs)
{
    int rc = 0;

    const struct dbrec *dbrec;
    struct rpmdb_it it;

    rpmdb_it_init(db, &it, RPMITER_NAME, capreq_name(req));
    while ((dbrec = rpmdb_it_get(&it)) != NULL) {
        if (unistdbpkgs && dbpkg_array_has(unistdbpkgs, dbrec->recno))
            continue;

        if (header_evr_match_req(dbrec->h, req)) {
            rc = 1;
            break;
        }
    }
    rpmdb_it_destroy(&it);
    
    return rc;
}

static
int lookup_file(rpmdb db, const struct capreq *req, tn_array *unistdbpkgs)
{
    struct rpmdb_it it;
    const struct dbrec *dbrec;
    int finded = 0;
    
    rpmdb_it_init(db, &it, RPMITER_FILE, capreq_name(req));
    while ((dbrec = rpmdb_it_get(&it)) != NULL) {
        if (unistdbpkgs && dbpkg_array_has(unistdbpkgs, dbrec->recno))
            continue;
        finded = 1;
        break;	
    }
    rpmdb_it_destroy(&it);
    return finded;
}


static
int lookup_cap(rpmdb db, const struct capreq *req, int strict,
               tn_array *unistdbpkgs)
{
    struct rpmdb_it it;
    const struct dbrec *dbrec;
    int rc = 0;
    
    rpmdb_it_init(db, &it, RPMITER_CAP, capreq_name(req));
    while ((dbrec = rpmdb_it_get(&it)) != NULL) {
        if (unistdbpkgs && dbpkg_array_has(unistdbpkgs, dbrec->recno))
            continue;
        
        if (header_cap_match_req(dbrec->h, req, strict)) {
            rc = 1;
            break;
        }
    }
    rpmdb_it_destroy(&it);
    return rc;
}


static
int header_evr_match_req(Header h, const struct capreq *req)
{
    struct pkg  pkg;
    uint32_t    *epoch;
    int         rc;
    
    headerNVR(h, (void*)&pkg.name, (void*)&pkg.ver, (void*)&pkg.rel);
    if (pkg.name == NULL || pkg.ver == NULL || pkg.rel == NULL) {
        logn(LOGERR, "headerNVR() failed");
        return 0;
    }

    if (headerGetEntry(h, RPMTAG_EPOCH, &rc, (void *)&epoch, NULL)) 
        pkg.epoch = *epoch;
    else
        pkg.epoch = 0;
    
    if (pkg_evr_match_req(&pkg, req, 0)) {
        DBGF("%s match %s!\n", pkg_snprintf_epoch_s(&pkg), pkg.epoch,
             capreq_snprintf_s0(req));
        return 1;
    }
    
        

    return 0;
}


static
int header_cap_match_req(Header h, const struct capreq *req, int strict)
{
    struct pkg  pkg;
    int         rc;

    rc = 0;
    pkg.caps = capreq_arr_new(0);
    rpm_capreqs_ldhdr(pkg.caps, h, CRTYPE_CAP);
    
    if (n_array_size(pkg.caps) > 0) {
        n_array_sort(pkg.caps);
        rc = pkg_caps_match_req(&pkg, req, strict);
    }

    n_array_free(pkg.caps);

    return rc;
}

    
#if 0 
static
int is_req_match(rpmdb db, dbiIndexSet matches, const struct capreq *req) 
{
    Header h;
    int i, rc;

    rc = 0;
    for (i = 0; i < matches.count; i++) 
        if ((h = rpmdbGetRecord(db, matches.recs[i].recOffset))) {
            if (header_match_req(h, req, strict)) {
                rc = 1;
                headerFree(h);
                break;
            }
            headerFree(h);
        }

    dbiFreeIndexRecord(matches);
    return rc;
}
#endif


int rpm_dbmatch_req(rpmdb db, const struct capreq *req, int strict,
                    tn_array *unistallrnos) 
{
    int rc;
    int is_file;

    is_file = (*capreq_name(req) == '/' ? 1:0);

    if (!is_file && lookup_pkg(db, req, unistallrnos))
        return 1;
    
    rc = lookup_cap(db, req, strict, unistallrnos);
    if (rc)
        return 1;

    if (is_file && lookup_file(db, req, unistallrnos))
        rc = 1;

    return rc;
    
}


int rpm_dbmap(rpmdb db, void (*mapfn)(unsigned recno, void *header, void *arg),
              void *arg) 
{
    int n = 0;
    Header h;

#ifdef HAVE_RPM_4_0
    rpmdbMatchIterator mi;
    mi = rpmdbInitIterator(db, RPMDBI_PACKAGES, NULL, 0);
    while ((h = rpmdbNextIterator(mi)) != NULL) {
        unsigned int recno = rpmdbGetIteratorOffset(mi);
#ifdef HAVE_RPM_4_1             /* omit pubkeys */
        if (headerIsEntry(h, RPMTAG_PUBKEYS))
            continue;
#endif
        if (sigint_reached())
            return 0;
        
        mapfn(recno, h, arg);
        n++;
    }
    rpmdbFreeIterator(mi);
#else	/* !HAVE_RPM_4_0 */
    int recno;

    recno = rpmdbFirstRecNum(db);
    if (recno == 0)
        return 0;

    if (recno < 0)
        die();
    
    while (recno > 0) {
        if ((h = rpmdbGetRecord(db, recno))) {
            mapfn(recno, h, arg);
            n++;
            headerFree(h);
        }
        
        recno = rpmdbNextRecNum(db, recno);
    }
#endif /* HAVE_RPM_4_0 */
    
    return n;
}


int rpm_get_pkgs_requires_capn(rpmdb db, tn_array *dbpkgs, const char *capname,
                               tn_array *unistdbpkgs, unsigned ldflags)
{
    struct rpmdb_it it;
    const struct dbrec *dbrec;
    int n = 0;
    
    rpmdb_it_init(db, &it, RPMITER_REQ, capname);
    while ((dbrec = rpmdb_it_get(&it)) != NULL) {
        struct dbpkg *dbpkg;
        
        if (unistdbpkgs && dbpkg_array_has(unistdbpkgs, dbrec->recno))
            continue;
        
        if (dbpkg_array_has(dbpkgs, dbrec->recno))
            continue;

        dbpkg = dbpkg_new(dbrec->recno, dbrec->h, ldflags);
        //msg(1, "%s <- %s\n", capname, pkg_snprintf_s(dbpkg->pkg));
        //mem_info(1, "get_pkgs_requires_capn\n");
        n_array_push(dbpkgs, dbpkg);
        n++;
    }
    rpmdb_it_destroy(&it);
    return n;
}


int rpm_get_obsoletedby_cap_N(rpmdb db, struct capreq *cap, struct dbrec_process *dp)
{
    struct rpmdb_it it;
    const struct dbrec *dbrec;
    int n = 0;
    
    rpmdb_it_init(db, &it, RPMITER_NAME, capreq_name(cap));
    while ((dbrec = rpmdb_it_get(&it)) != NULL) {
        if (dp->skip && dp->skip(dbrec->recno, dbrec->h, dp->arg))
            continue;
        
        if (header_evr_match_req(dbrec->h, cap)) {
            if ((dp->process(dbrec->recno, dbrec->h, dp->arg)))
                n++;
        }
    }
    rpmdb_it_destroy(&it);
    return n;
}


int rpm_get_obsoletedby_cap(rpmdb db, tn_array *dbpkgs, struct capreq *cap,
                            unsigned ldflags)
{
    struct rpmdb_it it;
    const struct dbrec *dbrec;
    int n = 0;
    
    rpmdb_it_init(db, &it, RPMITER_NAME, capreq_name(cap));
    while ((dbrec = rpmdb_it_get(&it)) != NULL) {
        if (dbpkg_array_has(dbpkgs, dbrec->recno))
            continue;
        
        if (header_evr_match_req(dbrec->h, cap)) {
            struct dbpkg *dbpkg = dbpkg_new(dbrec->recno, dbrec->h, ldflags);
            n_array_push(dbpkgs, dbpkg);
            n_array_sort(dbpkgs);
            n++;
        }
    }
    rpmdb_it_destroy(&it);
    return n;
}


int rpm_get_obsoletedby_pkg(rpmdb db, tn_array *dbpkgs, const struct pkg *pkg,
                            unsigned ldflags)
{
    struct capreq *self_cap;
    int i, n = 0;
    

    self_cap = capreq_new(pkg->name, pkg->epoch, pkg->ver, pkg->rel,
                          REL_EQ | REL_LT, 0);
    n = rpm_get_obsoletedby_cap(db, dbpkgs, self_cap, ldflags);
    capreq_free(self_cap);
    
    if (pkg->cnfls == NULL)
        return n;
    
    for (i=0; i < n_array_size(pkg->cnfls); i++) {
        struct capreq *cnfl = n_array_nth(pkg->cnfls, i);

        if (!cnfl_is_obsl(cnfl))
            continue;

        n += rpm_get_obsoletedby_cap(db, dbpkgs, cnfl, ldflags);
    }
    
    return n;
}


static 
int hdr_pkg_cmp_evr(Header h, const struct pkg *pkg)
{
    int rc;
    struct pkg  tmpkg;
    uint32_t    *epoch;
        
    headerNVR(h, (void*)&tmpkg.name, (void*)&tmpkg.ver,
              (void*)&tmpkg.rel);
    
    if (tmpkg.name == NULL || tmpkg.ver == NULL || tmpkg.rel == NULL) {
        logn(LOGERR, "headerNVR() failed");
        return 0;
    }
        
    if (headerGetEntry(h, RPMTAG_EPOCH, &rc, (void *)&epoch, NULL))
        tmpkg.epoch = *epoch;
    else
        tmpkg.epoch = 0;
    
    rc = pkg_cmp_evr(&tmpkg, pkg);
    
    return rc;
}


int rpm_is_pkg_installed(rpmdb db, const struct pkg *pkg, int *cmprc,
                         struct dbrec *dbrecp)
{
    int count = 0;
    struct rpmdb_it it;
    const struct dbrec *dbrec;

    rpmdb_it_init(db, &it, RPMITER_NAME, pkg->name);
    count = rpmdb_it_get_count(&it);
    if (count > 0 && (cmprc || dbrecp)) {
        if ((dbrec = rpmdb_it_get(&it)) == NULL) {
            logn(LOGWARN, "%s: rpmdb iterator returns NULL, "
                 "possibly corrupted database", pkg->name);
            count = 0; /* assume that package isn't installed */
        } else {
            if (cmprc)
                *cmprc = -hdr_pkg_cmp_evr(dbrec->h, pkg);
        
            if (dbrecp) {
                dbrecp->recno = dbrec->recno;
                dbrecp->h = headerLink(dbrec->h);
            }
        }
    }

    rpmdb_it_destroy(&it);
    return count;
}


tn_array *rpm_get_packages(rpmdb db, const struct pkg *pkg, unsigned ldflags)
{
    const struct dbrec *dbrec;
    tn_array *dbpkgs = NULL;
    int count = 0;
    struct rpmdb_it it;
        
    rpmdb_it_init(db, &it, RPMITER_NAME, pkg->name);
    count = rpmdb_it_get_count(&it);
    if (count == 0)
        return NULL;

    dbpkgs = dbpkg_array_new(2);

    while ((dbrec = rpmdb_it_get(&it))) 
        n_array_push(dbpkgs, dbpkg_new(dbrec->recno, dbrec->h, ldflags));
    
    rpmdb_it_destroy(&it);
    return dbpkgs;
}

tn_array *rpm_get_conflicted_dbpkgs(rpmdb db, const struct capreq *cap,
                                    tn_array *unistdbpkgs, unsigned ldflags)
{
    tn_array *dbpkgs = NULL;
    struct rpmdb_it it;
    const struct dbrec *dbrec;
    

    rpmdb_it_init(db, &it, RPMITER_CNFL, capreq_name(cap));
    while ((dbrec = rpmdb_it_get(&it))) {
        if (dbpkg_array_has(unistdbpkgs, dbrec->recno))
            continue;

        if (dbpkgs == NULL)
            dbpkgs = dbpkg_array_new(4);
        
        n_array_push(dbpkgs, dbpkg_new(dbrec->recno, dbrec->h, ldflags));
    }
    
    rpmdb_it_destroy(&it);
    return dbpkgs;
}


tn_array *rpm_get_provides_dbpkgs(rpmdb db, const struct capreq *cap,
                                  tn_array *unistdbpkgs, unsigned ldflags)
{
    tn_array *dbpkgs = NULL;
    struct rpmdb_it it;
    const struct dbrec *dbrec;
    

    rpmdb_it_init(db, &it, RPMITER_CAP, capreq_name(cap));
    while ((dbrec = rpmdb_it_get(&it))) {
        if (unistdbpkgs && dbpkg_array_has(unistdbpkgs, dbrec->recno))
            continue;

        if (dbpkgs == NULL)
            dbpkgs = dbpkg_array_new(4);
        
        else if (dbpkg_array_has(dbpkgs, dbrec->recno))
            continue;
        
        n_array_push(dbpkgs, dbpkg_new(dbrec->recno, dbrec->h, ldflags));
    }
    
    rpmdb_it_destroy(&it);
    return dbpkgs;
}

#if defined HAVE_RPMLOG && !defined ENABLE_STATIC
/* hack: rpmlib dumps messges to stdout only... (AFAIK)  */
void rpmlog(int prii, const char *fmt, ...) 
{
    va_list args;
    int pri, mask;
    int rpmlogMask, logpri = LOGERR, verbose_level = -1;

    pri =  RPMLOG_PRI(prii);
    mask = RPMLOG_MASK(pri);
    rpmlogMask = rpmlogSetMask(0); /* get mask */
        
    if ((mask & rpmlogMask) == 0)
	return;

    if (pri <= RPMLOG_ERR)
        logpri = LOGERR;
    
    else if (pri == RPMLOG_WARNING || pri == RPMLOG_NOTICE)
        logpri = LOGWARN;
    
    else {
        logpri = LOGINFO;
        verbose_level = 2;
    }

    va_start(args, fmt);

#if 0    
    printf("%d, v = %d, verbose = %d, rpmlib_verbose = %d\n", pri, verbose_level,
           verbose, rpmlib_verbose);
    vprintf(fmt, args);
#endif
    
    if (verbose_level > verbose || verbose_level > rpmlib_verbose)
        return;
    
    if ((logpri & (LOGERR | LOGWARN)) == 0)
        vlog(logpri, 0, fmt, args);
        
    else {                  /* basename(path) */
        char m[1024], *p, *q;
        int n;

        
        n = n_vsnprintf(m, sizeof(m), fmt, args);

        if (n > 0 && m[n - 1] == '\n')
            m[n - 1] = '\0';
        
        p = m;
        if (*p == '/' && strstr(p, ".rpm")) {
            p++;
            q = p;
            while ((p = strchr(q, '/')))
                q = p + 1;
            p = q;
        }

        if (strstr(m, "md5 OK") || strstr(m, "gpg OK") || strstr(m, "pgp OK"))
            logpri |= LOGFILE;
        
        log(logpri | LOGWARN, "%s\n", p);
    }
        
    va_end(args);
}

#endif /* HAVE_RPMLOG */
    
        
