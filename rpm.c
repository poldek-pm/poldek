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

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <rpm/rpmlib.h>
#include <rpm/rpmio.h>
#include <rpm/rpmurl.h>
#include <rpm/rpmmacro.h>
#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>

#include <vfile/vfile.h>

#include "rpmadds.h"
#include "log.h"
#include "pkg.h"
#include "capreq.h"

static
int header_evr_match_req(Header h, const struct capreq *req);
static
int header_cap_match_req(Header h, const struct capreq *req, int strict);

int rpm_initlib(tn_array *macros) 
{
    if (rpmReadConfigFiles(NULL, NULL) != 0) {
        log(LOGERR, "rpmlib init failed\n");
        return 0;
    }

    if (macros) {
        int i;
        
        for (i=0; i<n_array_size(macros); i++) {
            char *def, *macro;
            
            if ((macro = n_array_nth(macros, i)) == NULL)
                continue;
            
            if ((def = strchr(macro, ' ')) == NULL && 
                (def = strchr(macro, '\t')) == NULL) {
                log(LOGERR, "%s: invalid macro definition\n", macro);
                return 0;
                
            } else {
                char *sav = def;
                
                *def = '\0';
                def++;
                while(isspace(*def))
                    def++;
                msg(2, "addMacro %s %s\n", macro, def);
                addMacro(NULL, macro, NULL, def, RMIL_DEFAULT);
                *sav = ' ';
            }
        }
    }
    return 1;
}


rpmdb rpm_opendb(const char *dbpath, const char *rootdir, int mode) 
{
    rpmdb db = NULL;
    
    if (dbpath)
        addMacro(NULL, "_dbpath", NULL, dbpath, RMIL_DEFAULT);

    if (rpmdbOpen(rootdir ? rootdir : "/", &db, mode, 0) != 0) {
        db = NULL;
        log(LOGERR, "failed to open rpm database\n");
    }
    
    return db;
}


void rpm_closedb(rpmdb db) 
{
    rpmdbClose(db);
    db = NULL;
}


static void rpm_die(void) 
{
    log(LOGERR, "database error\n");
    die();
}



tn_array *rpm_get_file_conflict_hdrs(rpmdb db, const char *path,
                                     tn_array *exclrnos) 
{
    tn_array *cnflpkghdrs = n_array_new(4, (tn_fn_free)headerFree, NULL);
    
#ifdef HAVE_RPM_4_0
    rpmdbMatchIterator mi;
    Header h;

    mi = rpmdbInitIterator(db, RPMTAG_BASENAMES, path, 0);
    while((h = rpmdbNextIterator(mi)) != NULL) {
	unsigned int recno = rpmdbGetIteratorOffset(mi);
 	if (exclrnos && n_array_bsearch(exclrnos, (void*)recno))
	    continue;
        
        n_array_push(cnflpkghdrs, headerLink(h));
	break;	
    }
    rpmdbFreeIterator(mi);
    
#else /* HAVE_RPM_4_0 */

    dbiIndexSet matches;
    int rc;

    matches.count = 0;
    matches.recs = NULL;
    rc = rpmdbFindByFile(db, path, &matches);

    if (rc != 0) {
        if (rc < 0) 
            rpm_die();
        
        
    } else {
        int i;
        for (i = 0; i < matches.count; i++) {
            Header h;
            
            if (exclrnos &&
                n_array_bsearch(exclrnos, (void*)matches.recs[i].recOffset))
                continue;
            
            if ((h = rpmdbGetRecord(db, matches.recs[i].recOffset))) {
                n_array_push(cnflpkghdrs, headerLink(h));
                headerFree(h);
            }

            break;
        }
    }
#endif	/* !HAVE_RPM_4_0 */

    if (n_array_size(cnflpkghdrs) == 0) {
        n_array_free(cnflpkghdrs);
        cnflpkghdrs = NULL;
    }
    
    return cnflpkghdrs;
}



static
int lookup_pkg(rpmdb db, const struct capreq *req, tn_array *exclrnos)
{
    int rc = 0;

#ifdef HAVE_RPM_4_0
    rpmdbMatchIterator mi;
    Header h;

    mi = rpmdbInitIterator(db, RPMTAG_NAME, capreq_name(req), 0);
    while((h = rpmdbNextIterator(mi)) != NULL) {
	unsigned int recno = rpmdbGetIteratorOffset(mi);

        if (exclrnos && n_array_bsearch(exclrnos, (void*)recno))
	    continue;
        
 	if (header_evr_match_req(h, req)) {
	    rc = 1;
	    break;
	}
    }
    rpmdbFreeIterator(mi);
    
#else /* HAVE_RPM_4_0 */
    dbiIndexSet matches;

    matches.count = 0;
    matches.recs = NULL;
    rc = rpmdbFindPackage(db, capreq_name(req), &matches);

    if (rc != 0) {
        if (rc < 0)
            rpm_die();
        rc = 0;
        
    } else if (rc == 0) {
        Header h;
        int i;
        
        for (i = 0; i < matches.count; i++) {
            if (exclrnos &&
                n_array_bsearch(exclrnos, (void*)matches.recs[i].recOffset))
                continue;
        
            if ((h = rpmdbGetRecord(db, matches.recs[i].recOffset))) {
                if (header_evr_match_req(h, req)) {
                    rc = 1;
                    headerFree(h);
                    break;
                }
                headerFree(h);
            }
        }
        
        dbiFreeIndexRecord(matches);
    }
#endif /* HAVE_RPM_4_0 */

    return rc;
}

static
int lookup_file(rpmdb db, const struct capreq *req, tn_array *exclrnos)
{
    int finded = 0;

#ifdef HAVE_RPM_4_0
    rpmdbMatchIterator mi;
    Header h;

    mi = rpmdbInitIterator(db, RPMTAG_BASENAMES, capreq_name(req), 0);
    while((h = rpmdbNextIterator(mi)) != NULL) {
	unsigned int recno = rpmdbGetIteratorOffset(mi);
 	if (exclrnos && n_array_bsearch(exclrnos, (void*)recno))
	    continue;
   	finded = 1;
	break;	
    }
    rpmdbFreeIterator(mi);
    
#else /* HAVE_RPM_4_0 */

    dbiIndexSet matches;
    int rc;

    matches.count = 0;
    matches.recs = NULL;
    rc = rpmdbFindByFile(db, capreq_name(req), &matches);

    if (rc != 0) {
        if (rc < 0) 
            rpm_die();
        finded = 0;
        
    } else {
        int i;
        for (i = 0; i < matches.count; i++) {
            if (exclrnos &&
                n_array_bsearch(exclrnos, (void*)matches.recs[i].recOffset))
                continue;
            
            finded = 1;
            break;
        }
    }
#endif	/* !HAVE_RPM_4_0 */

    return finded;
}


static
int lookup_cap(rpmdb db, const struct capreq *req, int strict,
               tn_array *exclrnos)
{
    int rc = 0;

#ifdef HAVE_RPM_4_0
    rpmdbMatchIterator mi;
    Header h;

    mi = rpmdbInitIterator(db, RPMTAG_PROVIDES, capreq_name(req), 0);
    while((h = rpmdbNextIterator(mi)) != NULL) {
	unsigned int recOffset = rpmdbGetIteratorOffset(mi);
 	if (exclrnos &&
	    n_array_bsearch(exclrnos, (void*)recOffset))
	    continue;
	if (header_cap_match_req(h, req, strict)) {
	    rc = 1;
	    break;
	}
    }
    rpmdbFreeIterator(mi);
    
#else	/* !HAVE_RPM_4_0 */
    dbiIndexSet matches;

    matches.count = 0;
    matches.recs = NULL;
    rc = rpmdbFindByProvides(db, capreq_name(req), &matches);

    if (rc != 0) {
        if (rc < 0)
            rpm_die();
        rc = 0;
        
    } else if (rc == 0) {
        Header h;
        int i;
        
        for (i = 0; i < matches.count; i++) {
            if (exclrnos &&
                n_array_bsearch(exclrnos, (void*)matches.recs[i].recOffset))
                continue;

            if ((h = rpmdbGetRecord(db, matches.recs[i].recOffset))) {
                if (header_cap_match_req(h, req, strict)) {
                    rc = 1;
                    headerFree(h);
                    break;
                }
                headerFree(h);
            }
        }
        
        dbiFreeIndexRecord(matches);
    }
#endif	/* !HAVE_RPM_4_0 */

    return rc;
}

#if 0
static
int lookup_req(rpmdb db, const struct capreq *req, int strict,
               tn_array *exclrnos)
{
    dbiIndexSet matches;
    int rc;

    matches.count = 0;
    matches.recs = NULL;
    rc = rpmdbFindByProvides(db, capreq_name(req), &matches);

    if (rc != 0) {
        if (rc < 0)
            rpm_die();
        rc = 0;
        
    } else if (rc == 0) {
        Header h;
        int i;
        
        for (i = 0; i < matches.count; i++) {
            if (exclrnos &&
                n_array_bsearch(exclrnos, (void*)matches.recs[i].recOffset))
                continue;

            if ((h = rpmdbGetRecord(db, matches.recs[i].recOffset))) {
                if (header_cap_match_req(h, req, strict)) {
                    rc = 1;
                    headerFree(h);
                    break;
                }
                headerFree(h);
            }
        }
        
        dbiFreeIndexRecord(matches);
        return rc;
    }

    return 0;
}
#endif

static
int header_evr_match_req(Header h, const struct capreq *req)
{
    struct pkg  pkg;
    uint32_t    *epoch;
    int         rc;
    
    headerNVR(h, (void*)&pkg.name, (void*)&pkg.ver, (void*)&pkg.rel);
    if (pkg.name == NULL || pkg.ver == NULL || pkg.rel == NULL) {
        log(LOGERR, "headerNVR failed\n");
        return 0;
    }

    if (headerGetEntry(h, RPMTAG_EPOCH, &rc, (void *)&epoch, NULL)) 
        pkg.epoch = *epoch;
    else
        pkg.epoch = 0;

    if (pkg_evr_match_req(&pkg, req))
        return 1;

    return 0;
}


static
int header_cap_match_req(Header h, const struct capreq *req, int strict)
{
    void        *saved_allocfn, *saved_freefn;
    struct pkg  pkg;
    int         rc;

    
    rc = 0;
    /* do not alloc on capreq obstack */
    set_capreq_allocfn(malloc, free, &saved_allocfn, &saved_freefn);

    pkg.caps = capreq_arr_new();
    get_pkg_caps(pkg.caps, h);
    
    if (n_array_size(pkg.caps) > 0) {
        n_array_sort(pkg.caps);
        rc = pkg_caps_match_req(&pkg, req, strict);
    }

    n_array_free(pkg.caps);
    set_capreq_allocfn(saved_allocfn, saved_freefn, NULL, NULL);

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
                    tn_array *exclrnos) 
{
    int rc;
    int is_file;

    is_file = (*capreq_name(req) == '/' ? 1:0);

    if (!is_file && lookup_pkg(db, req, exclrnos))
        return 1;
    
    rc = lookup_cap(db, req, strict, exclrnos);
    if (rc)
        return 1;
    
    if (is_file && lookup_file(db, req, exclrnos))
        return 1;
    
    return 0;
}


/*
 * Installation 
 */
static void progress(const unsigned long amount, const unsigned long total) 
{
    static unsigned long prev_v = 0, vv = 0;
    
    if (amount && amount == total) { /* last notification */
        msg(1, "_. (%ld kB)\n", total/1024);

    } else if (amount == 0) {     /* first notification */
        msg(1, "_.");
        vv = 0;
        prev_v = 0;
        
    } else if (total == 0) {     /* impossible */
        assert(0);

    } else {
        unsigned long i;
        unsigned long v = amount * 60 / total;
        for (i=prev_v; i<v; i++)
            msg(1, "_.");
        prev_v = v;
    }
}


static void *install_cb(const Header arg __attribute__((unused)),
                        const rpmCallbackType op, 
                        const unsigned long amount, 
                        const unsigned long total,
                        const void *pkgpath,
                        void *data __attribute__((unused)))
{
#ifdef RPM_V4    
    Header h = arg;
#endif    
    void *rc = NULL;
    
 
    switch (op) {
        case RPMCALLBACK_INST_OPEN_FILE:
        case RPMCALLBACK_INST_CLOSE_FILE:
            n_assert(0);
            break;

        case RPMCALLBACK_INST_START:
            msg(1, "Installing %s\n", n_basenam(pkgpath));
            progress(amount, total);
            break;

        case RPMCALLBACK_INST_PROGRESS:
            progress(amount, total);
            break;

        default:
                                /* do nothing */
    }
    
    return rc;
}	


int rpm_install(rpmdb db, const char *rootdir, const char *path,
                unsigned filterflags, unsigned transflags, unsigned instflags)
{
    rpmTransactionSet rpmts = NULL;
    rpmProblemSet probs = NULL;
    int issrc;
    struct vfile *vf;
    int rc;
    Header h = NULL;


    if (rootdir == NULL)
        rootdir = "";

    if ((vf = vfile_open(path, VFT_RPMIO, VFM_RO)) == NULL)
        return 0;
    
    rc = rpmReadPackageHeader(vf->vf_fdt, &h, &issrc, NULL, NULL);
    
    if (rc != 0) {
        switch (rc) {
            case 1:
                log(LOGERR, "%s: does not appear to be a RPM package\n", path);
                goto l_err;
                break;
                
            default:
                log(LOGERR, "%s: cannot be installed (hgw why)\n", path);
                goto l_err;
                break;
        }
        n_assert(0);
        
    } else {
        if (issrc) {
            log(LOGERR, "%s: pakietów ¼ród³owych nie prowadzimy\n", path);
            goto l_err;
        }
        
        rpmts = rpmtransCreateSet(db, rootdir);
        rc = rpmtransAddPackage(rpmts, h, vf->vf_fdt, path, 
                                (instflags & INSTALL_UPGRADE) != 0, NULL);
        
        headerFree(h);	
        h = NULL;
        
        switch(rc) {
            case 0:
                break;
                
            case 1:
                log(LOGERR, "%s: rpm read error\n", path);
                goto l_err;
                break;
                
                
            case 2:
		log(LOGERR, "%s requires a newer version of RPM\n", path);
                goto l_err;
                break;
                
            default:
                log(LOGERR, "%s: rpmtransAddPackage() failed\n", path);
                goto l_err;
                break;
        }

        if ((instflags & INSTALL_NODEPS) == 0) {
            struct rpmDependencyConflict *conflicts;
            int numConflicts = 0;
            
            if (rpmdepCheck(rpmts, &conflicts, &numConflicts) != 0) {
                log(LOGERR, "%s: rpmdepCheck() failed\n", path);
                goto l_err;
            }
            
                
            if (conflicts) {
                log(LOGERR, "%s: failed dependencies:\n", path);
                printDepProblems(log_stream(), conflicts, numConflicts);
                rpmdepFreeConflicts(conflicts, numConflicts);
                goto l_err;
            }
        }

	rc = rpmRunTransactions(rpmts, install_cb,
                                (void *) ((long)instflags), 
                                NULL, &probs, transflags, filterflags);

        if (rc != 0) {
            if (rc > 0) {
                log(LOGERR, "%s: installation failed:\n", path);
                rpmProblemSetPrint(log_stream(), probs);
                goto l_err;
            } else {
                //log(LOGERR, "%s: installation failed (hgw why)\n", path);
            }
        }
    }

    
    vfile_close(vf);
    if (probs) 
        rpmProblemSetFree(probs);
    rpmtransFree(rpmts);
    return 1;
    
 l_err:
    vfile_close(vf);

    if (probs) 
        rpmProblemSetFree(probs);
    
    if (rpmts)
        rpmtransFree(rpmts);

    if (h)
        headerFree(h);

    return 0;
}


int rpm_dbmap(rpmdb db,
              void (*mapfn)(void *header, off_t offs, void *arg),
              void *arg) 
{
    int n = 0;
    Header h;

#ifdef HAVE_RPM_4_0
    rpmdbMatchIterator mi;

    mi = rpmdbInitIterator(db, RPMDBI_PACKAGES, NULL, 0);
    while ((h = rpmdbNextIterator(mi)) != NULL) {
	unsigned int recOffset = rpmdbGetIteratorOffset(mi);
	mapfn(h, recOffset, arg);
	n++;
    }
    rpmdbFreeIterator(mi);
#else	/* !HAVE_RPM_4_0 */
    int offs;

    offs = rpmdbFirstRecNum(db);
    if (offs == 0)
        return 0;

    if (offs < 0)
        rpm_die();
    
    while (offs > 0) {
        if ((h = rpmdbGetRecord(db, offs))) {
            mapfn(h, offs, arg);
            n++;
            headerFree(h);
        }
        
        offs = rpmdbNextRecNum(db, offs);
    }
#endif	/* !HAVE_RPM_4_0 */
    
    return n;
}


int rpm_dbiterate(rpmdb db, tn_array *offsets,
                  void (*mapfn)(void *header, off_t off, void *arg), void *arg)
{
    int n = 0;
    Header h;

#ifdef HAVE_RPM_4_0	/* XXX should test HAVE_RPM_4_0 (but I'm lazy). */
    rpmdbMatchIterator mi;

    mi = rpmdbInitIterator(db, RPMDBI_PACKAGES, NULL, 0);
    while ((h = rpmdbNextIterator(mi)) != NULL) {
	unsigned int recOffset = rpmdbGetIteratorOffset(mi);
	mapfn(h, recOffset, arg);
	n++;
    }
    rpmdbFreeIterator(mi);
#else	/* !HAVE_RPM_4_0 */
    int i, offs;

    offs = rpmdbFirstRecNum(db);
    if (offs == 0)
        return 0;

    if (offs < 0)
        rpm_die();

    for (i=0; i<n_array_size(offsets); i++) {
        offs = (int)n_array_nth(offsets, i);
        if ((h = rpmdbGetRecord(db, offs))) {
            mapfn(h, offs, arg);
            n++;
            headerFree(h);
        }
    }
#endif	/* !HAVE_RPM_4_0 */
    
    return n;
}

int rpm_get_pkgs_requires_capn(rpmdb db, const char *capname,
                               tn_array *exclrnos, tn_array *pkgs)
{
    int rc = 0;

#ifdef HAVE_RPM_4_0	/* XXX should test HAVE_RPM_4_0 (but I'm lazy). */
    rpmdbMatchIterator mi;
    Header h;

    mi = rpmdbInitIterator(db, RPMTAG_REQUIRENAME, capname, 0);
    while((h = rpmdbNextIterator(mi)) != NULL) {
	unsigned int recOffset = rpmdbGetIteratorOffset(mi);
	struct pkg *pkg;

	if (exclrnos &&
	    n_array_bsearch(exclrnos, (void*)recOffset)) {
	    continue;
	}
        
	n_array_push(exclrnos, (void*)recOffset);
	n_array_sort(exclrnos);
                
	if ((pkg = pkg_ldhdr_udata(h, "db", PKG_LDNEVR | PKG_LDCAPREQS,
		(void*)recOffset, sizeof(recOffset))) == NULL) {
	    rc = -1;
	    break;
	}
        pkg_add_selfcap(pkg);
	n_array_push(pkgs, pkg);
    }
    rpmdbFreeIterator(mi);
#else	/* !HAVE_RPM_4_0 */
    dbiIndexSet matches;

    matches.count = 0;
    matches.recs = NULL;
    rc = rpmdbFindByRequiredBy(db, capname, &matches);

    if (rc < 0) {
        rpm_die();
        
    } else if (rc == 0) {
        Header h;
        int i;
        
        for (i = 0; i < matches.count; i++) {
            register off_t recno = matches.recs[i].recOffset;

            if (exclrnos &&
                n_array_bsearch(exclrnos, (void*)recno)) {
                continue;
            }

            n_array_push(exclrnos, (void*)recno);
            n_array_sort(exclrnos);
            
            if ((h = rpmdbGetRecord(db, recno))) {
                struct pkg *pkg;
                
                if ((pkg = pkg_ldhdr_udata(h, "db", PKG_LDNEVR | PKG_LDCAPREQS,
                                  (void*)recno, sizeof(recno))) == NULL) {
                    rc = -1;
                    break;
                }
                pkg_add_selfcap(pkg);
                n_array_push(pkgs, pkg);
                headerFree(h);
            }
        }
        
        dbiFreeIndexRecord(matches);
    }
#endif	/* !HAVE_RPM_4_0 */
    
    return rc;
}


static 
int get_pkgs_requires_hfiles(rpmdb db, Header h, tn_array *exclrnos,
                             tn_array *pkgs)
{
    int t1, t2, t3, c1, c2, c3;
    char **names = NULL, **dirs = NULL;
    int32_t   *diridxs;
    char      path[PATH_MAX], *prevdir;
    int       *dirlens;
    int       i;
    
    if (!headerGetEntry(h, RPMTAG_BASENAMES, (void*)&t1, (void*)&names, &c1))
        return 0;

    n_assert(t1 == RPM_STRING_ARRAY_TYPE);
    if (!headerGetEntry(h, RPMTAG_DIRNAMES, (void*)&t2, (void*)&dirs, &c2))
        goto l_endfunc;
    
    n_assert(t2 == RPM_STRING_ARRAY_TYPE);
    if (!headerGetEntry(h, RPMTAG_DIRINDEXES, (void*)&t3,(void*)&diridxs, &c3))
        goto l_endfunc;
    n_assert(t3 == RPM_INT32_TYPE);

    n_assert(c1 == c3);

    dirlens = alloca(sizeof(*dirlens) * c2);
    for (i=0; i<c2; i++) 
        dirlens[i] = strlen(dirs[i]);
    
    prevdir = NULL;
    for (i=0; i<c1; i++) {
        register int diri = diridxs[i];
        if (prevdir == dirs[diri]) {
            n_strncpy(&path[dirlens[diri]], names[i], PATH_MAX-dirlens[diri]);
        } else {
            char *endp;
            
            endp = n_strncpy(path, dirs[diri], sizeof(path));
            n_strncpy(endp, names[i], sizeof(path) - (endp - path));
            prevdir = dirs[diri];
        }

        rpm_get_pkgs_requires_capn(db, path, exclrnos, pkgs);
    }
    
 l_endfunc:
    
    if (c1 && names)
        rpm_headerEntryFree(names, t1);

    if (c2 && dirs)
        rpm_headerEntryFree(dirs, t2);

    return 1;
}

/* add to pkgs packages which requires package given in Header */ 
int rpm_get_pkgs_requires_pkgh(rpmdb db, Header h, tn_array *exclrnos,
                               tn_array *pkgs)
{
    tn_array *caps;
    char *pkgname = NULL;
    int i;

    headerGetEntry(h, RPMTAG_NAME, NULL, (void *)&pkgname, NULL);
    n_assert(pkgname);
    rpm_get_pkgs_requires_capn(db, pkgname, exclrnos, pkgs);

    caps = capreq_arr_new();
    get_pkg_caps(caps, h);
    
    for (i=0; i<n_array_size(caps); i++) {
        struct capreq *cap = n_array_nth(caps, i);
        rpm_get_pkgs_requires_capn(db, capreq_name(cap), exclrnos, pkgs);
    }
    
    n_array_free(caps);
    get_pkgs_requires_hfiles(db, h, exclrnos, pkgs);
    return 0;
}


int rpm_get_pkgs_requires_obsl_pkg(rpmdb db, struct capreq *obsl,
                                   tn_array *exclrnos, tn_array *pkgs) 
{
    int n = 0;

#ifdef HAVE_RPM_4_0
    rpmdbMatchIterator mi;
    Header h;

    mi = rpmdbInitIterator(db, RPMTAG_NAME, capreq_name(obsl), 0);
    while((h = rpmdbNextIterator(mi)) != NULL) {
	unsigned int recno = rpmdbGetIteratorOffset(mi);

        if (exclrnos && n_array_bsearch(exclrnos, (void*)recno))
	    continue;
        
	if (header_evr_match_req(h, obsl)) {
	    rpm_get_pkgs_requires_pkgh(db, h, exclrnos, pkgs);
	    n++;
	}
    }
    rpmdbFreeIterator(mi);
    
#else	/* !HAVE_RPM_4_0 */

    dbiIndexSet matches;
    int rc;
    
    matches.count = 0;
    matches.recs = NULL;
    rc = rpmdbFindPackage(db, capreq_name(obsl), &matches);
    if (rc < 0) {
        rpm_die();
        
    } else if (rc == 0) {
        Header h;
        int i;
        
        for (i = 0; i < matches.count; i++) {
            if (exclrnos &&
                n_array_bsearch(exclrnos, (void*)matches.recs[i].recOffset))
                continue;

            if ((h = rpmdbGetRecord(db, matches.recs[i].recOffset))) {
                if (header_evr_match_req(h, obsl)) {
                    rpm_get_pkgs_requires_pkgh(db, h, exclrnos, pkgs);
                    n_array_push(exclrnos, (void*)matches.recs[i].recOffset);
                    n_array_sort(exclrnos);
                    rc = 1;
                    n++;
                    
                }
                headerFree(h);
            }
        }
        
        dbiFreeIndexRecord(matches);
        return n;
    }
#endif	/* !HAVE_RPM_4_0 */
    
    return n;
}


static
int cmp_evr_header2pkg(Header h, const struct pkg *pkg)
{
    struct pkg  tmpkg;
    uint32_t    *epoch;
    int         rc;
    
    headerNVR(h, (void*)&tmpkg.name, (void*)&tmpkg.ver, (void*)&tmpkg.rel);
    if (tmpkg.name == NULL || tmpkg.ver == NULL || tmpkg.rel == NULL) {
        log(LOGERR, "headerNVR failed\n");
        return 0;
    }

    if (headerGetEntry(h, RPMTAG_EPOCH, &rc, (void *)&epoch, NULL)) 
        tmpkg.epoch = *epoch;
    else
        tmpkg.epoch = 0;

    return pkg_cmp_evr(pkg, &tmpkg);
}



int rpm_is_pkg_installed(rpmdb db, const struct pkg *pkg, int *cmprc)
{
    int count = -1;

#ifdef HAVE_RPM_4_0
    rpmdbMatchIterator mi;
    Header h;

    mi = rpmdbInitIterator(db, RPMTAG_NAME, pkg->name, 0);
    count = rpmdbGetIteratorCount(mi);
    
    if (count > 0) {
        h = rpmdbNextIterator(mi);
 	*cmprc = cmp_evr_header2pkg(h, pkg);
    }
    rpmdbFreeIterator(mi);
    
#else /* HAVE_RPM_4_0 */
    dbiIndexSet matches;
    int rc;
    
    matches.count = 0;
    matches.recs = NULL;
    rc = rpmdbFindPackage(db, pkg->name, &matches);

    if (rc < 0) 
        rpm_die();
    
    else if (rc > 0)
        count = 0;

    else if (rc == 0) {
        Header h;

        count = matches.count;
        if (count > 0) {
            if ((h = rpmdbGetRecord(db, matches.recs[0].recOffset))) {
                *cmprc = cmp_evr_header2pkg(h, pkg);
                headerFree(h);
            }
        }
        dbiFreeIndexRecord(matches);
    }
#endif /* HAVE_RPM_4_0 */

    return count;
}
