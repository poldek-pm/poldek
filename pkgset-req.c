/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <trurl/trurl.h>

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"
#include "pkgmisc.h"
#include "capreq.h"
#include "pkgset-req.h"

void *pkg_na_malloc(struct pkg *pkg, size_t size);


static
int setup_req_pkgs(struct pkg *pkg, struct capreq *req, int strict, 
                  struct pkg *suspkgs[], int npkgs);

static
int setup_cnfl_pkgs(struct pkg *pkg, struct capreq *cnfl, int strict,
                   struct pkg *suspkgs[], int npkgs);


static
struct reqpkg *reqpkg_new(struct pkg *pkg, struct capreq *req,
                          uint8_t flags, int nadds)
{
    struct reqpkg *rpkg;
    
    if (flags & REQPKG_MULTI) {
        n_assert(nadds > 0);
        rpkg = pkg_na_malloc(pkg, sizeof(*rpkg) + ((nadds + 1) * sizeof(rpkg)));
        rpkg->adds[nadds] = NULL;
        
    } else {
        rpkg = pkg_na_malloc(pkg, sizeof(*rpkg) + (nadds * sizeof(rpkg)));
    }
    	
    rpkg->pkg = pkg;
    rpkg->req = req;
    rpkg->flags = flags;
    return rpkg;
}

static
int reqpkg_cmp(struct reqpkg *p1, struct reqpkg *p2)
{
    return (size_t)p1->pkg - (size_t)p2->pkg;
}

static
struct pkg_unreq *pkg_unreq_new(struct capreq *req, int mismatch)
{
    struct pkg_unreq *unreq;
    char s[512];
    int n;


    n = capreq_snprintf(s, sizeof(s), req);
    n_assert(n > 0);
    
    unreq = n_malloc(sizeof(*unreq) + n + 1);
    unreq->mismatch = mismatch;
    memcpy(unreq->req, s, n + 1);
    return unreq;
}

static
void visit_badreqs(struct pkgmark_set *pms, struct pkg *pkg, int deep)
{
    int i;
    
    if (pkg_has_unmetdeps(pms, pkg)) 
        return;

    pkg_set_unmetdeps(pms, pkg);
    msg_i(4, deep, " %s\n", pkg_snprintf_s(pkg));
    deep += 2;
    
    if (pkg->revreqpkgs) {
        for (i=0; i<n_array_size(pkg->revreqpkgs); i++) {
            struct pkg *revpkg;
            revpkg = n_array_nth(pkg->revreqpkgs, i);
            if (!pkg_has_unmetdeps(pms, revpkg))
                visit_badreqs(pms, revpkg, deep);
        }
    }
}

static 
int mark_badreqs(struct pkgmark_set *pms) 
{
    int i, deep = 1, nerrors = 0;
    tn_array *pkgs;
    
    pkgs = pkgmark_get_packages(pms, PKGMARK_UNMETDEPS);
    if (pkgs) {
        n_assert(n_array_size(pkgs));
        msg(4, "Packages with unsatisfied dependencies:\n");
    
        for (i=0; i < n_array_size(pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgs, i);
            nerrors++;
            pkg_clr_unmetdeps(pms, pkg);
            visit_badreqs(pms, pkg, deep);
        }
        n_array_free(pkgs);
    }
    
    return nerrors;
}


static
int pkgset_add_unreq(struct pkgset *ps, struct pkg *pkg, struct capreq *req,
                     int mismatch)
{
    tn_array *unreqs;


    if ((unreqs = n_hash_get(ps->_vrfy_unreqs, pkg_id(pkg))) == NULL) {
        unreqs = n_array_new(2, free, NULL);
        n_hash_insert(ps->_vrfy_unreqs, pkg_id(pkg), unreqs);
    }
    n_array_push(unreqs, pkg_unreq_new(req, mismatch));
    return 1;
}


int pkgset_verify_deps(struct pkgset *ps, int strict)
{
    int i, j, nerrors = 0;
    struct pkgmark_set *pms;
    
    n_assert(ps->_vrfy_unreqs == NULL);
    ps->_vrfy_unreqs = n_hash_new(127, (tn_fn_free)n_array_free);
    pms = pkgmark_set_new(n_array_size(ps->pkgs) / 10, 0);

    msgn(4, _("\nVerifying dependencies..."));
    
    for (i=0; i < n_array_size(ps->pkgs); i++) {
        struct pkg *pkg;

        pkg = n_array_nth(ps->pkgs, i);
        if (pkg->reqs == NULL)
            continue;
        
        n_assert(n_array_size(pkg->reqs));
        pkg->reqpkgs = n_array_new(n_array_size(pkg->reqs)/2+2, NULL,
                                   (tn_fn_cmp)reqpkg_cmp);

        msg(4, "%d. %s\n", i+1, pkg_snprintf_s(pkg));
        for (j=0; j < n_array_size(pkg->reqs); j++) {
            struct pkg *pkgsbuf[1024], **suspkgs;
            int nsuspkgs = 1024;
            struct capreq *req;

            req = n_array_nth(pkg->reqs, j);
            
            if (psreq_lookup(ps, req, &suspkgs, (struct pkg **)pkgsbuf,
                             &nsuspkgs)) {
                
                if (nsuspkgs == 0) /* self match */
                    continue;
                
                if (setup_req_pkgs(pkg, req, strict, suspkgs, nsuspkgs)) 
                    continue;
                else 
                    goto l_err_match;
            }
            
            nerrors++;
            if (poldek_VERBOSE > 3)
                msg(4, " req %-35s --> NOT FOUND\n", capreq_snprintf_s(req));

            pkgset_add_unreq(ps, pkg, req, 0);
            pkg_set_unmetdeps(pms, pkg);
            continue;
            
        l_err_match:
            nerrors++;
            pkgset_add_unreq(ps, pkg, req, 1);
            pkg_set_unmetdeps(pms, pkg);
        }
    }

    if (nerrors)
        mark_badreqs(pms);
    else 
        msgn(4, _("No unsatisfied dependencies detected -- OK"));

    if (nerrors) 
        msgn(4, _("%d unsatisfied dependencies, %d packages cannot be installed"),
            nerrors, ps->nerrors);
    
    pkgmark_set_free(pms);
    return nerrors == 0;
}


__inline__
static int add_reqpkg(struct pkg *pkg, struct capreq *req, struct pkg *dpkg)
{
    struct reqpkg *rpkg;
    struct reqpkg tmp_rpkg = { NULL, NULL, 0 };

    tmp_rpkg.pkg = dpkg;
    rpkg = n_array_bsearch(pkg->reqpkgs, &tmp_rpkg);

    if (rpkg == NULL) {
        rpkg = reqpkg_new(dpkg, req, 0, 0);
        
        n_array_push(pkg->reqpkgs, rpkg);
        n_array_sort(pkg->reqpkgs);
        if (dpkg->revreqpkgs == NULL)
            dpkg->revreqpkgs = n_array_new(2, NULL, NULL);
        n_array_push(dpkg->revreqpkgs, pkg);
    }
    
    n_assert(rpkg);
    
    if (capreq_is_prereq(req)) 
        rpkg->flags |= REQPKG_PREREQ;
    
    if (capreq_is_prereq_un(req)) 
        rpkg->flags |= REQPKG_PREREQ_UN;
    
    return 1;
}


static
void isort_pkgs(struct pkg *pkgs[], size_t size)
{
    register size_t i, j;

#if ENABLE_TRACE
    printf("before isort(): ");
    for (i = 0; i < size; i++) {
        register struct pkg *p = pkgs[i];
        printf("%s, ", pkg_id(p)); 
    }
    printf("\n");
#endif    
		   
    for (i = 1; i < size; i++) {
        register void *tmp = pkgs[i];

        j = i;

        while (j > 0 && pkg_cmp_name_evr_rev(tmp, pkgs[j - 1]) < 0) {
            DBGF(" %s < %s\n", pkg_id(tmp), pkg_id(pkgs[j - 1]));	
            pkgs[j] = pkgs[j - 1];
            j--;
        }
        
        pkgs[j] = tmp;
    }

#if ENABLE_TRACE
    printf("after isort(): ");
    for (i = 0; i < size; i++) {
        register struct pkg *p = pkgs[i];
        printf("%s, ", pkg_id(p)); 
    }
    printf("\n");
#endif    
}

/*
  Lookup req in ps
  If found returns true and
  - if req is rpmlib() requirement set npkgs to zero
  - otherwise suspkgs is pointed to array of "suspect" packages,  
    in npkgs suspkgs size is stored. Suspected packages are sorted
    descending by name and EVR.
  
*/   
int psreq_lookup(struct pkgset *ps, struct capreq *req,
                 struct pkg ***suspkgs, struct pkg **pkgsbuf, int *npkgs)
{
    const struct capreq_idx_ent *ent;
    char *reqname;
    int matched;


    reqname = capreq_name(req);
    *npkgs = 0;
    matched = 0;

    if ((ent = capreq_idx_lookup(&ps->cap_idx, reqname))) {
        *suspkgs = (struct pkg **)ent->crent_pkgs;
        *npkgs = ent->items;
        matched = 1;
        
    } else if (*reqname == '/') {
        int n;
        
        n = file_index_lookup(&ps->file_idx, reqname, 0, pkgsbuf, *npkgs);
        if (n > 0) {
            *npkgs = n;
            matched = 1;
            *suspkgs = pkgsbuf;
        }
    }

    /* disabled - well tested
      if (strncmp("rpmlib", capreq_name(req), 6) == 0 && !capreq_is_rpmlib(req))
         n_assert(0);
    */ 
    
    if (capreq_is_rpmlib(req)) {
        if (matched) {
            int i;
            
            for (i=0; i<*npkgs; i++) {
                if (strcmp((*suspkgs)[i]->name, "rpm") != 0) {
                    logn(LOGERR, _("%s: provides rpmlib cap \"%s\""),
                        pkg_snprintf_s((*suspkgs)[i]), reqname);
                    matched = 0;
                }
            }
        }

        if (pkgset_pmprovides(ps, req)) {
            matched = 1;
            capreq_set_satisfied(req);
            msg(4, " req %-35s --> PM_CAP\n", capreq_snprintf_s(req));
        }
#if 0
DUPA        
        if (!matched && (ps->flags & (PSMODE_VERIFY | PSMODE_MKIDX))) {
            matched = 1;
            logn(LOGWARN, "%s: not found (poldek needs to be linked with newer"
                 " rpmlib)\n", capreq_snprintf_s(req));
        }
#endif        
        
        *suspkgs = NULL;
        *npkgs = 0;
    }

    return matched;
}


int psreq_match_pkgs(const struct pkg *pkg, struct capreq *req, int strict, 
                     struct pkg *suspkgs[], int npkgs,
                     struct pkg **matches, int *nmatched)
{
    int i, n, nmatch;
    
    msg(4, " req %-35s --> ",  capreq_snprintf_s(req));
    
    n = 0;
    nmatch = 0;
    
    for (i = 0; i < npkgs; i++) {
        struct pkg *spkg = suspkgs[i];
        
        if (capreq_has_ver(req))  /* check version */
            if (!pkg_match_req(spkg, req, strict)) 
                continue;

        msg(4, "_%s, ", pkg_snprintf_s(spkg));
        nmatch++;
        
        if (spkg != pkg) { /* do not add itself */
            matches[n++] = spkg;
            
        } else {
            n = 0;
            break;
            //log(LOGERR, "%s: requires itself\n", pkg_snprintf_s(pkg));
        }
    }

    if (n > 1) 
        isort_pkgs(matches, n);
    
    msg(4, nmatch ? "\n" : "_UNMATCHED\n");

    *nmatched = n;
    return nmatch;
}


int psreq_find_match_packages(struct pkgset *ps,
                              const struct pkg *pkg, struct capreq *req,
                              struct pkg ***packages, int *npackages,
                              int strict)
{
    struct pkg **suspkgs, pkgsbuf[1024];
    int nsuspkgs = 0, found = 0;
    
    
    if (packages)
        *packages = NULL;
    
    found = psreq_lookup(ps, req, &suspkgs, (struct pkg **)pkgsbuf, &nsuspkgs);
        
    if (found && nsuspkgs) {
        struct pkg **matches;
        int nmatches = 0;
        
        found = 0;
        matches = alloca(sizeof(*matches) * nsuspkgs);
        if (psreq_match_pkgs(pkg, req, strict, suspkgs, nsuspkgs, matches, &nmatches)) {
            found = 1;
            
            if (nmatches && packages) {
                struct pkg **pkgs;
                int i;
                
                pkgs = n_malloc(sizeof(*pkgs) * (nmatches + 1));
                for (i=0; i < nmatches; i++)
                    pkgs[i] = matches[i];
                
                pkgs[nmatches] = NULL;
                *packages = pkgs;
                *npackages = nmatches;
            }
        }
    }
    
    return found;
}



static
int setup_req_pkgs(struct pkg *pkg, struct capreq *req, int strict, 
                   struct pkg *suspkgs[], int npkgs)
{
    int i, nmatched;
    struct pkg **matches;

    matches = alloca(sizeof(*matches) * npkgs);
    
    if (!psreq_match_pkgs(pkg, req, strict, suspkgs, npkgs,
                          matches, &nmatched))
        return 0;
    
    if (nmatched == 0)          /* selfmatched */
        return 1;
    
    if (nmatched == 1) {
        return add_reqpkg(pkg, req, matches[0]);
        
    } else {
        int isneq;
        uint8_t flags;
        struct reqpkg *rpkg;
        struct reqpkg tmp_rpkg = {NULL, NULL, 0};

        flags = 0;
        flags |= capreq_is_prereq(req) ? REQPKG_PREREQ : 0;
        flags |= capreq_is_prereq_un(req) ? REQPKG_PREREQ_UN : 0;
        
        tmp_rpkg.pkg = matches[0];
        rpkg = n_array_bsearch(pkg->reqpkgs, &tmp_rpkg);

        isneq = 1;
        /* compare the list */
        if (rpkg != NULL && rpkg->flags & REQPKG_MULTI) { 
            i = 0;
            isneq = 0;
            while (rpkg->adds[i] != NULL) {
                if (i+1 >= nmatched) {   /* different length */
                    isneq = 1;
                    break;
                }
                
                if (rpkg->adds[i]->pkg != matches[i+1]) {
                    isneq = 1;
                    break;
                }
                i++;
            }
        }
        
        if (isneq) {
            struct pkg *dpkg;

            dpkg = matches[0];
            rpkg = reqpkg_new(dpkg, req, flags | REQPKG_MULTI, nmatched - 1);
            n_array_push(pkg->reqpkgs, rpkg);
            n_array_sort(pkg->reqpkgs);
            if (dpkg->revreqpkgs == NULL)
                dpkg->revreqpkgs = n_array_new(2, NULL, (tn_fn_cmp)pkg_nvr_strcmp);
            n_array_push(dpkg->revreqpkgs, pkg);

            for (i=1; i<nmatched; i++) {
                dpkg = matches[i];
                
                rpkg->adds[i - 1] = reqpkg_new(dpkg, req, flags, 0);
                if (dpkg->revreqpkgs == NULL)
                    dpkg->revreqpkgs = n_array_new(2, NULL, NULL);
                n_array_push(dpkg->revreqpkgs, pkg);
            }
        }
    }
    
    return 1;
}



int pkgset_verify_conflicts(struct pkgset *ps, int strict) 
{
    int i, j;

    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg;

        pkg = n_array_nth(ps->pkgs, i);
        if (pkg->cnfls == NULL)
            continue;
        
        n_assert(n_array_size(pkg->cnfls));
        msg(4, "%d. %s\n", i, pkg_snprintf_s(pkg));
        for (j=0; j < n_array_size(pkg->cnfls); j++) {
            const struct capreq_idx_ent *ent;
            struct capreq *cnfl;
            char *cnflname;

            cnfl = n_array_nth(pkg->cnfls, j);
            cnflname = capreq_name(cnfl);
            
            if ((ent = capreq_idx_lookup(&ps->cap_idx, cnflname))) {
                if (setup_cnfl_pkgs(pkg, cnfl, strict,
                                    (struct pkg **)ent->crent_pkgs,
                                    ent->items)) {
                    continue;
                }
                
            } else {
                msg(4, " cnfl %-35s --> NOT FOUND\n",capreq_snprintf_s(cnfl));
            }
        }
    }
    
    return 1;
}


static
int setup_cnfl_pkgs(struct pkg *pkg, struct capreq *cnfl, int strict,
                    struct pkg *suspkgs[], int npkgs)
{
    int i, nmatch = 0;
    
    msg(4, " cnfl %-35s --> ",  capreq_snprintf_s(cnfl));
    n_assert(npkgs > 0);
    
    for (i = 0; i < npkgs; i++) {
        struct pkg *spkg = suspkgs[i];
        struct reqpkg *cnflpkg;
        
        /* bastard conflicts are direct */
        if (capreq_is_bastard(cnfl) && pkg_cmp_name(pkg, spkg) != 0)
            continue;
    
        if (capreq_has_ver(cnfl))  /* check version */
            if (!pkg_match_req(spkg, cnfl, strict)) 
                continue;
            
        /* do not conflict with myself */
        if (spkg == pkg) 
            continue;
        
        msg(4, "_%s, ", pkg_snprintf_s(spkg));

        cnflpkg = NULL;
        if (pkg->cnflpkgs) {
            struct reqpkg tmp_spkg = { spkg, NULL, 0 };
            cnflpkg = n_array_bsearch(pkg->cnflpkgs, &tmp_spkg);
        }
        
        if (cnflpkg != NULL) {
            if (capreq_is_obsl(cnfl)) 
                cnflpkg->flags |= REQPKG_OBSOLETE;
            
        } else {
            cnflpkg = reqpkg_new(spkg, cnfl, REQPKG_CONFLICT, 0);
            if (pkg->cnflpkgs == NULL) 
                pkg->cnflpkgs = n_array_new(n_array_size(pkg->cnfls)/2+2, NULL,
                                            (tn_fn_cmp)reqpkg_cmp);

            if (capreq_is_obsl(cnfl))
                cnflpkg->flags |= REQPKG_OBSOLETE;
            n_array_push(pkg->cnflpkgs, cnflpkg);
            n_array_sort(pkg->cnflpkgs);
        }
        nmatch++;
    }
    
    if (nmatch == 0)
        msg(4, "_UNMATCHED\n");
    else 
        msg(4, "\n");
    
    return nmatch;
}
