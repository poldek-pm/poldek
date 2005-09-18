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

#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>

#include <trurl/trurl.h>

#include "i18n.h"
#include "log.h"
#include "capreq.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgfl.h"
#include "depdirs.h"

#include "pm_rpm.h"

static
tn_array *do_ldhdr_capreqs(tn_array *arr, const Header h, struct pkg *pkg,
                           int crtype);

#define get_pkg_caps(arr, h, p)   do_ldhdr_capreqs(arr, h, p, PMCAP_CAP)
#define get_pkg_reqs(arr, h, p)   do_ldhdr_capreqs(arr, h, p, PMCAP_REQ)
#define get_pkg_cnfls(arr, h, p)  do_ldhdr_capreqs(arr, h, p, PMCAP_CNFL)
#define get_pkg_obsls(arr, h, p)  do_ldhdr_capreqs (arr, h, p, PMCAP_OBSL)

static
tn_array *do_ldhdr_capreqs(tn_array *arr, const Header h, struct pkg *pkg,
                           int crtype) 
{
    struct capreq *cr;
    int t1, t2, t3, c1 = 0, c2 = 0, c3 = 0;
    char **names, **versions, *label;
    int  *flags, *tags;
    int  i;
    

    int req_tags[3] = {
        RPMTAG_REQUIRENAME, RPMTAG_REQUIREVERSION, RPMTAG_REQUIREFLAGS
    };

    int prov_tags[3] = {
        RPMTAG_PROVIDENAME, RPMTAG_PROVIDEVERSION, RPMTAG_PROVIDEFLAGS
    };

    int cnfl_tags[3] = {
        RPMTAG_CONFLICTNAME, RPMTAG_CONFLICTVERSION, RPMTAG_CONFLICTFLAGS
    };

    int obsl_tags[3] = {
        RPMTAG_OBSOLETENAME, RPMTAG_OBSOLETEVERSION, RPMTAG_OBSOLETEFLAGS
    };
    
    n_assert(arr);
    
    switch (crtype) {
        case PMCAP_CAP:
            tags = prov_tags;
            label = "prov";
            break;
            
        case PMCAP_REQ:
            tags = req_tags;
            label = "req";
            break;
            
        case PMCAP_CNFL:
            tags = cnfl_tags;
            label = "cnfl";
            break;

        case PMCAP_OBSL:
            tags = obsl_tags;
            label = "cnfl";
            break;
            
        default:
            tags = NULL;
            label = NULL;
            n_die("%d: unknown type (internal error)", crtype);
    }

    names = NULL;
    if (!headerGetEntry(h, *tags, (void*)&t1, (void*)&names, &c1))
        return NULL;
    
    n_assert(names);

    
    tags++;
    versions = NULL;
    if (headerGetEntry(h, *tags, (void*)&t2, (void*)&versions, &c2)) {
        n_assert(t2 == RPM_STRING_ARRAY_TYPE);
        n_assert(versions);
        n_assert(c2);
        
    } else if (crtype == PMCAP_REQ) { /* reqs should have version tag */
        pm_rpmhdr_free_entry(names, t1);
        return 0;
    }
    
    
    tags++;
    flags = NULL;
    if (headerGetEntry(h, *tags, (void*)&t3, (void*)&flags, &c3)) {
        n_assert(t3 == RPM_INT32_TYPE);
        n_assert(flags);
        n_assert(c3);
        
    } else if (crtype == PMCAP_REQ) {  /* reqs should have flags */
        pm_rpmhdr_free_entry(names, t1);
        pm_rpmhdr_free_entry(versions, t2);
        return 0;
    }

    if (c2) 
        if (c1 != c2) {
            logn(LOGERR, "read %s: nnames (%d) != nversions (%d), broken rpm",
                 label, c1, c2);
#if 0
            for (i=0; i<c1; i++) 
                printf("n %s\n", names[i]);
            for (i=0; i<c2; i++) 
                printf("v %s\n", versions[i]);
#endif            
            goto l_err_endfunc;
        }
        
    if (c2 != c3) {
        logn(LOGERR, "read %s: nversions %d != nflags %d, broken rpm", label,
            c2, c3);
        goto l_err_endfunc;
    }

    for (i=0; i < c1 ; i++) {
        char *name, *evr = NULL;
        unsigned cr_relflags = 0, cr_flags = 0;
            
        name = names[i];
        if (c2 && *versions[i])
            evr = versions[i];
        
        
        if (c3) {               /* translate flags to poldek one */
            register uint32_t flag = flags[i];

            if (flag & RPMSENSE_LESS) 
                cr_relflags |= REL_LT;
            
            if (flag & RPMSENSE_GREATER) 
                cr_relflags |= REL_GT;
            
            if (flag & RPMSENSE_EQUAL) 
                cr_relflags |= REL_EQ;
            
            if (crtype == PMCAP_REQ) {
#ifndef HAVE_RPM_EXTDEPS
                if (flag & RPMSENSE_PREREQ) {
                    n_assert(crtype == PMCAP_REQ);
                    cr_flags |= CAPREQ_PREREQ | CAPREQ_PREREQ_UN;
                }
#else
                
                if (isLegacyPreReq(flag)) /* prepared by rpm < 4.0.2  */
                    cr_flags |= CAPREQ_PREREQ | CAPREQ_PREREQ_UN;
                    
                else if (isInstallPreReq(flag))
                    cr_flags |= CAPREQ_PREREQ;

                if (isErasePreReq(flag))
                    cr_flags |= CAPREQ_PREREQ_UN;
                
                DBGFIF(cr_flags & (CAPREQ_PREREQ | CAPREQ_PREREQ_UN),
                       "%s (%s, %s)\n", name,
                       cr_flags & CAPREQ_PREREQ ? "pre":"",
                       cr_flags & CAPREQ_PREREQ_UN ? "postun":"");
#endif /* HAVE_RPM_EXTDEPS */                
            }
        }

        if (crtype == PMCAP_OBSL) 
            cr_flags |= CAPREQ_OBCNFL;

        if ((cr = capreq_new_evr(name, evr, cr_relflags, cr_flags)) == NULL) {
            logn(LOGERR, "%s: '%s %s%s%s %s': invalid capability",
                 pkg ? pkg_snprintf_s(pkg) : "(null)", name, 
                 (cr_relflags & REL_LT) ? "<" : "",
                 (cr_relflags & REL_GT) ? ">" : "",
                 (cr_relflags & REL_EQ) ? "=":"", evr);
            goto l_err_endfunc;
            
        } else {
            msg(4, "%s%s: %s\n",
                cr->cr_flags & CAPREQ_PREREQ ?
                (crtype == PMCAP_OBSL ? "obsl" : "pre" ):"", 
                label, capreq_snprintf_s(cr));
            n_array_push(arr, cr);
        }
    }
    
    pm_rpmhdr_free_entry(names, t1);
    pm_rpmhdr_free_entry(versions, t2);
    pm_rpmhdr_free_entry(flags, t3);

    return arr;
    
 l_err_endfunc:
    pm_rpmhdr_free_entry(names, t1);
    pm_rpmhdr_free_entry(versions, t2);
    pm_rpmhdr_free_entry(flags, t3);
    return NULL;
}

tn_array *pm_rpm_ldhdr_capreqs(tn_array *arr, const Header h, int crtype) 
{
    return do_ldhdr_capreqs(arr, h, NULL, crtype);
}


__inline__ 
static int valid_fname(const char *fname, mode_t mode, const char *pkgname) 
{
    
#if 0  /* too many bad habits :-> */
    char *denychars = "\r\n\t |;";
    if (strpbrk(fname, denychars)) {
        logn(LOGINFO, "%s: bad habit: %s \"%s\" with whitespaces",
            pkgname, S_ISDIR(mode) ? "dirname" : "filename", fname);
    }
#endif     

    if (strlen(fname) > 255) {
        logn(LOGERR, _("%s: %s \"%s\" longer than 255 bytes"),
            pkgname, S_ISDIR(mode) ? _("dirname") : _("filename"), fname);
        return 0;
    }
    
    return 1;
}

/* -1 on error  */
int pm_rpm_ldhdr_fl(tn_alloc *na, tn_tuple **fl,
                    Header h, int which, const char *pkgname)
{
    int t1, t2, t3, t4, c1, c2, c3, c4;
    char **names = NULL, **dirs = NULL, **symlinks = NULL, **skipdirs;
    int32_t   *diridxs;
    uint32_t  *sizes;
    uint16_t  *modes;
    struct    flfile *flfile;
    struct    pkgfl_ent **fentdirs = NULL;
    int       *fentdirs_items;
    int       i, j, ndirs = 0, nerr = 0, missing_file_hdrs_err = 0;
    const char *errmsg_notag = _("%s: no %s tag");

    n_assert(na);
    if (!headerGetEntry(h, RPMTAG_BASENAMES, (void*)&t1, (void*)&names, &c1))
        return 0;

    n_assert(t1 == RPM_STRING_ARRAY_TYPE);
    if (!headerGetEntry(h, RPMTAG_DIRNAMES, (void*)&t2, (void*)&dirs, &c2))
        goto l_endfunc;
    
    n_assert(t2 == RPM_STRING_ARRAY_TYPE);
    if (!headerGetEntry(h, RPMTAG_DIRINDEXES, (void*)&t3,(void*)&diridxs, &c3))
    {
        logn(LOGERR, errmsg_notag, pkgname, "DIRINDEXES");
        nerr++;
        goto l_endfunc;
    }

    n_assert(t3 == RPM_INT32_TYPE);
    
    if (c1 != c3) {
        logn(LOGERR, "%s: size of DIRINDEXES (%d) != size of BASENAMES (%d)",
             pkgname, c3, c1);
        nerr++;
        goto l_endfunc;
    }
    
    if (!headerGetEntry(h, RPMTAG_FILEMODES, (void*)&t4, (void*)&modes, &c4)) {
        if (poldek_VERBOSE > 1)
            logn(LOGWARN, errmsg_notag, pkgname, "FILEMODES");
        missing_file_hdrs_err = 1;
        modes = NULL;
    }
    
    if (!headerGetEntry(h, RPMTAG_FILESIZES, (void*)&t4, (void*)&sizes, &c4)) {
        if (poldek_VERBOSE > 1)
            logn(LOGWARN, errmsg_notag, pkgname, "FILESIZES");
        missing_file_hdrs_err = 2;
        sizes = NULL;
    }
    
    if (!headerGetEntry(h, RPMTAG_FILELINKTOS, (void*)&t4, (void*)&symlinks,
                        &c4)) {
        symlinks = NULL;
    }
    
    skipdirs = alloca(sizeof(*skipdirs) * c2);
    fentdirs = alloca(sizeof(*fentdirs) * c2);
    fentdirs_items = alloca(sizeof(*fentdirs_items) * c2);

    /* skip unneded dirnames */
    for (i=0; i<c2; i++) {
        struct pkgfl_ent *flent;

        fentdirs_items[i] = 0;
        if (!valid_fname(dirs[i], 0, pkgname))
            nerr++;

        if (which != PKGFL_ALL) {
            int is_depdir;

            is_depdir = in_depdirs(dirs[i] + 1);
            
            if (!is_depdir && which == PKGFL_DEPDIRS) {
                msg(5, "skip files in dir %s\n", dirs[i]);
                skipdirs[i] = NULL;
                fentdirs[i] = NULL;
                continue;
                
            } else if (is_depdir && which == PKGFL_NOTDEPDIRS) {
                msg(5, "skip files in dir %s\n", dirs[i]);
                skipdirs[i] = NULL;
                fentdirs[i] = NULL;
                continue;
            }
        }

        skipdirs[i] = dirs[i];
        for (j=0; j<c1; j++)
            if (diridxs[j] == i)
                fentdirs_items[i]++;
        
        flent = pkgfl_ent_new(na, dirs[i], strlen(dirs[i]), fentdirs_items[i]);
        fentdirs[i] = flent;
        ndirs++;
    }
    
    msg(4, "%d files in package\n", c1);
    for (i=0; i<c1; i++) {
        struct pkgfl_ent *flent;
        register int j = diridxs[i];
        int len;

        if (!valid_fname(names[i], modes ? modes[i] : 0, pkgname))
            nerr++;
        
        msg(5, "  %d: %s %s/%s \n", i, skipdirs[j] ? "add " : "skip",
            dirs[j], names[i]);
            
        if (skipdirs[j] == NULL)
            continue;
        
        flent = fentdirs[j];
        len = strlen(names[i]);
        if (symlinks) { 
            flfile = flfile_new(na, sizes ? sizes[i] : 0,
                                modes ? modes[i] : 0,
                                names[i], len,
                                symlinks[i],
                                strlen(symlinks[i]));
        } else {
            flfile = flfile_new(na, sizes ? sizes[i] : 0,
                                modes ? modes[i] : 0,
                                names[i], len,
                                NULL,
                                0);
            
        }
        
        flent->files[flent->items++] = flfile;
        n_assert(flent->items <= fentdirs_items[j]);
    }
    
 l_endfunc:
    
    if (c1 && names)
        pm_rpmhdr_free_entry(names, t1);

    if (c2 && dirs)
        pm_rpmhdr_free_entry(dirs, t2);

    if (c4 && symlinks)
        pm_rpmhdr_free_entry(symlinks, t4);
    
    if (nerr) {
        logn(LOGERR, _("%s: skipped file list"), pkgname);
        
    } else if (ndirs) {
        int n = 0;
        for (i=0; i<c2; i++) 
            if (fentdirs[i] != NULL)
                n++;
        
        if (n > 0) {
            tn_tuple *t;
            int j = 0;
            
            t = n_tuple_new(na, n, NULL);
            for (i=0; i<c2; i++) {
                if (fentdirs[i]  == NULL)
                    continue;
                
                n_tuple_set_nth(t, j++, fentdirs[i]);
                qsort(&fentdirs[i]->files, fentdirs[i]->items,
                      sizeof(struct flfile*), 
                      (int (*)(const void *, const void *))flfile_cmp_qsort);
            }
            *fl = t;
        }
    }
#if 0
    if (missing_file_hdrs_err) {
        char *missing = _("FILEMODES tag");
        if (missing_file_hdrs_err > 1)
            missing = _("FILEMODES and FILESIZES tags");
        logn(LOGERR, _("%s: missing %s in some packages"), pkgname, missing);
    }
#endif    
    return nerr ? -1 : 1;
}

struct pkg *pm_rpm_ldhdr(tn_alloc *na, Header h, const char *fname, unsigned fsize,
                         unsigned ldflags)
{
    struct pkg *pkg;
    uint32_t   *epoch, *size, *btime, *itime;
    char       *name, *version, *release, *arch = NULL, *os = NULL;
    int        type;
    
    headerNVR(h, (void*)&name, (void*)&version, (void*)&release);
    if (name == NULL || version == NULL || release == NULL) {
        logn(LOGERR, _("%s: read name/version/release failed"), fname);
        return NULL;
    }
    
    if (!headerGetEntry(h, RPMTAG_EPOCH, &type, (void *)&epoch, NULL)) 
        epoch = NULL;

    if (pm_rpmhdr_issource(h)) {
        arch = "src";
        
    } else {
        if (!headerGetEntry(h, RPMTAG_ARCH, &type, (void *)&arch, NULL)) {
            logn(LOGERR, _("%s: read architecture tag failed"), fname);
            return NULL;
        }

        if (type != RPM_STRING_TYPE)
            arch = NULL;
    }
    
    
    if (!headerGetEntry(h, RPMTAG_OS, &type, (void *)&os, NULL)) {
        if (poldek_VERBOSE > 1)
            logn(LOGWARN, _("%s: missing OS tag"), fname);
        os = NULL;
            
    } else if (type != RPM_STRING_TYPE)
        os = NULL;

    if (!headerGetEntry(h, RPMTAG_SIZE, &type, (void *)&size, NULL)) 
        size = NULL;

    if (!headerGetEntry(h, RPMTAG_BUILDTIME, &type, (void *)&btime, NULL)) 
        btime = NULL;

    if (!headerGetEntry(h, RPMTAG_INSTALLTIME, &type, (void *)&itime, NULL)) 
        itime = NULL;
    
    pkg = pkg_new_ext(na, name, epoch ? *epoch : 0, version, release, arch, os,
                      fname, size ? *size : 0, fsize, btime ? *btime : 0);
    
    if (pkg == NULL)
        return NULL;
    
    if (itime) 
        pkg->itime = *itime;
        
    msg(4, "ld %s\n", pkg_snprintf_s(pkg));
    
    if (ldflags & PKG_LDCAPS) {
        pkg->caps = capreq_arr_new(0);
        get_pkg_caps(pkg->caps, h, pkg);
    
        if (n_array_size(pkg->caps)) 
            n_array_sort(pkg->caps);
        else {
            n_array_free(pkg->caps);
            pkg->caps = NULL;
        }
    }
    
    if (ldflags & PKG_LDREQS) {
        pkg->reqs = capreq_arr_new(0);
        get_pkg_reqs(pkg->reqs, h, pkg);

        if (n_array_size(pkg->reqs) == 0) {
            n_array_free(pkg->reqs);
            pkg->reqs = NULL;
        }
    }

    if (ldflags & PKG_LDCNFLS) {
        pkg->cnfls = capreq_arr_new(0);
        get_pkg_cnfls(pkg->cnfls, h, pkg);
        get_pkg_obsls(pkg->cnfls, h, pkg);
        
        if (n_array_size(pkg->cnfls) > 0) {
            n_array_sort(pkg->cnfls);
        
        } else {
            n_array_free(pkg->cnfls);
            pkg->cnfls = NULL;
        };
    }

    if (ldflags & (PKG_LDFL_DEPDIRS | PKG_LDFL_WHOLE)) {
        unsigned flldflags = 0;
        if (ldflags & PKG_LDFL_WHOLE)
            flldflags = PKGFL_ALL;
        else
            flldflags = PKGFL_DEPDIRS;
        
        if (pm_rpm_ldhdr_fl(na, &pkg->fl, h, flldflags,
                            pkg_snprintf_s(pkg)) == -1) {
            pkg_free(pkg);
            pkg = NULL;
        
        } else if (pkg->fl && n_tuple_size(pkg->fl) > 0) {
            n_tuple_sort_ex(pkg->fl, (tn_fn_cmp)pkgfl_ent_cmp);
        }
    }
    
    return pkg;
}


struct pkg *pm_rpm_ldpkg(void *pm_rpm,
                         tn_alloc *na, const char *path, unsigned ldflags)
{
    struct pkg *pkg = NULL;
    Header h;

    pm_rpm = pm_rpm;
    if (pm_rpmhdr_loadfile(path, &h)) {
        pkg = pm_rpm_ldhdr(na, h, path, 0, ldflags);
        headerFree(h);
    }

    return pkg;
}
