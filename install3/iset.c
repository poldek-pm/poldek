/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

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

#define ENABLE_TRACE 0

#include <stdlib.h>
#include <string.h>
#include <sys/param.h>          /* for PATH_MAX */

#include <trurl/nassert.h>
#include <trurl/nhash.h>
#include <trurl/narray.h>
#include <trurl/nmalloc.h>
#include <trurl/nstr.h>

#include "i18n.h"
#include "pkg.h"
#include "capreq.h"
#include "iset.h"
#include "log.h"

extern int poldek_conf_MULTILIB;
#define PKGMARK_ISET (1 << 20)

struct iset {
    tn_array             *pkgs;         
    tn_hash              *capcache; /* cache of resolved packages caps */
    struct pkgmark_set   *pms;
};

inline void iset_markf(struct iset *iset, struct pkg *pkg, unsigned mflag)
{
    pkg_set_mf(iset->pms, pkg, mflag);
}

inline int iset_ismarkedf(struct iset *iset, const struct pkg *pkg,
                          unsigned mflag)
{
    return pkg_isset_mf(iset->pms, pkg, mflag);

}

const tn_array *iset_packages(struct iset *iset)
{
    return iset->pkgs;
}

tn_array *iset_packages_in_install_order(struct iset *iset)
{
    tn_array *pkgs = NULL;
    void *cmpfn;
    
    cmpfn = n_array_ctl_set_cmpfn(iset->pkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev);
    
    packages_order(iset->pkgs, &pkgs, PKGORDER_INSTALL);
    n_assert(pkgs);
    n_assert(n_array_size(pkgs) == n_array_size(iset->pkgs));

    n_array_ctl_set_cmpfn(iset->pkgs, cmpfn);
    return pkgs;
}

const struct pkgmark_set *iset_pms(struct iset *iset)
{
    return iset->pms;
}

struct iset *iset_new(void) 
{
    struct iset *iset; 

    iset = n_malloc(sizeof(*iset));
    iset->pkgs = pkgs_array_new_ex(128, pkg_cmp_recno);
    iset->capcache = n_hash_new(128, NULL);
    iset->pms = pkgmark_set_new(0, 0);
    return iset;
}

void iset_free(struct iset *iset) 
{
    n_array_free(iset->pkgs);
    n_hash_free(iset->capcache);
    pkgmark_set_free(iset->pms);
    free(iset);
}

void iset_add(struct iset *iset, struct pkg *pkg, unsigned mflag)
{
    DBGF("add %s\n", pkg_id(pkg));
    n_array_push(iset->pkgs, pkg_link(pkg));
    
    mflag |= PKGMARK_ISET;
    iset_markf(iset, pkg, mflag);
}

int iset_remove(struct iset *iset, struct pkg *pkg)
{
    int i;
    
    if (!iset_ismarkedf(iset, pkg, PKGMARK_ISET)) /* not here */
        return 0;
    
    n_hash_clean(iset->capcache); /* flush all, TODO: remove pkg caps only */
    pkg_clr_mf(iset->pms, pkg, PKGMARK_ISET);
    
    for (i=0; i < n_array_size(iset->pkgs); i++) {
        struct pkg *p = n_array_nth(iset->pkgs, i);
        if (pkg_cmp_name_evr(p, pkg) == 0) {
            if (!poldek_conf_MULTILIB)
                break;
            
            if (pkg_cmp_arch(p, pkg) == 0)
                break;
        }
    }

    if (i < n_array_size(iset->pkgs)) /* found */
        n_array_remove_nth(iset->pkgs, i);
    
    n_assert(!iset_has_pkg(iset, pkg));
    return 1;
}

int iset_has_pkg_recno(struct iset *iset, int recno) 
{
    struct pkg tmpkg;
    tmpkg.recno = recno;
    return n_array_bsearch(iset->pkgs, &tmpkg) != NULL;
}

int iset_has_pkg(struct iset *iset, const struct pkg *pkg)
{
    int i;
    
    return iset_ismarkedf(iset, pkg, PKGMARK_ISET);
    
    for (i=0; i<n_array_size(iset->pkgs); i++) {
        struct pkg *p = n_array_nth(iset->pkgs, i);
        if (pkg_cmp_name_evr(p, pkg) == 0) {
            if (poldek_conf_MULTILIB)
                return pkg_cmp_arch(p, pkg) == 0;
            else
                return 1;
        }
    }
    
    return 0;
}

int iset_provides(struct iset *iset, const struct capreq *cap)
{
    char             *dirname, *basename, path[PATH_MAX];
    char             *capnvr = NULL, *capname = NULL;
    int              i, is_file = 0;
    struct pkg       *pkg = NULL;
    

    if (!capreq_has_ver(cap)) {
        capname = (char*)capreq_name(cap);
        
    } else {
        capname = capnvr = alloca(256);
        capreq_snprintf(capnvr, 256, cap);
    }
    
    if ((pkg = n_hash_get(iset->capcache, capname))) {
        DBGF("cache hit %s\n", capreq_snprintf_s(cap));
        return 1;
    }
    
    if (capreq_is_file(cap)) {
        is_file = 1;
        strncpy(path, capreq_name(cap), sizeof(path));
        path[PATH_MAX - 1] = '\0';
        n_basedirnam(path, &dirname, &basename);
        n_assert(dirname);
        
        if (*dirname == '\0') { /* path = "/foo" */
            char *tmp;
            n_strdupap("/", &tmp);
            dirname = tmp;
        }
        
        n_assert(*dirname);
        if (*dirname == '/' && *(dirname + 1) != '\0')
            dirname++;
    }

    pkg = NULL;
    for (i=0; i < n_array_size(iset->pkgs); i++) {
        struct pkg *p = n_array_nth(iset->pkgs, i);
        
        if (is_file && pkg_has_path(p, dirname, basename))
            pkg = p;
        
        else if (pkg_match_req(p, cap, 1))
            pkg = p;

        DBGF("  - %s provides %s -> %s\n", pkg_id(p),
             capreq_snprintf_s(cap), pkg ? "YES" : "NO");
        
        if (pkg)
            break;
    }

    if (pkg != NULL) {
        DBGF("addto cache %s\n", capnvr ? capnvr : capname);
        if (capnvr == NULL) {
            n_hash_insert(iset->capcache, capname, pkg);
            
        } else {
            n_hash_insert(iset->capcache, capnvr, pkg);
            if (!n_hash_exists(iset->capcache, capname))
                n_hash_insert(iset->capcache, capname, pkg);
        }
    }
    
    DBGF("%s -> %s\n", capreq_snprintf_s(cap), pkg ? pkg_id(pkg) : "NO");
    return pkg != NULL;
}

void iset_dump(struct iset *iset)
{
    int i;

    printf("iset dump %p: ",  iset);
    
        
    for (i=0; i<n_array_size(iset->pkgs); i++) {
        struct pkg *pkg = n_array_nth(iset->pkgs, i);
        printf("%s, ", pkg_snprintf_s(pkg));
    }
    printf("\n");
}
