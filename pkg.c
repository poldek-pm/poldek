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

#include <rpm/rpmlib.h>
#include <trurl/nstr.h>
#include <trurl/nassert.h>

#include "rpmadds.h"
#include "log.h"
#include "misc.h"
#include "capreq.h"
#include "pkg.h"
#include "h2n.h"
#include "pkgdir.h"

static void *(*pkg_alloc_fn)(size_t) = malloc;
static void (*pkg_free_fn)(void*) = free;


void set_pkg_allocfn(void *(*pkg_allocfn)(size_t), void (*pkg_freefn)(void*))
{
//    pkg_alloc_fn = pkg_allocfn;
//    pkg_free_fn = pkg_freefn;
}


/* always store fields in order: path, name, version, release, arch */
struct pkg *pkg_new(const char *name, int32_t epoch,
                    const char *version, const char *release,
                    const char *arch, uint32_t size, uint32_t btime)
{
    struct pkg *pkg;
    int name_len = 0, version_len = 0, release_len = 0, arch_len = 0;
    char *buf;
    int len;

    n_assert(name);
    n_assert(version);
    n_assert(release);
    n_assert(arch);
    
    if (version == NULL || release == NULL)
        return NULL;
    
    name_len = strlen(name);
    len = 1 + name_len + 1;
    
    version_len = strlen(version);
    len += version_len + 1;
    	
    release_len = strlen(release);
    len += release_len + 1;

    if (arch) {
        arch_len = strlen(arch);
        len += arch_len + 1;
    }
    
    pkg = pkg_alloc_fn(sizeof(*pkg) + len);
    pkg->free = pkg_free_fn;
    pkg->flags = PKG_COLOR_WHITE;
    pkg->epoch = epoch;
    pkg->size = size;
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

    n_assert(arch_len);
    pkg->arch = buf;
    memcpy(buf, arch, arch_len);
    buf += arch_len;
    *buf++ = '\0';
    
    pkg->reqs = NULL;
    pkg->caps = NULL;
    pkg->cnfls = NULL;
    pkg->fl = NULL;
    pkg->reqpkgs = NULL;
    pkg->revreqpkgs = NULL;
    pkg->cnflpkgs = NULL;
    pkg->other_files_offs = 0;
    pkg->pkgdir = NULL;
    pkg->pkg_pkguinf_offs = 0;

    pkg->_refcnt = 0;
    
    return pkg;
}

void pkg_free(struct pkg *pkg) 
{
    if (pkg->_refcnt > 0) {
        pkg->_refcnt--;
        return;
    }
        
    if (pkg->caps)
        n_array_free(pkg->caps);
    
    if (pkg->reqs)
        n_array_free(pkg->reqs);

    if (pkg->cnfls)
        n_array_free(pkg->cnfls);

    if (pkg->reqpkgs)
        n_array_free(pkg->reqpkgs);

    if (pkg->revreqpkgs)
        n_array_free(pkg->revreqpkgs);

    if (pkg->cnflpkgs)
        n_array_free(pkg->cnflpkgs);
    
    if (pkg->fl) 
        n_array_free(pkg->fl);

    if (pkg_has_ldpkguinf(pkg)) {
        if (pkg->pkg_pkguinf)
            pkguinf_free(pkg->pkg_pkguinf);
        pkg_clr_ldpkguinf(pkg);
    }
    

    pkg->free(pkg);
}


struct pkg *pkg_link(struct pkg *pkg) 
{
    pkg->_refcnt++;
    return pkg;
}


struct pkg *pkg_ldhdr(Header h, const char *fname, unsigned ldflags)
{
    struct pkg *pkg;
    uint32_t   *epoch, *size, *btime;
    char       *name, *version, *release, *arch;
    int        type;
    
    headerNVR(h, (void*)&name, (void*)&version, (void*)&release);
    if (name == NULL || version == NULL || release == NULL) {
        log(LOGERR, "%s: read name/version/release failed\n", fname);
        return NULL;
    }
    
    if (!headerGetEntry(h, RPMTAG_EPOCH, &type, (void *)&epoch, NULL)) 
        epoch = NULL;

    if (!headerGetEntry(h, RPMTAG_ARCH, &type, (void *)&arch, NULL)) {
        log(LOGERR, "%s: read architecture tag failed\n", fname);
        return NULL;
    }

    if (!headerGetEntry(h, RPMTAG_SIZE, &type, (void *)&size, NULL)) 
        size = NULL;

    if (!headerGetEntry(h, RPMTAG_BUILDTIME, &type, (void *)&btime, NULL)) 
        btime = NULL;
    
    pkg = pkg_new(name, epoch ? *epoch : 0, version, release, arch,
                  size ? *size : 0, btime ? *btime : 0);

    if (pkg == NULL)
        return NULL;

    msg(3, "ld %s\n", pkg_snprintf_s(pkg));
    
    if (ldflags & PKG_LDCAPS) {
        pkg->caps = capreq_arr_new(0);
        get_pkg_caps(pkg->caps, h);
    
        if (n_array_size(pkg->caps)) 
            n_array_sort(pkg->caps);
        else {
            n_array_free(pkg->caps);
            pkg->caps = NULL;
        }
    }
    
    if (ldflags & PKG_LDREQS) {
        pkg->reqs = capreq_arr_new(0);
        get_pkg_reqs(pkg->reqs, h);

        if (n_array_size(pkg->reqs) == 0) {
            n_array_free(pkg->reqs);
            pkg->reqs = NULL;
        }
    }

    if (ldflags & PKG_LDCNFLS) {
        pkg->cnfls = capreq_arr_new(0);
        get_pkg_cnfls(pkg->cnfls, h);
        get_pkg_obsls(pkg->cnfls, h);
        
        if (n_array_size(pkg->cnfls) > 0) {
            n_array_sort(pkg->cnfls);
        
        } else {
            n_array_free(pkg->cnfls);
            pkg->cnfls = NULL;
        };
    }

    if (ldflags & PKG_LDFL) {
        pkg->fl = pkgfl_array_new(32);
    
        if (pkgfl_ldhdr(pkg->fl, h, PKGFL_ALL, pkg_snprintf_s(pkg)) == -1) {
            pkg_free(pkg);
            pkg = NULL;
        
        } else if (n_array_size(pkg->fl) > 0) {
            n_array_sort(pkg->fl);
            
        } else {
            n_array_free(pkg->fl);
            pkg->fl = NULL;
        };
    }
    
    return pkg;
}


struct pkg *pkg_ldrpm(const char *path, unsigned ldflags)
{
    struct pkg *pkg = NULL;
    FD_t fdt;
    Header h;
    
    if ((fdt = Fopen(path, "r")) == NULL) 
        log(LOGERR, "open %s: %s\n", path, rpmErrorString());
        
    else {
        if (rpmReadPackageHeader(fdt, &h, NULL, NULL, NULL) != 0) {
            log(LOGERR, "%s: read header failed\n", path);
            
        } else {
            if (headerIsEntry(h, RPMTAG_SOURCEPACKAGE))
                log(LOGERR, "%s: reject source package\n", path);
            else
                pkg = pkg_ldhdr(h, path, ldflags);
            
            headerFree(h);
        }
        Fclose(fdt);
    }

    return pkg;
}


int pkg_cmp_name(const struct pkg *p1, const struct pkg *p2) 
{
    return strcmp(p1->name, p2->name);
}

int pkg_cmp_ver(const struct pkg *p1, const struct pkg *p2) 
{
    register int rc = 0;

    if ((rc = p1->epoch - p2->epoch))
        return rc;

    return rpmvercmp(p1->ver, p2->ver);
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
    
    rc = rpmvercmp(p1->ver, p2->ver);

    if (rc == 0)
        rc = rpmvercmp(p1->rel, p2->rel);
    
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
    int has = 0;
    
    if (pkg->flags & PKG_HAS_SELFCAP)
        return 1;
    
    if (pkg->caps == NULL) {
        pkg->caps = capreq_arr_new(0);
        
    } else {
        int i;
        
        for (i=0; i<n_array_size(pkg->caps); i++) {
            struct capreq *cap = n_array_nth(pkg->caps, i);
            
            if (strcmp(capreq_name(cap), pkg->name) == 0 &&
                capreq_epoch(cap) == pkg->epoch &&
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
    register int cmprc;

    
    if ((strcmp(capreq_name(cap), capreq_name(req))) != 0)
        return 0;
    
    if (capreq_has_epoch(req)) {
        if (!capreq_has_epoch(cap))
            return strict == 0;

        cmprc = capreq_epoch(cap) - capreq_epoch(req);
        if (rel_not_match(cmprc, req))
            return 0;

        /* if epochs are not equal versions and releases doesn't matter */
        if (cmprc != 0) 
            return 1;
    }

    if (capreq_has_ver(req)) {
        if (!capreq_has_ver(cap))
            return strict == 0;
        
        cmprc = rpmvercmp(capreq_ver(cap), capreq_ver(req));
        if (rel_not_match(cmprc, req))
            return 0;

        /* if versions are not equal releases doesn't matter */
        if (cmprc != 0) 
            return 1;
    }

    if (capreq_has_rel(req)) {
        if (!capreq_has_rel(cap))
            return strict == 0;
        
        if (rel_not_match(rpmvercmp(capreq_rel(cap), capreq_rel(req)), req))
            return 0;
        
    }

    return 1;
}


int pkg_evr_match_req(const struct pkg *pkg, const struct capreq *req)
{
    register int cmprc;


    n_assert(strcmp(pkg->name, capreq_name(req)) == 0);
    
    if (capreq_has_epoch(req)) {
        cmprc = pkg->epoch - capreq_epoch(req);
        if (rel_not_match(cmprc, req))
            return 0;
        
        /* if epochs are not equal versions and releases doesn't matter */
        if (cmprc != 0) 
            return 1;
    }
    
    if (capreq_has_ver(req)) {
        cmprc = rpmvercmp(pkg->ver, capreq_ver(req));
        if (rel_not_match(cmprc, req))
            return 0;

        /* if versions are not equal releases doesn't matter */
        if (cmprc != 0) 
            return 1;
    }
        
    if (capreq_has_rel(req)) {
        n_assert(capreq_has_ver(req));
        if (rel_not_match(rpmvercmp(pkg->rel, capreq_rel(req)), req))
            return 0;
    }
    
    return 1;
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
    
    if ((n = n_array_bsearch_idx(pkg->caps, req)) == -1) {
        return 0;
            
    } else {
        struct capreq *cap;
        int i;

        if (strict == 0)    /* matched by name */
            return 1;
        
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
    /* package should not provide itself with diffrent version */
    if (strcmp(pkg->name, capreq_name(req)) == 0)
        return pkg_evr_match_req(pkg, req);
    
    return pkg_caps_match_req(pkg, req, strict);
}


#if 0
static __inline__
int pkg_match_cnfl(const struct pkg *pkg, const struct capreq *cnfl)
{
    register int cmprc;
    
    if (strcmp(pkg->name, capreq_name(cnfl)) != 0)
        return 0;
    
    if (capreq_has_epoch(cnfl)) {
        cmprc = pkg->epoch - capreq_epoch(cnfl);
        if (rel_not_match(cmprc, cnfl))
            return 0;

        /* if epochs are not equal versions and releases doesn't matter */
        if (cmprc != 0) 
            return 1;
    }

    if (capreq_has_ver(cnfl)) {
        cmprc = rpmvercmp(pkg->ver, capreq_ver(cnfl));
        if (rel_not_match(cmprc, cnfl))
            return 0;

        /* if versions are not equal releases doesn't matter */
        if (cmprc != 0) 
            return 1;
    }

    if (capreq_has_rel(cnfl)) 
        if (rel_not_match(rpmvercmp(pkg->rel, capreq_rel(cnfl)), cnfl))
            return 0;
    
    return 1;
}


/* look up into package caps */
int pkg_obsoletes_pkg(const struct pkg *pkg, const struct pkg *opkg)
{
    int n;
        
    DBGMSG("\npkg_obs_match_pkg %s %s\n", pkg_snprintf_s(pkg),
           pkg_snprintf_s0(opkg));
    
    if (pkg->cnfls == NULL || n_array_size(pkg->cnfls) == 0)
        return 0;     /* not match */
    
    if ((n = n_array_bsearch_idx_ex(pkg->cnfls, pkg->name,
                                    (tn_fn_cmp)capreq_cmp2name)) == -1) {
        return 0;
            
    } else {
        struct capreq *cnfl;
        int i;

        cnfl = n_array_nth(pkg->cnfls, n);
        
        if (cnfl_is_obsl(cnfl) && pkg_match_cnfl(opkg, cnfl)) {
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
                
                
            if (pkg_match_cnfl(opkg, cnfl)) {
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
#endif 

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

#if 0
void pkg_store(const struct pkg *pkg, tn_buf *nbuf) 
{
    uint32_t nflags, nsize; 
    int32_t nepoch, nbuf_size;

    nflags = hton32(pkg->flags);
    nepoch = hton32(pkg->epoch);
    nsize = hton32(pkg->size);
    nbuf_size = hton32(pkg->_buf_size);
    
    n_buf_add(nbuf, &nflags, sizeof(nflags));
    n_buf_add(nbuf, &nsize, sizeof(nsize));
    n_buf_add(nbuf, &nepoch, sizeof(nepoch));
    n_buf_add(nbuf, &nbuf_size, sizeof(nbuf_size));
    n_buf_add(nbuf, pkg->_buf, pkg->_buf_size);
    
    capreqs_store(pkg->caps, nbuf);
    capreqs_store(pkg->reqs, nbuf);
    capreqs_store(pkg->cnfls, nbuf);
}
    

struct pkg *pkg_restore(tn_buf_it *nbufi) 
{
    uint32_t flags, size; 
    int32_t epoch, buf_size;
    struct pkg *pkg;
    char *p;
    

    p = n_buf_it_get(nbufi, sizeof(flags));

    if (p == NULL)
        return NULL;
    
    flags = ntoh32(*(uint32_t*)p);

    p = n_buf_it_get(nbufi, sizeof(size));
    if (p == NULL)
        return NULL;
    size = ntoh32(*(int32_t*)p);
    
    p = n_buf_it_get(nbufi, sizeof(epoch));
    if (p == NULL)
        return NULL;
    epoch = ntoh32(*(int32_t*)p);

    p = n_buf_it_get(nbufi, sizeof(buf_size));
    if (p == NULL)
        return NULL;
    
    buf_size = ntoh32(*(int32_t*)p);

    p = n_buf_it_get(nbufi, buf_size);
    if (p == NULL)
        return NULL;

    pkg = pkg_alloc_fn(sizeof(*pkg) + buf_size);
    pkg->flags = flags;
    pkg->size = size;
    pkg->epoch = epoch;
    pkg->_buf_size = buf_size;
    memcpy(pkg->_buf, p, buf_size);
    
    pkg->caps = capreqs_restore(nbufi);
    pkg->reqs = capreqs_restore(nbufi);
    pkg->cnfls = capreqs_restore(nbufi);
    return pkg;
}
#endif /* 0 */

struct pkguinf *pkg_info(const struct pkg *pkg) 
{
    struct pkguinf *pkgu = NULL;

    
    if (pkg_has_ldpkguinf(pkg)) 
        pkgu = pkguinf_link(pkg->pkg_pkguinf);
    
    else if (pkg->pkgdir)
        pkgu = pkguinf_restore(pkg->pkgdir->vf->vf_stream, pkg->pkg_pkguinf_offs);
    
    if (pkgu)
        pkgu = pkguinf_touser(pkgu);

    return pkgu;
    
}


char *pkg_filename(const struct pkg *pkg, char *buf, size_t size) 
{
    unsigned len = 0;
    int n_len, v_len, r_len, a_len;
    char *s;
    
    s = buf;
    
    n_len = pkg->ver  - pkg->name - 1;
    v_len = pkg->rel  - pkg->ver - 1;
    r_len = pkg->arch - pkg->rel - 1;
    a_len = strlen(pkg->arch);
    
    len = n_len + 1 + v_len + 1 +
        r_len + 1 + a_len + 1/* '.' */ + 3/* "rpm" */ + 1;

    if (len >= size)
        return NULL;

    memcpy(s, pkg->name, len - 4); /* all pkg members stored in _buf */
    
    s += n_len;
    n_assert(*s == '\0');
    *s++ = '-';

    s += v_len;
    n_assert(*s == '\0');
    *s++ = '-';

    s += r_len;
    n_assert(*s == '\0');	
    *s++ = '.';

    s += a_len;
    n_assert(*s == '\0');	
    *s++ = '.';

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
        n = snprintf(buf, size, "%s/", pkg->pkgdir->path);

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


char *pkg_snprintf(char *str, size_t size, const struct pkg *pkg)
{
    snprintf(str, size, "%s-%s-%s", pkg->name, pkg->ver, pkg->rel);
    return str;
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
