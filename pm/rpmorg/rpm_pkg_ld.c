/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <netinet/in.h>
#include <trurl/trurl.h>

#ifdef HAVE_RPM_RPMEVR_H
# define _RPMEVR_INTERNAL 1
#endif

#include "i18n.h"
#include "log.h"
#include "capreq.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgfl.h"
#include "depdirs.h"

#include "pm_rpm.h"

struct rpm_cap_tagset {
    int pmtag;
    char *label;
    int name_tag;
    int version_tag;
    int flags_tag;
};

static struct rpm_cap_tagset rpm_cap_tags_tab[] = {
    { PMCAP_REQ,  "req",  RPMTAG_REQUIRENAME, RPMTAG_REQUIREVERSION, RPMTAG_REQUIREFLAGS },
    { PMCAP_CAP,  "cap",  RPMTAG_PROVIDENAME, RPMTAG_PROVIDEVERSION, RPMTAG_PROVIDEFLAGS },
    { PMCAP_CNFL, "cnfl", RPMTAG_CONFLICTNAME, RPMTAG_CONFLICTVERSION, RPMTAG_CONFLICTFLAGS },
    { PMCAP_OBSL, "obsl", RPMTAG_OBSOLETENAME, RPMTAG_OBSOLETEVERSION, RPMTAG_OBSOLETEFLAGS },
#if HAVE_RPMTAG_SUGGESTS
    /*  RPMTAG_SUGGESTS* doesn't work */
    /* { PMCAP_SUG,  "sugg", RPMTAG_SUGGESTSNAME, RPMTAG_SUGGESTSVERSION, RPMTAG_SUGGESTSFLAGS }, */
    { PMCAP_SUG,  "sug",  RPMTAG_REQUIRENAME, RPMTAG_REQUIREVERSION, RPMTAG_REQUIREFLAGS },
#endif
    { 0, 0, 0, 0, 0 },
};

static unsigned setup_reqflags(unsigned rpmflags, unsigned rflags)
{

#if RPMSENSE_PREREQ != RPMSENSE_ANY /* rpm 4.4 drops legacy PreReq support */
    if (isLegacyPreReq(rpmflags)) /* prepared by rpm < 4.0.2  */
        rflags |= CAPREQ_PREREQ | CAPREQ_PREREQ_UN;

    else
#endif
        if (isInstallPreReq(rpmflags))
            rflags |= CAPREQ_PREREQ;

    if (isErasePreReq(rpmflags))
        rflags |= CAPREQ_PREREQ_UN;

    DBGFIF(rflags & (CAPREQ_PREREQ | CAPREQ_PREREQ_UN),
           "(%s, %s)\n",
           rflags & CAPREQ_PREREQ ? "pre":"",
           rflags & CAPREQ_PREREQ_UN ? "postun":"");

    return rflags;
}

static int is_suggestion(unsigned flag)
{
#if HAVE_RPMTAG_SUGGESTS        /* skipping suggests */
    return (flag & RPMSENSE_MISSINGOK);
#endif
    return 0;
}


static
tn_array *load_capreqs(tn_alloc *na, tn_array *arr, const Header h, struct pkg *pkg, int pmcap_tag)
{
    struct rpm_cap_tagset *tgs = NULL;
    struct capreq *cr;
    struct rpmhdr_ent e_name, e_version, e_flag;
    char **names = NULL, **versions = NULL;
    uint32_t *flags = NULL;
    int  i, rc = 0, ownedarr = 0;

    if (arr == NULL) {
        arr = capreq_arr_new(0);
        ownedarr = 1;
    }

    i = 0;
    while (rpm_cap_tags_tab[i].pmtag > 0) {
        if (rpm_cap_tags_tab[i].pmtag == pmcap_tag) {
            tgs = &rpm_cap_tags_tab[i];
            break;
        }
        i++;
    }

    if (tgs == NULL) {
        if (pmcap_tag == PMCAP_SUG)
            return NULL;

        n_die("%d: unknown captag (internal error)", pmcap_tag);
    }


    if (!pm_rpmhdr_ent_get(&e_name, h, tgs->name_tag))
        return NULL;

    //printf("ldcap %s\n", e_name.val);

    if (pm_rpmhdr_ent_get(&e_version, h, tgs->version_tag)) {
        //n_assert(t2 == RPM_STRING_ARRAY_TYPE);
        //n_assert(versions);
        //n_assert(c2);

    } else if (pmcap_tag == PMCAP_REQ) { /* reqs should have version tag */
        pm_rpmhdr_ent_free(&e_name);
        return 0;
    }

    if (pm_rpmhdr_ent_get(&e_flag, h, tgs->flags_tag)) {
        //n_assert(t3 == RPM_INT32_TYPE);
        //n_assert(flags);
        //n_assert(c3);

    } else if (pmcap_tag == PMCAP_REQ) {  /* reqs should have version too */
        pm_rpmhdr_ent_free(&e_name);
        pm_rpmhdr_ent_free(&e_version);
        return 0;
    }

    if (e_flag.cnt && (e_name.cnt != e_version.cnt)) {
        logn(LOGERR, "read %s: nnames (%d) != nversions (%d), broken rpm",
             tgs->label, e_name.cnt, e_version.cnt);
#if 0
        for (i=0; i<c1; i++)
            printf("n %s\n", names[i]);
        for (i=0; i<c2; i++)
            printf("v %s\n", versions[i]);
#endif
        goto l_end;
    }

    if (e_version.cnt != e_flag.cnt) {
        logn(LOGERR, "read %s: nversions %d != nflags %d, broken rpm",
             tgs->label, e_version.cnt, e_flag.cnt);
        goto l_end;
    }

    names = pm_rpmhdr_ent_as_strarr(&e_name);
    versions = pm_rpmhdr_ent_as_strarr(&e_version);
    flags = pm_rpmhdr_ent_as_intarr(&e_flag);

    for (i=0; i < e_name.cnt; i++) {
        char *name, *evr = NULL;
        unsigned cr_relflags = 0, cr_flags = 0;

        name = names[i];

        if (e_version.cnt && *versions[i])
            evr = versions[i];

        if (e_flag.cnt) {               /* translate flags to poldek one */
            register uint32_t flag = flags[i];

            if (flag & RPMSENSE_LESS)
                cr_relflags |= REL_LT;

            if (flag & RPMSENSE_GREATER)
                cr_relflags |= REL_GT;

            if (flag & RPMSENSE_EQUAL)
                cr_relflags |= REL_EQ;

            if (pmcap_tag == PMCAP_REQ) {
                if (is_suggestion(flag))
                    continue;
                cr_flags = setup_reqflags(flag, cr_flags);
            }

            if (pmcap_tag == PMCAP_SUG && !is_suggestion(flag))
                continue;
        }

        if (pmcap_tag == PMCAP_OBSL)
            cr_flags |= CAPREQ_OBCNFL;

        if ((cr = capreq_new_evr(na, name, evr, cr_relflags, cr_flags)) == NULL) {
            logn(LOGERR, "%s: '%s %s%s%s %s': invalid capability",
                 pkg ? pkg_id(pkg) : "(null)", name,
                 (cr_relflags & REL_LT) ? "<" : "",
                 (cr_relflags & REL_GT) ? ">" : "",
                 (cr_relflags & REL_EQ) ? "=":"", evr);
            goto l_end;

        } else {
            msg(5, "%s%s: %s\n",
                cr->cr_flags & CAPREQ_PREREQ ?
                (pmcap_tag == PMCAP_OBSL ? "obsl" : "pre" ):"",
                tgs->label, capreq_snprintf_s(cr));
            n_array_push(arr, cr);
        }
    }
    rc = 1;                     /* OK */

l_end:
    if (rc) {
        if (ownedarr && n_array_size(arr) == 0)
            n_array_cfree(&arr);

    } else if (ownedarr) {      /* error */
        n_array_cfree(&arr);
    }

    pm_rpmhdr_ent_free(&e_name);
    pm_rpmhdr_ent_free(&e_version);
    pm_rpmhdr_ent_free(&e_flag);

    return arr;
}


tn_array *pm_rpm_ldhdr_capreqs(tn_array *arr, const Header h, int crtype)
{
    return load_capreqs(NULL, arr, h, NULL, crtype);
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
	if (poldek_VERBOSE > 1)
    	    logn(LOGWARN, _("%s: skipped %s \"%s\" longer than 255 bytes"),
        	pkgname, S_ISDIR(mode) ? _("dirname") : _("filename"), fname);
        return 0;
    }

    return 1;
}

/* rpmtd shortcuts */
static int hget(rpmtd td, Header h, int32_t tag) {
    return headerGet(h, tag, td, HEADERGET_MINMEM);
}

static uint16_t *uint16_at(rpmtd td, int index)
{
    int i = rpmtdSetIndex(td, index);
    n_assert(i >= 0);            /* ret -1 on error */
    return rpmtdGetUint16(td);
}

static uint32_t *uint32_at(rpmtd td, int index)
{
    int i = rpmtdSetIndex(td, index);
    n_assert(i >= 0);            /* ret -1 on error */
    return rpmtdGetUint32(td);
}

static char *str_at(rpmtd td, int index)
{
    int i = rpmtdSetIndex(td, index);
    //printf("str_at %d is %d", index, i);
    n_assert(i >= 0);            /* ret -1 on error */
    return rpmtdGetString(td);
}

/* -1 on error  */
int pm_rpm_ldhdr_fl(tn_alloc *na, tn_tuple **fl,
                    Header h, int which, const char *pkgname)
{
    char      **skipdirs;
    size_t    *dirslen = NULL;
    struct    flfile *flfile;
    struct    pkgfl_ent **fentdirs = NULL;
    int       *fentdirs_items;
    unsigned  i;
    int       j, ndirs = 0, nerr = 0, missing_file_hdrs_err = 0;
    const char *errmsg_notag = _("%s: no %s tag");
    struct rpmtd_s  names, dirs, diridxs, modes, sizes, symlinks;

    n_assert(na);

    if (!hget(&names, h, RPMTAG_BASENAMES))
        return NULL;

    if (!hget(&dirs, h, RPMTAG_DIRNAMES))
        goto l_endfunc;

    if (!hget(&diridxs, h, RPMTAG_DIRINDEXES)) {
        logn(LOGERR, errmsg_notag, pkgname, "DIRINDEXES");
        nerr++;
        goto l_endfunc;
    }

    if (names.count != diridxs.count) {
        logn(LOGERR, "%s: size of DIRINDEXES (%d) != size of BASENAMES (%d)",
             pkgname, diridxs.count, names.count);
        nerr++;
        goto l_endfunc;
    }

    if (!hget(&modes, h, RPMTAG_FILEMODES)) {
        if (poldek_VERBOSE > 1)
            logn(LOGWARN, errmsg_notag, pkgname, "FILEMODES");
        missing_file_hdrs_err = 1;
    }

    if (!hget(&sizes, h, RPMTAG_FILESIZES)) {
        if (poldek_VERBOSE > 1)
            logn(LOGWARN, errmsg_notag, pkgname, "FILESIZES");
        missing_file_hdrs_err = 2;
    }

    hget(&symlinks, h, RPMTAG_FILELINKTOS);

    skipdirs = alloca(sizeof(*skipdirs) * dirs.count);
    fentdirs = alloca(sizeof(*fentdirs) * dirs.count);
    dirslen = alloca(sizeof(size_t) * dirs.count);
    fentdirs_items = alloca(sizeof(*fentdirs_items) * dirs.count);

    /* skip unneded dirnames */
    for (i=0; i < rpmtdCount(&dirs); i++) {
        struct pkgfl_ent *flent;
        char *dir = str_at(&dirs, i);

        dirslen[i] = strlen(dir);

        fentdirs_items[i] = 0;
        if (!valid_fname(dir, 0, pkgname)) {
    	    skipdirs[i] = NULL;
    	    fentdirs[i] = NULL;
            continue;
        }

        if (which != PKGFL_ALL) {
            int is_depdir;

            is_depdir = in_depdirs(dir + 1);

            if (!is_depdir && which == PKGFL_DEPDIRS) {
                msg(5, "skip files in dir %s\n", dir);
                skipdirs[i] = NULL;
                fentdirs[i] = NULL;
                continue;

            } else if (is_depdir && which == PKGFL_NOTDEPDIRS) {
                msg(5, "skip files in dir %s\n", dir);
                skipdirs[i] = NULL;
                fentdirs[i] = NULL;
                continue;
            }
        }

        skipdirs[i] = dir;
        for (j=0; j < rpmtdCount(&names); j++)
            if (*uint32_at(&diridxs, j) == i)
                fentdirs_items[i]++;

        flent = pkgfl_ent_new(na, dir, dirslen[i], fentdirs_items[i]);
        fentdirs[i] = flent;
        ndirs++;
    }

    msg(4, "%d files in package\n", rpmtdCount(&names));
    for (i=0; i < rpmtdCount(&names); i++) {
        struct pkgfl_ent *flent;
        uint32_t *jp = uint32_at(&diridxs, i);
        char *name = str_at(&names, i);
        uint16_t *mode = uint16_at(&modes, i);
        n_assert(jp != NULL);
        int j = *jp;

        if (!valid_fname(name, mode ? *mode : 0, pkgname))
            continue;

        msg(5, "  %d: %s %s/%s \n", i, skipdirs[j] ? "add " : "skip", str_at(&dirs, j), name);

        if (skipdirs[j] == NULL)
            continue;

        flent = fentdirs[j];
        int len = strlen(name);

        /* FIXME: ignore dirpaths longer then 255 characters lp#1288989 */
        if (S_ISDIR(mode ? *mode : 0) && dirslen[j] + len > 254) {
    	    if (poldek_VERBOSE > 1)
    		logn(LOGWARN, _("%s: skipped dirname \"%s/%s\": longer than 255 bytes"),
        	    pkgname, flent->dirname, name);
    	    continue;
    	}

        char *symlink = str_at(&symlinks, i);
        uint32_t *size = uint32_at(&sizes, i);

        flfile = flfile_new(na, size ? *size : 0,
                            mode ? *mode : 0,
                            name, len,
                            symlink,
                            symlink ? strlen(symlink) : 0);

        flent->files[flent->items++] = flfile;
        n_assert(flent->items <= fentdirs_items[j]);
    }

 l_endfunc:
    if (nerr) {
        logn(LOGERR, _("%s: skipped file list"), pkgname);

    } else if (ndirs) {
        int n = 0;
        for (i=0; i < rpmtdCount(&dirs); i++) {
            if (fentdirs[i] != NULL)
                n++;
        }

        if (n > 0) {
            tn_tuple *t;
            int j = 0;

            t = n_tuple_new(na, n, NULL);
            for (i=0; i < rpmtdCount(&dirs); i++) {
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

    rpmtdFreeData(&names);
    rpmtdFreeData(&dirs);
    rpmtdFreeData(&diridxs);
    rpmtdFreeData(&modes);
    rpmtdFreeData(&sizes);
    rpmtdFreeData(&symlinks);

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
    uint32_t   epoch, size, btime, itime;
    uint32_t   *psize = NULL, *pbtime = NULL, *pitime = NULL;
    const char *name = NULL, *version = NULL, *release = NULL, *arch = NULL;
    char       osbuf[128], *os = osbuf;
    char       srcrpmbuf[128], *srcrpm = srcrpmbuf;

    pm_rpmhdr_nevr(h, &name, &epoch, &version, &release, &arch, NULL);

    if (name == NULL || version == NULL || release == NULL) {
        logn(LOGERR, _("%s: read name/version/release failed"), fname);
        return NULL;
    }

    if (pm_rpmhdr_issource(h))
        arch = "src";

    if (!pm_rpmhdr_get_string(h, RPMTAG_OS, osbuf, sizeof(osbuf))) {
        os = NULL;
        if (poldek_VERBOSE > 1)
            logn(LOGWARN, _("%s: missing OS tag"), fname);
    }

    if (pm_rpmhdr_get_int(h, RPMTAG_SIZE, &size))
        psize = &size;

    if (pm_rpmhdr_get_int(h, RPMTAG_BUILDTIME, &btime))
        pbtime = &btime;

    if (pm_rpmhdr_get_int(h, RPMTAG_INSTALLTIME, &itime))
        pitime = &itime;

    if (!pm_rpmhdr_get_string(h, RPMTAG_SOURCERPM, srcrpm, sizeof(srcrpmbuf)))
        srcrpm = NULL;

    pkg = pkg_new_ext(na, name, epoch ? epoch : 0, version, release, arch, os,
                      fname, srcrpm, psize ? *psize : 0, fsize,
                      pbtime ? *pbtime : 0);

    if (pkg == NULL)
        return NULL;

    if (pitime)
        pkg->itime = *pitime;

    pkg->color = headerGetNumber(h, RPMTAG_HEADERCOLOR);

    msg(4, "ld %s\n", pkg_id(pkg));

    if (ldflags & PKG_LDCAPS)
        pkg->caps = load_capreqs(na, NULL, h, pkg, PMCAP_CAP);

    if (ldflags & PKG_LDREQS) {
        pkg->reqs = load_capreqs(na, NULL, h, pkg, PMCAP_REQ);
        pkg->sugs = load_capreqs(na, NULL, h, pkg, PMCAP_SUG);
    }

    if (ldflags & PKG_LDCNFLS) {
        pkg->cnfls = capreq_arr_new(0);
        load_capreqs(na, pkg->cnfls, h, pkg, PMCAP_CNFL);
        load_capreqs(na, pkg->cnfls, h, pkg, PMCAP_OBSL);

        if (n_array_size(pkg->cnfls) > 0) {
            n_array_sort(pkg->cnfls);

        } else {
            n_array_free(pkg->cnfls);
            pkg->cnfls = NULL;
        };
    }

    if (ldflags & (PKG_LDFL_DEPDIRS | PKG_LDFL_WHOLE)) {
        unsigned fl_ldflags = 0;
        if (ldflags & PKG_LDFL_WHOLE)
            fl_ldflags = PKGFL_ALL;
        else
            fl_ldflags = PKGFL_DEPDIRS;

        if (pm_rpm_ldhdr_fl(na, &pkg->fl, h, fl_ldflags, pkg_id(pkg)) == -1) {
            pkg_free(pkg);
            pkg = NULL;

        } else if (pkg->fl && n_tuple_size(pkg->fl) > 0) {
            n_tuple_sort_ex(pkg->fl, (tn_fn_cmp)pkgfl_ent_cmp);
        }

        if (pkg->fl == NULL)
            exit(1);
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
