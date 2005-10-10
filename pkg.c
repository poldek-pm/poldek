/*
  Copyright (C) 2000 - 2004 Pawel A. Gajda <mis@k2.net.pl>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*!
  Package class
 */

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/param.h>


#include <trurl/nstr.h>
#include <trurl/nassert.h>
#define ENABLE_TRACE 0
#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "capreq.h"
#include "pkgfl.h"
#include "pkgu.h"
#include "pkg.h"
#include "pkgdir/pkgdir.h"
#include "pkgroup.h"
#include "pkgcmp.h"
#include "pkg_ver_cmp.h"

int poldek_conf_PROMOTE_EPOCH = 0;
int poldek_conf_MULTILIB = 0;

static tn_hash *architecture_h = NULL;
static tn_array *architecture_a = NULL;

static tn_hash *operatingsystem_h = NULL;
static tn_array *operatingsystem_a = NULL;

struct an_arch {
    int score;
    int index;
    char arch[0];
};

static
int pkgmod_register_arch(const char *arch)
{
    struct an_arch *an_arch;
    
    if (architecture_h == NULL) {
        architecture_h = n_hash_new(21, free);
        architecture_a = n_array_new(16, NULL, NULL);
        n_hash_ctl(architecture_h, TN_HASH_NOCPKEY);
    }

    if ((an_arch = n_hash_get(architecture_h, arch)) == NULL) {
        int len = strlen(arch);
        
        an_arch = n_malloc(sizeof(*an_arch) + len + 1);

        an_arch->score = pm_architecture_score(arch);
        n_assert(an_arch->score >= 0);
        if (!an_arch->score) an_arch->score = INT_MAX - 1;

        memcpy(an_arch->arch, arch, len + 1);
        n_array_push(architecture_a, an_arch);
        
        /* +1 in fact; 0 means no arch */
        an_arch->index = n_array_size(architecture_a);
        n_assert(an_arch->index < UINT16_MAX);
        n_hash_insert(architecture_h, an_arch->arch, an_arch);
    }
    
    return an_arch->index;
}

const char *pkg_arch(const struct pkg *pkg)
{
    if (pkg->_arch) {
        struct an_arch *a = n_array_nth(architecture_a, pkg->_arch - 1);
        n_assert(a);
        return a->arch;
    }
    return NULL;
}

int pkg_arch_score(const struct pkg *pkg)
{
    struct an_arch *a;
        
    if (!pkg->_arch)
        return 0;
    
    a = n_array_nth(architecture_a, pkg->_arch - 1);
    return a->score;
}

int pkg_set_arch(struct pkg *pkg, const char *arch)
{
    pkg->_arch = pkgmod_register_arch(arch);
    return 1;
}


struct an_os {
    //int score;
    int index;
    char os[0];
};

static
int pkgmod_register_os(const char *os)
{
    struct an_os *an_os;
    
    if (operatingsystem_h == NULL) {
        operatingsystem_h = n_hash_new(21, free);
        operatingsystem_a = n_array_new(16, NULL, NULL);
        n_hash_ctl(operatingsystem_h, TN_HASH_NOCPKEY);
    }

    if ((an_os = n_hash_get(operatingsystem_h, os)) == NULL) {
        int len = strlen(os);
        
        an_os = n_malloc(sizeof(*an_os) + len + 1);

        //an_os->score = pm__score(os);
        //n_assert(an_os->score >= 0);
        //if (!an_os->score) an_os->score = INT_MAX - 1;

        memcpy(an_os->os, os, len + 1);
        n_array_push(operatingsystem_a, an_os);
        /* +1 in fact; 0 means no os */
        an_os->index = n_array_size(operatingsystem_a); 
        n_hash_insert(operatingsystem_h, an_os->os, an_os);
    }

    return an_os->index;
}

const char *pkg_os(const struct pkg *pkg)
{
    if (pkg->_os) {
        struct an_os *o = n_array_nth(operatingsystem_a, pkg->_os - 1);
        n_assert(o);
        return o->os;
    }
    return NULL;
}

int pkg_set_os(struct pkg *pkg, const char *os)
{
    pkg->_os = pkgmod_register_os(os);
    return 1;
}



/* always store fields in order: path, name, version, release, arch */
struct pkg *pkg_new_ext(tn_alloc *na, 
                        const char *name, int32_t epoch,
                        const char *version, const char *release,
                        const char *arch, const char *os, const char *fn,
                        uint32_t size, uint32_t fsize,
                        uint32_t btime)
{
    struct pkg *pkg;
    int name_len = 0, version_len = 0, release_len = 0, fn_len = 0,arch_len = 0;
    char *buf, pkg_fn[PATH_MAX];
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

    if (fn && arch) {           /* compare filename with "standard" name */
        //fn = n_basenam(fn);
        n_snprintf(pkg_fn, sizeof(pkg_fn), "%s-%s-%s.%s.rpm", name,
                   version, release, arch);
        //printf("cmp %s %s\n", pkg_fn, fn);
        if (strcmp(pkg_fn, fn) == 0)
            fn = NULL;
        else {
            fn_len = strlen(fn);
            len += fn_len + 1;
        }
    }

    len += len + 1;             /* for id (nvr) */
    
    
    if (poldek_conf_MULTILIB && arch) {
        arch_len = strlen(arch); 
        len += arch_len + 1;
    }
        
    len += 1;
    if (na == NULL) {
        pkg = n_calloc(1, sizeof(*pkg) + len);
        
    } else {
        pkg = na->na_calloc(na, sizeof(*pkg) + len);
        pkg->na = n_ref(na);
        DBGF("+%p %p %d\n", na, &na->_refcnt, na->_refcnt);
    }
    
    pkg->flags |= PKG_COLOR_WHITE;
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
    
    pkg->fn = NULL;
    if (fn) {
        pkg->fn = buf;
        memcpy(buf, fn, fn_len);
        buf += fn_len;
        *buf++ = '\0';
    }

    if (arch) 
        pkg->_arch = pkgmod_register_arch(arch);

    if (os) 
        pkg->_os = pkgmod_register_os(os);

    pkg->_nvr = buf;
    memcpy(buf, name, name_len);
    buf += name_len;
    *buf++ = '-';
    
    memcpy(buf, version, version_len);
    buf += version_len;
    *buf++ = '-';
    
    memcpy(buf, release, release_len);
    buf += release_len;


    if (poldek_conf_MULTILIB && arch) {
        *buf++ = '.';
        memcpy(buf, arch, arch_len);
        buf += arch_len;
    }

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

#if 0                           /* nfy */
static tn_array *clone_array(tn_array *arr, unsigned flags)
{
    flags = flags;
    if (arr)
        return n_ref(arr);
    return NULL;
}

struct pkg *pkg_clone(tn_alloc *na, struct pkg *pkg, unsigned flags) 
{
    struct pkg *new;

    new = pkg_new_ext(na, pkg->name, pkg->epoch, pkg->ver, pkg->rel,
                      pkg_arch(pkg), pkg_os(pkg), pkg->fn,
                      pkg->size, pkg->fsize, pkg->btime);
    
    new->fmtime = pkg->fmtime;
    n_assert(flags & PKG_LDNEVR); /* deep copy not implemented yet */
        
    new->caps  = clone_array(pkg->caps, flags);
    new->reqs  = clone_array(pkg->reqs, flags);
    new->cnfls = clone_array(pkg->cnfls, flags);
    if (pkg->fl)
        new->fl = n_ref(pkg->fl)
    new->reqpkgs = clone_array(pkg->reqpkgs, flags);
    new->revreqpkgs = clone_array(pkg->revreqpkgs, flags);
    new->cnflpkgs = clone_array(pkg->cnflpkgs, flags);

    pkg->pkgdir = NULL;
    pkg->pkgdir_data = NULL;
    pkg->pkgdir_data_free = NULL;
    pkg->load_pkguinf = NULL;
    pkg->load_nodep_fl = NULL;
    
    pkg->pri = 0;
    pkg->groupid = 0;           /* remapping not implemented */
    pkg->_refcnt = 0;
    
    return pkg;
}
#endif

void pkg_free(struct pkg *pkg) 
{
#if ENABLE_TRACE   
    if (strcmp(pkg->name, "poldek") == 0) {
        DBGF_F("%p %s (pdir %s, na->refcnt=%d), refcnt=%d (%p)\n",
               pkg, pkg_snprintf_s(pkg),
               pkg->pkgdir ? pkgdir_idstr(pkg->pkgdir) : "<none>",
               pkg->na ? pkg->na->_refcnt : -1,
               pkg->_refcnt, &pkg->_refcnt);
    }
#endif
    if (pkg->_refcnt > 0) {
        pkg->_refcnt--;
        return;
    }
    n_array_cfree(&pkg->caps);
    n_array_cfree(&pkg->reqs);
    n_array_cfree(&pkg->cnfls);
    n_array_cfree(&pkg->reqpkgs);
    n_array_cfree(&pkg->revreqpkgs);
    n_array_cfree(&pkg->cnflpkgs);

    if (pkg->fl) {
        n_tuple_free(pkg->na, pkg->fl);
        pkg->fl = NULL;
    }
    
    if (pkg_has_ldpkguinf(pkg)) {
        if (pkg->pkg_pkguinf)
            pkguinf_free(pkg->pkg_pkguinf);
        pkg_clr_ldpkguinf(pkg);
    }

    if (pkg->pkgdir_data && pkg->pkgdir_data_free) {
        pkg->pkgdir_data_free(pkg->na, pkg->pkgdir_data);
        pkg->pkgdir_data = NULL;
    }

    
    if (pkg->na) {
        tn_alloc *na = pkg->na;
        memset(pkg, 0, sizeof(*pkg));
        DBGF("-%p %p %d\n", na, &na->_refcnt, na->_refcnt);
        n_alloc_free(na);
        return;
    }
    
    memset(pkg, 0, sizeof(*pkg));
    n_free(pkg);
}


int pkg_add_selfcap(struct pkg *pkg) 
{
    int i, has = 0;
    struct capreq *selfcap;
    
    if (pkg->flags & PKG_HAS_SELFCAP)
        return 1;

    if (pkg->caps == NULL) {
        pkg->caps = capreq_arr_new(0);
        
    } else if ((i = capreq_arr_find(pkg->caps, pkg->name)) != -1) {
        for (; i<n_array_size(pkg->caps); i++) {
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

    selfcap = capreq_new(pkg->na, pkg->name, pkg->epoch, pkg->ver, pkg->rel,
                         REL_EQ, 0);
    n_array_push(pkg->caps, selfcap);
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

static void promote_epoch_warn(int verbose_level,
                               const char *title0, const char *p0,
                               const char *p1)
{
    if (poldek_VERBOSE > verbose_level)
        logn(LOGWARN, "%s '%s' needs an epoch (assuming same "
             "epoch as %s)\n", title0, p0, p1);
}


int cap_xmatch_req(const struct capreq *cap, const struct capreq *req,
                   unsigned flags)
{
    register int cmprc = 0, evr = 0;

    DBGF("cap %s req %s\n", capreq_snprintf_s(cap), capreq_snprintf_s0(req));
    
    if (strcmp(capreq_name(cap), capreq_name(req)) != 0)
        return 0;

    if (!capreq_versioned(req))
        return 1;

    if (capreq_has_epoch(cap) || capreq_has_epoch(req)) {
        int do_promote = 0;

        if (poldek_conf_PROMOTE_EPOCH)
            flags |= POLDEK_MA_PROMOTE_EPOCH;
        
        if (flags & POLDEK_MA_PROMOTE_EPOCH) {
            if (!capreq_has_epoch(req) && (flags & POLDEK_MA_PROMOTE_REQEPOCH)){
                promote_epoch_warn(1, "req", capreq_snprintf_s(req),
                                   capreq_snprintf_s0(cap));
                do_promote = 1;
            }

            if (!capreq_has_epoch(cap) && (flags & POLDEK_MA_PROMOTE_CAPEPOCH)){
                promote_epoch_warn(1, "cap", capreq_snprintf_s(cap),
                                   capreq_snprintf_s0(req));
                do_promote = 1;
            }
        }
        
        if (do_promote) {
            cmprc = 0;
            
        } else {
            cmprc = capreq_epoch(cap) - capreq_epoch(req);
            if (cmprc != 0)
                return rel_match(cmprc, req);
        }
        evr = 1;
        
    }
#if 0                           /* disabled autopromotion */
    else if (capreq_epoch(req) > 0) { /* always promote cap's epoch */
        cmprc = 0;
        evr = 1;
    }
#endif    

#if 0    
    if (capreq_has_epoch(req)) {
        if (!capreq_has_epoch(cap))
            return strict == 0;

        cmprc = capreq_epoch(cap) - capreq_epoch(req);
        if (cmprc != 0)
            return rel_match(cmprc, req);
        evr = 1;
    }
#endif
    
    if (capreq_has_ver(req)) {
        if (!capreq_has_ver(cap))
            return (flags & POLDEK_MA_PROMOTE_VERSION);
        
        cmprc = pkg_version_compare(capreq_ver(cap), capreq_ver(req));
        if (cmprc != 0)
            return rel_match(cmprc, req);
        evr = 1;
    }

    if (capreq_has_rel(req)) {
        if (!capreq_has_rel(cap))
            return (flags & POLDEK_MA_PROMOTE_VERSION);
        
        cmprc = pkg_version_compare(capreq_rel(cap), capreq_rel(req));
        if (cmprc != 0)
            return rel_match(cmprc, req);
        evr = 1;
    }
    
    return evr ? rel_match(cmprc, req) : 1;
}

int cap_match_req(const struct capreq *cap, const struct capreq *req,
                  int strict)
{
    return cap_xmatch_req(cap, req, strict ? 0 : POLDEK_MA_PROMOTE_VERSION);
}

#if 0
static inline 
int OLDdo_pkg_evr_match_req(const struct pkg *pkg, const struct capreq *req,
                         int promote_epoch)
{
    register int cmprc = 0, evr = 0;


    n_assert(strcmp(pkg->name, capreq_name(req)) == 0);

    if (!capreq_versioned(req))
        return 1;

    if (promote_epoch == -1)
        promote_epoch = poldek_conf_PROMOTE_EPOCH;

    if (pkg->epoch || capreq_has_epoch(req)) {
        int promote = 0;
        if (!capreq_has_epoch(req) && promote_epoch) {
            promote_epoch_warn(1, "req", capreq_snprintf_s(req),
                               pkg_evr_snprintf_s(pkg));
            promote = 1;
        } 

        if (!pkg->epoch && capreq_epoch(req) > 0 && promote_epoch) {
            promote_epoch_warn(1, "package", pkg_evr_snprintf_s(pkg),
                               capreq_snprintf_s(req));
            promote = 1;
        }

        if (promote) {
            cmprc = 0;
            
        } else {
            cmprc = pkg->epoch - capreq_epoch(req);
            if (cmprc != 0)
                return rel_match(cmprc, req);
        }
        evr = 1;
        
    }
#if 0    /* disabled autopromotion */
    else if (capreq_epoch(req) > 0) { /* always promote package's epoch */
        cmprc = 0;
        evr = 1;
    }
#endif    
    
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
#endif

static inline 
int do_pkg_evr_match_req(const struct capreq *pkgcap, const struct capreq *req,
                         int promote_epoch)
{
    unsigned ma_flags = 0;
    
    if (promote_epoch == -1)
        promote_epoch = poldek_conf_PROMOTE_EPOCH;

    if (promote_epoch)     /* rpm promotes Requires epoch only */
        ma_flags |= POLDEK_MA_PROMOTE_REQEPOCH;
    
    return cap_xmatch_req(pkgcap, req, ma_flags);
}

int pkg_evr_match_req(const struct pkg *pkg, const struct capreq *req, unsigned flags)
{
    struct capreq *cap;
    register int rc = 0;
    
    n_assert(strcmp(pkg->name, capreq_name(req)) == 0);

    if (!capreq_versioned(req))
        return 1;

    cap = capreq_new(NULL, pkg->name, pkg->epoch, pkg->ver, pkg->rel,
                     REL_EQ, 0);
    
    if (flags & (POLDEK_MA_PROMOTE_VERSION | POLDEK_MA_PROMOTE_EPOCH)) {
        rc = do_pkg_evr_match_req(cap, req, 0) ? 1 : 0;
        if (!rc && pkg->epoch)  /* promote the epoch */
            rc = do_pkg_evr_match_req(cap, req, 1) ? 1 : 0;
        
    } else {
        rc = do_pkg_evr_match_req(cap, req, -1) ? 1 : 0;
    }

    capreq_free(cap);
    
    DBGF("%s match %s ? %s\n", pkg_evr_snprintf_s(pkg),
         capreq_snprintf_s(req), rc ? "YES" : "NO");
    return rc;
}


/* look up into package caps only */
int pkg_caps_match_req(const struct pkg *pkg, const struct capreq *req,
                       unsigned flags)
{
    int n;
        
    DBGF("\npkg_caps_match_req %s %s\n", pkg_snprintf_s(pkg), 
         capreq_snprintf_s(req));
        
    if (pkg->caps == NULL || n_array_size(pkg->caps) == 0)
        return 0;     /* not match */
    
    if ((n = capreq_arr_find(pkg->caps, capreq_name(req))) == -1) {
        return 0;
            
    } else {
        struct capreq *cap;
        int i;

        cap = n_array_nth(pkg->caps, n);
        if (cap_xmatch_req(cap, req, flags)) {
            DBGF("chk%d (%s-%s-%s) -> match (flags=%d)\n", n, capreq_name(cap),
                 capreq_ver(cap), capreq_rel(cap), flags);
            return 1;
        }
        n++;
            
        for (i = n; i<n_array_size(pkg->caps); i++) {
            struct capreq *cap;

            cap = n_array_nth(pkg->caps, n);
            if (strcmp(capreq_name(cap), capreq_name(req)) != 0) {
                DBGF("chk%d %s-%s-%s -> NOT match IRET\n", i,
                     capreq_name(cap), capreq_ver(cap),
                     capreq_rel(cap));
                return 0;
            }
                
                
            if (cap_xmatch_req(cap, req, flags)) {
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
    
    if (pkg->fl == NULL || n_tuple_size(pkg->fl) == 0)
        return 0;

    if (*dirname == '/' && *(dirname + 1) != '\0')
        dirname++;

    tmp.dirname = (char*)dirname;
    tmp.items = 0;

    flent = n_tuple_bsearch_ex(pkg->fl, &tmp, (tn_fn_cmp)pkgfl_ent_cmp);
    if (flent != NULL) {
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

int pkg_xmatch_req(const struct pkg *pkg, const struct capreq *req, unsigned flags)
{
    if (n_str_eq(pkg->name, capreq_name(req))) {
        if (pkg_evr_match_req(pkg, req, flags))
            return 1;
    }
    
    return pkg_caps_match_req(pkg, req, flags);
}


int pkg_match_req(const struct pkg *pkg, const struct capreq *req, int strict)
{
    if (strcmp(pkg->name, capreq_name(req)) == 0 &&
        pkg_evr_match_req(pkg, req, strict ? 0 : POLDEK_MA_PROMOTE_VERSION))
        return 1;
    
    return pkg_caps_match_req(pkg, req, strict ? 0 : POLDEK_MA_PROMOTE_VERSION);
}


int pkg_satisfies_req(const struct pkg *pkg, const struct capreq *req,
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

int pkg_requires_versioned_cap(const struct pkg *pkg, const struct capreq *cap)
{
    int i, rc = 0;

    DBGF("%s requires %s (reqs=%p, size=%d)?\n", pkg_id(pkg), capreq_snprintf_s(cap), pkg->reqs,
           pkg->reqs ? n_array_size(pkg->reqs) : 0);
    
    if (pkg->reqs == NULL)
        return 0;

    n_array_sort(pkg->reqs);
    i = n_array_bsearch_idx_ex(pkg->reqs, cap, (tn_fn_cmp)capreq_cmp_name);
    if (i == -1)
        return 0;
    
    while (i < n_array_size(pkg->reqs)) {
        struct capreq *req = n_array_nth(pkg->reqs, i);
        i++;

        if (strcmp(capreq_name(req), capreq_name(cap)) != 0)
            break;
        
        if (!capreq_versioned(req))
            continue;

        rc = cap_match_req(cap, req, 1);
        DBGF("  cap_match_req %s %s => %d\n", capreq_snprintf_s(cap), capreq_snprintf_s0(req), rc);
        if (rc)
            break;
    }
    
    return rc;
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
        
        if (capreq_is_obsl(cnfl) && pkg_match_req(opkg, cnfl, 1)) {
            DBGMSG("chk%d (%s-%s-%s) -> match\n", n,
                   capreq_snprintf_s(cnfl));
            return 1;
        }
        n++;
            
        for (i = n; i<n_array_size(pkg->cnfls); i++) {
            struct capreq *cnfl;
            
            cnfl = n_array_nth(pkg->cnfls, n);
            if (!capreq_is_obsl(cnfl))
                continue;
            
            if (strcmp(capreq_name(cnfl), pkg->name) != 0) {
                DBGMSG("chk%d %s -> NOT match IRET\n", i,
                       capreq_snprintf_s(cnfl));
                return 0;
            }
                
                
            if (pkg_match_req(opkg, cnfl, 1)) {
                DBGMSG("chk %s -> match\n", capreq_snprintf_s(cnfl));
                return 1;
            } else {
                DBGMSG("chk%d %s -> NOT match\n", i,
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
        cnfl = capreq_new(pkg->na, cpkg->name, cpkg->epoch, cpkg->ver,
                          cpkg->rel, REL_EQ,
                          (isbastard ? CAPREQ_BASTARD : 0));
        
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

struct pkguinf *pkg_info_ex(const struct pkg *pkg, tn_array *langs) 
{
    struct pkguinf *pkgu = NULL;

    if (pkg->load_pkguinf)
        pkgu = pkg->load_pkguinf(NULL, pkg, pkg->pkgdir_data, langs);
    
    else if (pkg_has_ldpkguinf(pkg)) 
        pkgu = pkguinf_link(pkg->pkg_pkguinf);
    
    return pkgu;
}

struct pkguinf *pkg_info(const struct pkg *pkg) 
{
    struct pkguinf *pkgu = NULL;
    if (pkg_has_ldpkguinf(pkg)) 
        pkgu = pkguinf_link(pkg->pkg_pkguinf);

    else if (pkg->load_pkguinf)
        pkgu = pkg->load_pkguinf(NULL, pkg, pkg->pkgdir_data, NULL);
    
    return pkgu;
}


static
tn_tuple *do_pkg_other_fl(tn_alloc *na, const struct pkg *pkg) 
{
    tn_tuple *fl = NULL;
    
    if (pkg->load_nodep_fl)
        fl = pkg->load_nodep_fl(na, pkg, 
                                pkg->pkgdir_data, 
                                pkg->pkgdir ?
                                pkg->pkgdir->foreign_depdirs : NULL);
    
    return fl;
}

struct pkgflist *pkg_info_get_nodep_flist(const struct pkg *pkg) 
{
    struct pkgflist *flist = NULL;
    tn_tuple *fl = NULL;
    tn_alloc *na;

    na = n_alloc_new(16, TN_ALLOC_OBSTACK);
    fl = do_pkg_other_fl(na, pkg);
    if (fl) {
        flist = na->na_malloc(na, sizeof(*flist));
        flist->_na = na;
        flist->fl = fl;
        n_tuple_sort_ex(flist->fl, (tn_fn_cmp)pkgfl_ent_cmp);
    }
    
    return flist;
}


struct pkgflist *pkg_info_get_flist(const struct pkg *pkg) 
{
    struct pkgflist *flist = NULL;
    tn_tuple *fl = NULL;
    tn_alloc *na;

    na = n_alloc_new(16, TN_ALLOC_OBSTACK);
    fl = do_pkg_other_fl(na, pkg);
    
    if (pkg->fl == NULL) {
        if (fl == NULL) {
            n_alloc_free(na);
        } else {
            flist = na->na_malloc(na, sizeof(*flist));
            flist->_na = na;
            flist->fl = fl;
        }
        
    } else {                    /* pkg->fl != NULL  */
        if (fl == NULL) {
            n_alloc_free(na);
            flist = n_malloc(sizeof(*flist));
            flist->_na = NULL;
            flist->fl = pkg->fl;
            
        } else {
            tn_tuple *wholefl;
            int i, n;
            
            wholefl = n_tuple_new(na, n_tuple_size(pkg->fl) + n_tuple_size(fl),
                                  NULL);
            n = 0;
            for (i=0; i<n_tuple_size(pkg->fl); i++)
                n_tuple_set_nth(wholefl, n++, n_tuple_nth(pkg->fl, i));

            for (i=0; i<n_tuple_size(fl); i++)
                n_tuple_set_nth(wholefl, n++, n_tuple_nth(fl, i));
            
            flist = na->na_malloc(na, sizeof(*flist));
            flist->_na = na;
            flist->fl = wholefl;
        }
    }
    
    if (flist)
        n_tuple_sort_ex(flist->fl, (tn_fn_cmp)pkgfl_ent_cmp);

    DBGF("RET %p, fl = %p, na = %p\n", flist, flist ? flist->fl : NULL,
           flist ? flist->_na : NULL);
    
    return flist;
}

void pkg_info_free_flist(struct pkgflist *flist)
{
    DBGF("FRE %p, fl = %p, na = %p\n", flist, flist ? flist->fl : NULL,
           flist ? flist->_na : NULL);
    
    if (flist->_na)
        n_alloc_free(flist->_na);
    else
        n_free(flist);
}

const char *pkg_group(const struct pkg *pkg) 
{
    if (pkg->pkgdir && pkg->pkgdir->pkgroups)
        return pkgroup(pkg->pkgdir->pkgroups, pkg->groupid);
    return NULL;
}

char *pkg_filename(const struct pkg *pkg, char *buf, size_t size) 
{
    int n_len, v_len, r_len, a_len = 0;
    unsigned len = 0;
    const char *arch = NULL;
    char *s;
    

    if (pkg->fn) {
        n_snprintf(buf, size, pkg->fn);
        return buf;
    }
    
    n_len = pkg->ver  - pkg->name - 1;
    v_len = pkg->rel  - pkg->ver - 1;
    r_len = strlen(pkg->rel);

    if (pkg->_arch) {
        arch  = pkg_arch(pkg);
        a_len = strlen(arch);
    }
    
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

    if (arch) {
        memcpy(s, arch, a_len);
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
    if (pkg->fn) 
        return pkg->fn;
        
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

const char *pkg_pkgdirpath(const struct pkg *pkg) 
{
    if (pkg->pkgdir)
        return pkg->pkgdir->path;
    return NULL;
}

unsigned pkg_file_url_type(const struct pkg *pkg)
{
    n_assert(pkg->pkgdir);
    return vf_url_type(pkg->pkgdir->path);
}

char *pkg_localpath(const struct pkg *pkg, char *path, size_t size,
                    const char *cachedir)
{
    int n = 0;
    char namebuf[1024], *fn;
    char *pkgpath;

    n_assert(pkg->pkgdir);
    pkgpath = pkg->pkgdir->path;

    fn = pkg_filename(pkg, namebuf, sizeof(namebuf));
    if (vf_url_type(pkgpath) == VFURL_PATH) {
        n = n_snprintf(path, size, "%s/%s", pkgpath, fn);
        
    } else {
        char buf[1024];
        //DBGF_F("pkgpath = %s\n", pkgpath);
        vf_url_as_dirpath(buf, sizeof(buf), pkgpath);
        //DBGF_F("pkgpath_dir = %s\n", buf);
        
        n = n_snprintf(path, size, "%s%s%s/%s", cachedir ? cachedir : "",
                       cachedir ? "/" : "", buf, n_basenam(fn));
    }

    //DBGF_F("%s\n", path);
    if (size - n > 2)
        return path;

    return NULL;
}

int pkg_printf(const struct pkg *pkg, const char *str) 
{
    return printf("%s-%s-%s%s", pkg->name, pkg->ver, pkg->rel,
           str ? str : "");
}

int pkg_evr_snprintf(char *str, size_t size, const struct pkg *pkg)
{
    char e[32];

    *e = '\0';
    if (pkg->epoch)
        snprintf(e, sizeof(e), "%d:", pkg->epoch);
    
    return n_snprintf(str, size, "%s-%s%s-%s", pkg->name, e, pkg->ver, pkg->rel);
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

tn_array *pkgs_array_new_ex(int size,
                            int (*cmpfn)(const struct pkg *p1,
                                         const struct pkg *p2))
{
    tn_array *arr;

    if (cmpfn == NULL)
        cmpfn = pkg_cmp_name_evr_rev;
    
    arr = n_array_new(size, (tn_fn_free)pkg_free, (tn_fn_cmp)cmpfn);
    n_array_ctl(arr, TN_ARRAY_AUTOSORTED);
    return arr;
}

tn_array *pkgs_array_new(int size) 
{
    return pkgs_array_new_ex(size, NULL);
}

char *pkg_strsize(char *buf, int size, const struct pkg *pkg) 
{
    char unit = 'K';
    double pkgsize = pkg->size/1024;

    if (pkgsize >= 1024) {
        pkgsize /= 1024;
        unit = 'M';
    }

    n_snprintf(buf, size, "%.1f %cB", pkgsize, unit);
    return buf;
}

static
char *do_strtime(char *buf, int size, uint32_t time) 
{
    time_t t = time;
    
    if (time)
        strftime(buf, size, "%Y/%m/%d %H:%M", localtime(&t));
    else
        *buf = '\0';
    
    buf[size-1] = '\0';
    return buf;
}


char *pkg_strbtime(char *buf, int size, const struct pkg *pkg) 
{
    return do_strtime(buf, size, pkg->btime);
}

char *pkg_stritime(char *buf, int size, const struct pkg *pkg) 
{
    return do_strtime(buf, size, pkg->itime);
}

void *pkg_na_malloc(struct pkg *pkg, size_t size)
{
    if (pkg->na)
        return pkg->na->na_malloc(pkg->na, size);
    n_assert(0);
    return NULL;
}

struct pkg *pkg_link(struct pkg *pkg)
{
#if ENABLE_TRACE    
    if (strcmp(pkg->name, "XX") == 0) {
        DBGF("%p %s (pdir %s, na->refcnt=%d), refcnt=%d (%p)\n",
             pkg, pkg_snprintf_s(pkg),
             pkg->pkgdir ? pkgdir_idstr(pkg->pkgdir) : "<none>",
             pkg->na ? pkg->na->_refcnt : -1,
             pkg->_refcnt, &pkg->_refcnt);
    }
#endif    
    pkg->_refcnt++;
    return pkg;
}

const char *pkg_id(const struct pkg *p) 
{
    return p->_nvr;
}

int pkg_id_snprintf(char *str, size_t size, const struct pkg *pkg)
{
    return n_snprintf(str, size, "%s", pkg_id(pkg));
}

int pkg_idevr_snprintf(char *str, size_t size, const struct pkg *pkg)
{
    if (poldek_conf_MULTILIB == 0)
        return n_snprintf(str, size, "%s-%s", pkg->ver, pkg->rel);
    
    return n_snprintf(str, size, "%s-%s.%s", pkg->ver, pkg->rel,
                      pkg_arch(pkg));
}
