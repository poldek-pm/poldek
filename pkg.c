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

#include <trurl/nstr.h>
#include <trurl/nassert.h>

#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "capreq.h"
#include "pkgfl.h"
#include "pkgu.h"
#include "pkg.h"
#include "pkgdir/pkgdir.h"
#include "pkgroup.h"
#include "pkg_ver_cmp.h"

static tn_hash *architecture_h = NULL;
static tn_hash *operatingsystem_h = NULL; 

static void *(*pkg_alloc_fn)(size_t) = n_malloc;
static void (*pkg_free_fn)(void*) = n_free;

#if 0                           /* disabled cause gives not reasonable profit */
void set_pkg_allocfn(void *(*pkg_allocfn)(size_t), void (*pkg_freefn)(void*))
{
    pkg_alloc_fn = pkg_allocfn;
    pkg_free_fn = pkg_freefn;
}
#endif

static inline const char *register_os(const char *os)
{
    const char *oss;
    
    if (operatingsystem_h == NULL) {
        operatingsystem_h = n_hash_new(21, free);
        n_hash_ctl(operatingsystem_h, TN_HASH_NOCPKEY);
    }
    
    if ((oss = n_hash_get(operatingsystem_h, os)) == NULL) {
        oss = n_strdup(os);
        n_hash_insert(operatingsystem_h, oss, oss);
    }

    return oss;
}

static inline const char *register_arch(const char *arch)
{
    const char *aarch;
    
    if (architecture_h == NULL) {
        architecture_h = n_hash_new(21, free);
        n_hash_ctl(architecture_h, TN_HASH_NOCPKEY);
    }
    
    if ((aarch = n_hash_get(architecture_h, arch)) == NULL) {
        aarch = n_strdup(arch);
        n_hash_insert(architecture_h, aarch, aarch);
    }

    return aarch;
}




/* always store fields in order: path, name, version, release, arch */
struct pkg *pkg_new_ext(const char *name, int32_t epoch,
                        const char *version, const char *release,
                        const char *arch, const char *os,
                        uint32_t size, uint32_t fsize,
                        uint32_t btime)
{
    struct pkg *pkg;
    int name_len = 0, version_len = 0, release_len = 0;
    char *buf;
    int len;

    n_assert(name);
    n_assert(version);
    n_assert(release);
    
    if (version == NULL || release == NULL)
        return NULL;
    
    name_len = strlen(name);
    len = name_len + 1;
    
    version_len = strlen(version);
    len += version_len + 1;
    	
    release_len = strlen(release);
    len += release_len + 1;

    len += len + 1;             /* for nvr */

    len += 1;
    pkg = pkg_alloc_fn(sizeof(*pkg) + len);
    memset(pkg, 0, sizeof(*pkg));
    
    pkg->free = pkg_free_fn;
    pkg->flags = PKG_COLOR_WHITE;
    pkg->epoch = epoch;
    pkg->size = size;
    pkg->fsize = fsize;
    pkg->btime = btime;
    pkg->_buf_size = len;
    buf = pkg->_buf;

    pkg->name = buf;
    memcpy(buf, name, name_len);
    buf += name_len;
    *buf++ = '\0';
    
    pkg->ver = buf;
    memcpy(buf, version, version_len);
    buf += version_len;
    *buf++ = '\0';
    
    pkg->rel = buf;
    memcpy(buf, release, release_len);
    buf += release_len;
    *buf++ = '\0';

    
    if (arch) 
        pkg->arch = register_arch(arch);

    if (os) 
        pkg->os = register_os(os);

    pkg->nvr = buf;
    memcpy(buf, name, name_len);
    buf += name_len;
    *buf++ = '-';
    
    memcpy(buf, version, version_len);
    buf += version_len;
    *buf++ = '-';
    
    memcpy(buf, release, release_len);
    buf += release_len;
    *buf++ = '\0';
    
    pkg->reqs = NULL;
    pkg->caps = NULL;
    pkg->cnfls = NULL;
    pkg->fl = NULL;
    pkg->reqpkgs = NULL;
    pkg->revreqpkgs = NULL;
    pkg->cnflpkgs = NULL;

    pkg->pkgdir = NULL;
    pkg->pkgdir_data = NULL;
    pkg->pkgdir_data_free = NULL;
    pkg->load_pkguinf = NULL;
    pkg->load_nodep_fl = NULL;
    
    pkg->pri = 0;
    pkg->groupid = 0;
    pkg->_refcnt = 0;
    
    return pkg;
}


void pkg_free(struct pkg *pkg) 
{

    //printf("free %s (%d)\n", pkg_snprintf_s(pkg), pkg->_refcnt);
    
    if (pkg->_refcnt > 0) {
        pkg->_refcnt--;
        return;
    }
    
    if (pkg->caps) {
        n_array_free(pkg->caps);
        pkg->caps = NULL;
    }
    
    
    if (pkg->reqs) {
        n_array_free(pkg->reqs);
        pkg->reqs = NULL;
    }
    	
    
    if (pkg->cnfls) {
        n_array_free(pkg->cnfls);
        pkg->cnfls = NULL;
    }
    

    if (pkg->reqpkgs) {
        n_array_free(pkg->reqpkgs);
        pkg->reqpkgs = NULL;
    }
    
    if (pkg->revreqpkgs) {
        n_array_free(pkg->revreqpkgs);
        pkg->revreqpkgs = NULL;
    }
    
    if (pkg->cnflpkgs) {
        n_array_free(pkg->cnflpkgs);
        pkg->cnflpkgs = NULL;
    }
    
    if (pkg->fl) {
        n_array_free(pkg->fl);
        pkg->fl = NULL;
    }
    
    if (pkg_has_ldpkguinf(pkg)) {
        if (pkg->pkg_pkguinf)
            pkguinf_free(pkg->pkg_pkguinf);
        pkg_clr_ldpkguinf(pkg);
    }

    if (pkg->pkgdir_data && pkg->pkgdir_data_free) {
        pkg->pkgdir_data_free(pkg->pkgdir_data);
        pkg->pkgdir_data = NULL;
    }
    
    pkg->free(pkg);
}

#if 0
struct pkg *pkg_link(struct pkg *pkg) 
{
    pkg->_refcnt++;
    //printf("link %s (%d)\n", pkg_snprintf_s(pkg), pkg->_refcnt);
    return pkg;
}
#endif

int pkg_strncmp_name(const struct pkg *p1, const struct pkg *p2)
{
    return strncmp(p1->name, p2->name, strlen(p2->name));
}


int pkg_eq_name_prefix(const struct pkg *pkg1, const struct pkg *pkg2) 
{
    char *p1, *p2;
    int n;
    
    if ((p1 = strchr(pkg1->name, '-')) == NULL)
        p1 = strchr(pkg1->name, '\0');

    if ((p2 = strchr(pkg2->name, '-')) == NULL)
        p2 = strchr(pkg2->name, '\0');

    n = p1 - pkg1->name;
    if (n - (p2 - pkg2->name) != 0)
        return 0;

    return strncmp(pkg1->name, pkg2->name, n) == 0;
}


static __inline__
int pkg_deepcmp_(const struct pkg *p1, const struct pkg *p2);

int pkg_deepstrcmp_name_evr(const struct pkg *p1, const struct pkg *p2) 
{
    register int rc = 0;

    if ((rc = pkg_cmp_name(p1, p2)))
        return rc;

    if ((rc = p1->epoch - p2->epoch))
        return rc;

    if ((rc = strcmp(p1->ver, p2->ver)))
        return rc;
    
    if ((rc = strcmp(p1->rel, p2->rel)))
        return rc;

    return pkg_deepcmp_(p1, p2);
}


int pkg_cmp_ver(const struct pkg *p1, const struct pkg *p2) 
{
    register int rc = 0;

    if ((rc = p1->epoch - p2->epoch))
        return rc;

    return pkg_version_compare(p1->ver, p2->ver);
}


int pkg_cmp_name_ver(const struct pkg *p1, const struct pkg *p2) 
{
    register int rc = 0;

    if ((rc = pkg_cmp_name(p1, p2)))
        return rc;

    return pkg_cmp_ver(p1, p2);
}


int pkg_cmp_evr(const struct pkg *p1, const struct pkg *p2) 
{
    int rc = 0;

    n_assert(p1->ver && p2->ver && p1->rel && p2->rel);
    
    if ((rc = p1->epoch - p2->epoch))
        return rc;
    
    rc = pkg_version_compare(p1->ver, p2->ver);

    if (rc == 0)
        rc = pkg_version_compare(p1->rel, p2->rel);
    
    return rc;
}


int pkg_cmp_name_evr(const struct pkg *p1, const struct pkg *p2) 
{
    register int rc;
    
    if ((rc = pkg_cmp_name(p1, p2)))
        return rc;
    
    return pkg_cmp_evr(p1, p2);
}

int pkg_cmp_name_evr_rev(const struct pkg *p1, const struct pkg *p2) 
{
    register int rc;

    if ((rc = pkg_cmp_name(p1, p2)))
        return rc;
    
    return -pkg_cmp_evr(p1, p2);

    //printf("cmp %s %s -> %d\n", pkg_snprintf_s(p1), pkg_snprintf_s0(p2), rc);
    //return rc;
}


int pkg_cmp_name_evr_rev_srcpri(const struct pkg *p1, const struct pkg *p2) 
{
    register int rc;

    if ((rc = pkg_cmp_name_evr_rev(p1, p2)) == 0 && p1->pkgdir != p2->pkgdir)
        return p1->pkgdir->pri - p2->pkgdir->pri;
    return rc;
}


int pkg_cmp_name_srcpri(const struct pkg *p1, const struct pkg *p2) 
{
    register int rc;

    if ((rc = pkg_cmp_name(p1, p2)) == 0) {
        if (p1->pkgdir != p2->pkgdir)
            rc = p1->pkgdir->pri - p2->pkgdir->pri;
        
        if (rc == 0)
            rc = -pkg_cmp_evr(p1, p2);
    }
    
    return rc;
}


int pkg_strcmp_name_evr(const struct pkg *p1, const struct pkg *p2) 
{
    register int rc;
    
    if ((rc = pkg_cmp_name(p1, p2)))
        return rc;

    n_assert(p1->ver && p2->ver && p1->rel && p2->rel);
    
    if ((rc = p1->epoch - p2->epoch))
        return rc;
    
    if ((rc = strcmp(p1->ver, p2->ver)) == 0)
        rc = strcmp(p1->rel, p2->rel);
    
    return rc;
}


static __inline__
int pkg_deepcmp_(const struct pkg *p1, const struct pkg *p2) 
{
    register int rc;
    
    if ((rc = p1->btime - p2->btime))
        return rc;

    if ((rc = p1->size - p2->size))
        return rc;

    if ((rc = p1->fsize - p2->fsize))
        return rc;

    if (p1->arch && p2->arch == NULL)
        return 1;
    
    if (p1->arch == NULL && p2->arch)
        return -1;

    if ((rc = strcmp(p1->arch, p2->arch)))
        return rc;
    
    if (p1->os && p2->os == NULL)
        return 1;
    
    if (p1->os == NULL && p2->os)
        return -1;

    return strcmp(p1->os, p2->os);
}

    
int pkg_deepcmp_name_evr_rev(const struct pkg *p1, const struct pkg *p2) 
{
    register int rc;
    
    if ((rc = pkg_cmp_name_evr_rev(p1, p2)))
        return rc;

    return pkg_deepcmp_(p1, p2);
}

int pkg_deepcmp_name_evr_rev_verify(const struct pkg *p1, const struct pkg *p2)
{
    register int rc;

    if ((rc = pkg_deepcmp_name_evr_rev(p1, p2)) == 0) {
        logn(LOGERR | LOGDIE, "packages %s and %s are equal to me, give up",
             pkg_snprintf_s(p1), pkg_snprintf_s0(p2));
        
    }
    
    return rc;
}


int pkg_cmp_uniq(const struct pkg *p1, const struct pkg *p2) 
{
    register int rc;
    
    if ((rc = pkg_cmp_name_evr_rev(p1, p2)) == 0 && verbose > 1)
        logn(LOGWARN, _("duplicated %s"), pkg_snprintf_s(p1));
    
    return rc;
}


int pkg_cmp_name_uniq(const struct pkg *p1, const struct pkg *p2) 
{
    register int rc;
    
    if ((rc = pkg_cmp_name(p1, p2)) == 0 && verbose > 1)
        logn(LOGWARN, _("duplicated name %s"), pkg_snprintf_s(p1));
    
    return rc;
}



int pkg_eq_name_evr(const struct pkg *p1, const struct pkg *p2) 
{
    if (p1 == p2)
        return 1;
    return pkg_cmp_name_evr(p1, p2);
}

int pkg_cmp_pri(struct pkg *p1, struct pkg *p2)
{
    register int cmprc = 0;

    if ((cmprc = p1->pri - p2->pri))
        return cmprc;
    
    return pkg_cmp_name_evr_rev(p1, p2);
}


int pkg_cmp_btime(struct pkg *p1, struct pkg *p2)
{
    register int cmprc;

    cmprc = p1->btime - p2->btime;
    if (cmprc == 0)
        cmprc = pkg_cmp_name_evr_rev(p1, p2);
    
    return cmprc;
}

int pkg_cmp_btime_rev(struct pkg *p1, struct pkg *p2)
{
    register int cmprc;

    cmprc = p2->btime - p1->btime;
    if (cmprc == 0)
        cmprc = pkg_cmp_name_evr_rev(p1, p2);
    
    return cmprc;
}



int pkg_eq_capreq(const struct pkg *pkg, const struct capreq *cr) 
{
    return strcmp(pkg->name, capreq_name(cr)) == 0 &&
        strcmp(pkg->ver, capreq_ver(cr)) == 0 &&
        strcmp(pkg->rel, capreq_rel(cr)) == 0 &&
        pkg->epoch == capreq_epoch(cr) &&
        cr->cr_relflags & REL_EQ;
}


int pkg_add_selfcap(struct pkg *pkg) 
{
    int i, has = 0;
    
    if (pkg->flags & PKG_HAS_SELFCAP)
        return 1;
    
    if (pkg->caps == NULL) {
        pkg->caps = capreq_arr_new(0);
        
    } else if ((i = capreq_arr_find(pkg->caps, pkg->name)) != -1) {
        
        for (i = i; i<n_array_size(pkg->caps); i++) {
            struct capreq *cap = n_array_nth(pkg->caps, i);
            
            if (strcmp(capreq_name(cap), pkg->name) != 0)
                break;

            if (capreq_epoch(cap) == pkg->epoch &&
                strcmp(capreq_ver(cap), pkg->ver) == 0 &&
                strcmp(capreq_rel(cap), pkg->rel) == 0) {
                
                has = 1;
                break;
            }
        }
    }

    pkg->flags |= PKG_HAS_SELFCAP;

    if (has == 1) {
        n_array_uniq(pkg->caps);
        return 1;
    }
    
    
    capreq_pkg(pkg->caps, pkg->epoch, pkg->name, strlen(pkg->name),
               pkg->ver, strlen(pkg->ver), pkg->rel, strlen(pkg->rel));
    
    if (n_array_size(pkg->caps)) {
        n_array_sort(pkg->caps);
        n_array_uniq(pkg->caps);
    } else {
        n_array_free(pkg->caps);
        pkg->caps = NULL;
        n_assert(0);
    }
    
    return pkg->caps != NULL;
}

/* RET: bool, true if cmprc matches relation */
__inline__ static
int rel_match(int cmprc, const struct capreq *req) 
{
    if (cmprc == 0)
        cmprc = req->cr_relflags & REL_EQ;
    else if (cmprc > 0)
        cmprc = req->cr_relflags & REL_GT;
    else if (cmprc < 0)
        cmprc = req->cr_relflags & REL_LT;
    else
        n_assert(0);
    
    return cmprc;
}

#define rel_not_match(cmprc, req) (rel_match(cmprc, req) == 0)

__inline__
int cap_match_req(const struct capreq *cap, const struct capreq *req,
                  int strict)
{
    register int cmprc = 0, evr = 0;

    DBGMSG_F("cap %s req %s\n", capreq_snprintf_s(cap), capreq_snprintf_s0(req));
    
    if ((strcmp(capreq_name(cap), capreq_name(req))) != 0)
        return 0;
    
    if (capreq_has_epoch(req)) {
        if (!capreq_has_epoch(cap))
            return strict == 0;

        cmprc = capreq_epoch(cap) - capreq_epoch(req);
        if (cmprc != 0)
            return rel_match(cmprc, req);
        evr = 1;
    }

    if (capreq_has_ver(req)) {
        if (!capreq_has_ver(cap))
            return strict == 0;
        
        cmprc = pkg_version_compare(capreq_ver(cap), capreq_ver(req));
        if (cmprc != 0)
            return rel_match(cmprc, req);
        evr = 1;
    }

    if (capreq_has_rel(req)) {
        if (!capreq_has_rel(cap))
            return strict == 0;
        
        cmprc = pkg_version_compare(capreq_rel(cap), capreq_rel(req));
        if (cmprc != 0)
            return rel_match(cmprc, req);
        evr = 1;
    }
    
    return evr ? rel_match(cmprc, req) : 1;
}

static inline 
int pkg_evr_match_req_(const struct pkg *pkg, const struct capreq *req)
{
    register int cmprc = 0, evr = 0;


    n_assert(strcmp(pkg->name, capreq_name(req)) == 0);

    if (!capreq_versioned(req))
        return 1;
    
    if (capreq_has_epoch(req) && pkg->epoch) {    
        cmprc = pkg->epoch - capreq_epoch(req);
        if (cmprc != 0)
            return rel_match(cmprc, req);
        evr = 1;
        
    } else if (capreq_epoch(req) > 0) {
        cmprc = 0;
        evr = 1;
    }
    
    
    if (capreq_has_ver(req)) {
        cmprc = pkg_version_compare(pkg->ver, capreq_ver(req));
        if (cmprc != 0)
            return rel_match(cmprc, req);
        evr = 1;
    }
        
    if (capreq_has_rel(req)) {
        n_assert(capreq_has_ver(req));
        cmprc = pkg_version_compare(pkg->rel, capreq_rel(req));
        if (cmprc != 0)
            return rel_match(cmprc, req);
        evr = 1;
    }
    
    return evr ? rel_match(cmprc, req) : 1;
}


int pkg_evr_match_req(const struct pkg *pkg, const struct capreq *req)
{
#if ENABLE_TRACE    
    register int rc;
    
    rc = pkg_evr_match_req_(pkg, req) ? 1:0;
    
    DBGMSG_F("%s[:%d] match %s ? %s\n", pkg_snprintf_s(pkg),
             pkg->epoch, capreq_snprintf_s(req), rc ? "YES" : "NO");
    return rc;
#else
    return pkg_evr_match_req_(pkg, req);
#endif
}



/* look up into package caps only */
int pkg_caps_match_req(const struct pkg *pkg, const struct capreq *req,
                       int strict)
{
    int n;
        
    DBGMSG("\npkg_caps_match_req %s %s\n", pkg_snprintf_s(pkg), 
           capreq_snprintf_s(req));
        
    if (pkg->caps == NULL || n_array_size(pkg->caps) == 0)
        return 0;     /* not match */
    
    if ((n = capreq_arr_find(pkg->caps, capreq_name(req))) == -1) {
        return 0;
            
    } else {
        struct capreq *cap;
        int i;

        cap = n_array_nth(pkg->caps, n);
        if (cap_match_req(cap, req, strict)) {
            DBGMSG("chk%d (%s-%s-%s) -> match (%d)\n", n, capreq_name(cap),
                   capreq_ver(cap), capreq_rel(cap), strict);
            return 1;
        }
        n++;
            
        for (i = n; i<n_array_size(pkg->caps); i++) {
            struct capreq *cap;

            cap = n_array_nth(pkg->caps, n);
            if (strcmp(capreq_name(cap), capreq_name(req)) != 0) {
                DBGMSG("chk%d %s-%s-%s -> NOT match IRET\n", i,
                       capreq_name(cap), capreq_ver(cap),
                       capreq_rel(cap));
                return 0;
            }
                
                
            if (cap_match_req(cap, req, strict)) {
                DBGMSG("chk %s-%s-%s -> match\n", capreq_name(cap),
                       capreq_ver(cap), capreq_rel(cap));
                return 1;
            } else {
                DBGMSG("chk%d %s-%s-%s -> NOT match\n", i,
                       capreq_name(cap), capreq_ver(cap),
                       capreq_rel(cap));
            }
        }
        DBGMSG("NONE\n");
    }
    
    return 0;
}


int pkg_has_path(const struct pkg *pkg,
                 const char *dirname, const char *basename)
{
    struct pkgfl_ent *flent, tmp;
    int rc = 0;
    
    if (pkg->fl == NULL || n_array_size(pkg->fl) == 0)
        return 0;

    if (*dirname == '/' && *(dirname + 1) != '\0')
        dirname++;

    tmp.dirname = (char*)dirname;
    tmp.items = 0;
    
    if ((flent = n_array_bsearch(pkg->fl, &tmp)) != NULL) {
        int i;
        
        for (i=0; i<flent->items; i++) {
            if (strcmp(basename, flent->files[i]->basename) == 0) {
                rc = 1;
                break;
            }
        }
    }

    return rc;
}

int pkg_match_req(const struct pkg *pkg, const struct capreq *req, int strict)
{
#if 0    
    /* package should not provide itself with different version */
#endif    
    if (strcmp(pkg->name, capreq_name(req)) == 0 && pkg_evr_match_req(pkg, req))
        return 1;
    
    return pkg_caps_match_req(pkg, req, strict);
}


int pkg_statisfies_req(const struct pkg *pkg, const struct capreq *req,
                       int strict) 
{
    if (!capreq_is_file(req))
        return pkg_match_req(pkg, req, strict);
    
    else {
        char *dirname, *basename, path[PATH_MAX];
        
        strncpy(path, capreq_name(req), sizeof(path));
        path[PATH_MAX - 1] = '\0';
        n_basedirnam(path, &dirname, &basename);
        n_assert(dirname);
        n_assert(*dirname);
        if (*dirname == '/' && *(dirname + 1) != '\0')
            dirname++;
        
        return pkg_has_path(pkg, dirname, basename);
    }

    return 1;
}


int pkg_obsoletes_pkg(const struct pkg *pkg, const struct pkg *opkg) 
{
    if (strcmp(pkg->name, opkg->name) != 0)
        return pkg_caps_obsoletes_pkg_caps(pkg, opkg);

    return pkg_cmp_evr(pkg, opkg) > 0;
}


/* look up into package caps */
int pkg_caps_obsoletes_pkg_caps(const struct pkg *pkg, const struct pkg *opkg)
{
    int n;
        
    DBGMSG("\npkg_obs_match_pkg %s %s\n", pkg_snprintf_s(pkg),
           pkg_snprintf_s0(opkg));
    
    if (pkg->cnfls == NULL || n_array_size(pkg->cnfls) == 0)
        return 0;     /* not match */
    
    if ((n = capreq_arr_find(pkg->cnfls, opkg->name)) == -1) {
        return 0;
            
    } else {
        struct capreq *cnfl;
        int i;

        cnfl = n_array_nth(pkg->cnfls, n);
        
        if (cnfl_is_obsl(cnfl) && pkg_match_req(opkg, cnfl, 1)) {
            DBGMSG("chk%d (%s-%s-%s) -> match\n", n,
                   capreq_snprintf_s(cnfl));
            return 1;
        }
        n++;
            
        for (i = n; i<n_array_size(pkg->cnfls); i++) {
            struct capreq *cnfl;
            
            cnfl = n_array_nth(pkg->cnfls, n);
            if (!cnfl_is_obsl(cnfl))
                continue;
            
            if (strcmp(capreq_name(cnfl), pkg->name) != 0) {
                DBGMSG("chk%d %s-%s-%s -> NOT match IRET\n", i,
                       capreq_snprintf_s(cnfl));
                return 0;
            }
                
                
            if (pkg_match_req(opkg, cnfl, 1)) {
                DBGMSG("chk %s-%s-%s -> match\n", capreq_snprintf_s(cnfl));
                return 1;
            } else {
                DBGMSG("chk%d %s-%s-%s -> NOT match\n", i,
                       capreq_snprintf_s(cnfl));
            }
        }
        DBGMSG("NONE\n");
    }
    
    return 0;
}

int pkg_add_pkgcnfl(struct pkg *pkg, struct pkg *cpkg, int isbastard)
{
    struct capreq *cnfl = NULL;
    
    if (n_array_bsearch_ex(pkg->cnfls, cpkg->name,
                           (tn_fn_cmp)capreq_cmp2name) == NULL) {
        cnfl = capreq_new(cpkg->name, cpkg->epoch, cpkg->ver,
                          cpkg->rel, REL_EQ,
                          (isbastard ? CAPREQ_PLDEKBAST : 0));
        
        n_array_push(pkg->cnfls, cnfl);
        n_array_sort(pkg->cnfls);
    }
    
    return cnfl != NULL;
}

int pkg_has_pkgcnfl(struct pkg *pkg, struct pkg *cpkg)
{
    return pkg->cnfls && (n_array_bsearch_ex(pkg->cnfls, cpkg->name,
                                             (tn_fn_cmp)capreq_cmp2name));
}

struct pkguinf *pkg_info(const struct pkg *pkg) 
{
    struct pkguinf *pkgu = NULL;

    if (pkg_has_ldpkguinf(pkg)) 
        pkgu = pkguinf_link(pkg->pkg_pkguinf);

    else if (pkg->load_pkguinf)
        pkgu = pkg->load_pkguinf(pkg, pkg->pkgdir_data);
    
    return pkgu;
}

tn_array *pkg_other_fl(const struct pkg *pkg) 
{
    tn_array *fl = NULL;
    
    if (pkg->load_nodep_fl)
        fl = pkg->load_nodep_fl(pkg, 
                                pkg->pkgdir_data, 
                                pkg->pkgdir ?
                                pkg->pkgdir->foreign_depdirs : NULL);
    
    return fl;
}


tn_array *pkg_info_get_fl(const struct pkg *pkg) 
{
    tn_array *fl = NULL;

    fl = pkg_other_fl(pkg);
    
    if (fl == NULL) {
        fl = pkg->fl;
        
    } else if (pkg->fl) {
        int i;
        for (i=0; i < n_array_size(pkg->fl); i++)
            n_array_push(fl, n_array_nth(pkg->fl, i));
    }
    
    if (fl)
        n_array_sort(fl);
    
    return fl;
}

void pkg_info_free_fl(const struct pkg *pkg, tn_array *fl) 
{
    if (pkg->load_nodep_fl && pkg->pkgdir_data)
        n_array_free(fl);
}


const char *pkg_group(const struct pkg *pkg) 
{
    if (pkg->pkgdir && pkg->pkgdir->pkgroups)
        return pkgroup(pkg->pkgdir->pkgroups, pkg->groupid);
    return NULL;
}

char *pkg_filename(const struct pkg *pkg, char *buf, size_t size) 
{
    unsigned len = 0;
    int n_len, v_len, r_len, a_len;
    char *s;
    
    n_len = pkg->ver  - pkg->name - 1;
    v_len = pkg->rel  - pkg->ver - 1;
    r_len = strlen(pkg->rel);

    if (pkg->arch) 
        a_len = strlen(pkg->arch);
    else
        a_len = 0;
    
    len = n_len + 1 + v_len + 1 +
        r_len + 1 + a_len + 1/* '.' */ + 3/* "rpm" */ + 1;

    if (len >= size)
        return NULL;
    
    s = buf;
    /* all pkg members are stored in _buf */
    memcpy(s, pkg->name, len - 4 - a_len); 
    
    s += n_len;
    n_assert(*s == '\0');
    *s++ = '-';

    s += v_len;
    n_assert(*s == '\0');
    *s++ = '-';

    s += r_len;
    n_assert(*s == '\0');	
    *s++ = '.';

    if (pkg->arch) {
        memcpy(s, pkg->arch, a_len);
        s += a_len;
        //n_assert(*s == '\0');
        *s++ = '.';
    }

    memcpy(s, "rpm\0", 4);
    return buf;
}

char *pkg_filename_s(const struct pkg *pkg) 
{
    static char buf[256];
    return pkg_filename(pkg, buf, sizeof(buf));
}

char *pkg_path(const struct pkg *pkg, char *buf, size_t size) 
{
    int n = 0;
    
    if (pkg->pkgdir->path) 
        n = n_snprintf(buf, size, "%s/", pkg->pkgdir->path);

    if (pkg_filename(pkg, buf + n, size - n) == NULL)
        buf = NULL;
    
    return buf;
}


char *pkg_path_s(const struct pkg *pkg) 
{
    static char buf[PATH_MAX];
    return pkg_path(pkg, buf, sizeof(buf));
}

int pkg_printf(const struct pkg *pkg, const char *str) 
{
    return printf("%s-%s-%s%s", pkg->name, pkg->ver, pkg->rel,
           str ? str : "");
}


int pkg_evr_snprintf(char *str, size_t size, const struct pkg *pkg)
{
    char epoch[32];

    *epoch = '\0';
    if (pkg->epoch)
        snprintf(epoch, sizeof(epoch), "%d:", pkg->epoch);
    
    return n_snprintf(str, size, "%s-%s%s-%s", pkg->name, epoch, pkg->ver,
                      pkg->rel);
}

char *pkg_evr_snprintf_s(const struct pkg *pkg)
{
    static char str[256];
    pkg_evr_snprintf(str, sizeof(str), pkg);
    return str;
}

char *pkg_evr_snprintf_s0(const struct pkg *pkg)
{
    static char str[256];
    pkg_evr_snprintf(str, sizeof(str), pkg);
    return str;
}


int pkg_snprintf(char *str, size_t size, const struct pkg *pkg)
{
    int n;
    
    n = n_snprintf(str, size, "%s-%s-%s", pkg->name, pkg->ver, pkg->rel);
    return n;
}

char *pkg_snprintf_s(const struct pkg *pkg)
{
    static char str[256];
    snprintf(str, sizeof(str), "%s-%s-%s", pkg->name, pkg->ver, pkg->rel);
    return str;
}

char *pkg_snprintf_s0(const struct pkg *pkg)
{
    static char str[256];
    snprintf(str, sizeof(str), "%s-%s-%s", pkg->name, pkg->ver, pkg->rel);
    return str;
}

char *pkg_snprintf_s1(const struct pkg *pkg)
{
    static char str[256];
    snprintf(str, sizeof(str), "%s-%s-%s", pkg->name, pkg->ver, pkg->rel);
    return str;
}


tn_array *pkgs_array_new(int size)
{
    tn_array *arr;
    
    arr = n_array_new(size, (tn_fn_free)pkg_free,
                      (tn_fn_cmp)pkg_cmp_name_evr_rev);
    n_array_ctl(arr, TN_ARRAY_AUTOSORTED);
    return arr;
}


int pkg_nvr_strcmp(struct pkg *p1, struct pkg *p2) 
{
    return strcmp(p1->nvr, p2->nvr);
}

int pkg_nvr_strcmp_rev(struct pkg *p1, struct pkg *p2) 
{
    return -strcmp(p1->nvr, p2->nvr);
}


int pkg_nvr_strncmp(struct pkg *pkg, const char *name) 
{
    return strncmp(pkg->nvr, name, strlen(name));
}

int pkg_nvr_strcmp_btime(struct pkg *p1, struct pkg *p2)
{
    register int cmprc;

    cmprc = p1->btime - p2->btime;
    if (cmprc == 0)
        cmprc = pkg_nvr_strcmp(p1, p2);

    return cmprc;
}

int pkg_nvr_strcmp_btime_rev(struct pkg *p1, struct pkg *p2)
{
    return -pkg_nvr_strcmp_btime(p1, p2);
}


static int gmt_off = 0; /* TZ offset */
static int gmt_off_flag = 0;

#include <time.h>
static void setup_gmt_off(void) 
{
    time_t t;
    struct tm *tm;

    t = time(NULL);
    if ((tm = localtime(&t))) 
#ifdef HAVE_TM_GMTOFF
        gmt_off = tm->tm_gmtoff;
#elif defined HAVE_TM___GMTOFF
        gmt_off = tm->__tm_gmtoff;
#endif        
}

int pkg_nvr_strcmp_bday(struct pkg *p1, struct pkg *p2)
{
    register int cmprc;

    if (gmt_off_flag == 0) {
        setup_gmt_off();
        gmt_off_flag = 1;
    }
    
    cmprc = ((p1->btime + gmt_off) / 86400) - ((p2->btime + gmt_off) / 86400);
    
    if (cmprc == 0)
        cmprc = pkg_nvr_strcmp(p1, p2);
    
    return cmprc;
}

int pkg_nvr_strcmp_bday_rev(struct pkg *p1, struct pkg *p2)
{
    return -pkg_nvr_strcmp_bday(p1, p2); 
}
