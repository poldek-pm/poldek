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
#include <obstack.h>

#include <rpmlib.h>
#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>

#include "log.h"
#include "pkg.h"
#include "pkgset-def.h"
#include "pkgset.h"
#include "misc.h"
#include "usrset.h"
#include "rpmadds.h"
#include "pkgset-req.h"


extern void *pkg_alloc(size_t size);


static
int setup_req_pkgs(struct pkg *pkg, struct capreq *req, int strict, 
                  struct pkg *suspkgs[], int npkgs);

static
int setup_cnfl_pkgs(struct pkg *pkg, struct capreq *cnfl, int strict,
                   struct pkg *suspkgs[], int npkgs);

static 
int pkgset_verify_conflicts(struct pkgset *ps, int strict);


struct reqpkg *reqpkg_new(struct pkg *pkg, uint8_t flags, int nadds)
{
    struct reqpkg *rpkg;
    
    if (flags & REQPKG_MULTI) {
        n_assert(nadds > 0);
        rpkg = pkg_alloc(sizeof(*rpkg) + ((nadds + 1) * sizeof(rpkg)));
        rpkg->adds[nadds] = NULL;
        
    } else {
        rpkg = pkg_alloc(sizeof(*rpkg) + (nadds * sizeof(rpkg)));
    }
    	
    rpkg->pkg = pkg;
    rpkg->flags = flags;
    return rpkg;
}


int reqpkg_cmp(struct reqpkg *p1, struct reqpkg *p2)
{
    return (size_t)p1->pkg - (size_t)p2->pkg;
}


static
void visit_badreqs(struct pkg *pkg, int deep) 
{
    int i;
    
    if (pkg_has_badreqs(pkg)) 
        return;

    pkg_set_badreqs(pkg);
    msg_i(2, deep, " %s\n", pkg_snprintf_s(pkg));
    deep += 2;
    
    if (pkg->revreqpkgs) {
        for (i=0; i<n_array_size(pkg->revreqpkgs); i++) {
            struct pkg *revpkg;
            revpkg = n_array_nth(pkg->revreqpkgs, i);
            if (!pkg_has_badreqs(revpkg)) 
                visit_badreqs(revpkg, deep);
        }
    }
}

static 
void mark_badreqs(struct pkgset *ps) 
{
    int i, deep = 1;
    
    msg(2, "Packages with unsatisfied dependencies:\n");
    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
        if (pkg_has_badreqs(pkg)) {
            ps->nerrors++;
            pkg_clr_badreqs(pkg);
            visit_badreqs(pkg, deep);
        }
    }
}



int pkgset_verify_deps(struct pkgset *ps, int strict)
{
    int i, j, nerrors = 0;

    msg(1, "$Verifying dependencies...\n");
    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg;

        pkg = n_array_nth(ps->pkgs, i);
        if (pkg->reqs == NULL)
            continue;
        
        n_assert(n_array_size(pkg->reqs));
        pkg->reqpkgs = n_array_new(n_array_size(pkg->reqs)/2+2, NULL,
                                   (tn_fn_cmp)reqpkg_cmp);

        msg(4, "%d. %s\n", i+1, pkg_snprintf_s(pkg));
        for (j=0; j<n_array_size(pkg->reqs); j++) {
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
            if (verbose > 2)
                msg(4, " req %-35s --> NOT FOUND\n", capreq_snprintf_s(req));
            else
                log(LOGERR, "%s: req %s not found\n", pkg_snprintf_s(pkg),
                    capreq_snprintf_s(req));
            pkg_set_badreqs(pkg);
            continue;
            
        l_err_match:
            nerrors++;
            if (verbose < 3)
                log(LOGERR, "%s: req %s not matched\n", pkg_snprintf_s(pkg),
                    capreq_snprintf_s(req));
            
            pkg_set_badreqs(pkg);
        }
    }

    mark_badreqs(ps);
    if (nerrors) 
        msg(1,"%d unsatisfied dependencies, %d packages cannot be installed\n",
            nerrors, ps->nerrors);

    pkgset_verify_conflicts(ps, strict);
    return nerrors == 0;
}


__inline__
static int add_reqpkg(struct pkg *pkg, struct capreq *req, struct pkg *dpkg)
{
    struct reqpkg *rpkg;
    struct reqpkg tmp_rpkg = {NULL, 0, {NULL}};

    tmp_rpkg.pkg = dpkg;
    rpkg = n_array_bsearch(pkg->reqpkgs, &tmp_rpkg);

    if (rpkg != NULL) {
        if (capreq_is_prereq(req)) 
            rpkg->flags |= REQPKG_PREREQ;
        
    } else {
        rpkg = reqpkg_new(dpkg, capreq_is_prereq(req) ? REQPKG_PREREQ : 0, 0);
        n_array_push(pkg->reqpkgs, rpkg);
        n_array_sort(pkg->reqpkgs);
        if (dpkg->revreqpkgs == NULL)
            dpkg->revreqpkgs = n_array_new(2, NULL, NULL);
        n_array_push(dpkg->revreqpkgs, pkg);
    }
    return 1;
}


static
void isort_pkgs(struct pkg *pkgs[], size_t size)
{
    register size_t i, j;

    for (i = 1; i < size; i++) {
	register void *tmp = pkgs[i];

	j = i;

	while (j > 0 && pkg_cmp_name_evr(tmp, pkgs[j - 1]) > 0) {
	    pkgs[j] = pkgs[j - 1];
	    j--;
	}
        
	pkgs[j] = tmp;
    }
}

int psreq_lookup(struct pkgset *ps, struct capreq *req,
                 struct pkg ***suspkgs, struct pkg **pkgsbuf, int *npkgs)
{
    const struct capreq_idx_ent *ent;
    char *reqname;
    int matched, isrpmreq;
            
    reqname = capreq_name(req);

    *npkgs = 0;
    matched = 0;

    isrpmreq = (strncmp(reqname, "rpmlib(", 7) == 0);
    
    if ((ent = capreq_idx_lookup(&ps->cap_idx, reqname))) {
        *suspkgs = (struct pkg **)ent->pkgs;
        *npkgs = ent->items;
        matched = 1;
        
    } else if (*reqname == '/') {
        int n;
        
        n = file_index_lookup(&ps->file_idx, reqname, pkgsbuf, *npkgs);
        if (n > 0) {
            *npkgs = n;
            matched = 1;
            *suspkgs = pkgsbuf;
        }
    }

    if (isrpmreq) {
        struct capreq *cap;
        
        if (matched) {
            int i;
            
            for (i=0; i<*npkgs; i++) {
                if (strcmp((*suspkgs)[i]->name, "rpm") != 0) 
                    log(LOGERR, "%s: provides %s\n",
                        pkg_snprintf_s((*suspkgs)[i]), reqname);
            }
            matched = 0;
            *npkgs = 0;
            *suspkgs = NULL;
        }

        if (!(ps->flags & (PSMODE_VERIFY | PSMODE_MKIDX))) {
            if ((cap = n_array_bsearch(ps->rpmcaps, req)))
                if (cap_match_req(cap, req, 1)) {
                    matched = 1;
                    msg(4, " req %-35s --> RPMLIB_CAP\n", capreq_snprintf_s(req));
                }
        }
    }
    
    return matched;
}


int psreq_match_pkgs(struct pkg *pkg, struct capreq *req, int strict, 
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
        
        if (spkg != pkg) /* do not add itself */
            matches[n++] = spkg;
#if 0 /* too many packages requires itself  */
        else {
            log(LOGERR, "\n");
            log(LOGERR, "%s: requires itself\n", pkg_snprintf_s(pkg));
        }
#endif        
    }

    if (n > 1) 
        isort_pkgs(matches, n);
    
    msg(4, nmatch ? "\n" : "_UNMATCHED\n");

    *nmatched = n;
    return nmatch;
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
        struct reqpkg tmp_rpkg = {NULL, 0, {NULL}};

        flags = 0;
        flags |= capreq_is_prereq(req) ? REQPKG_PREREQ : 0;
        tmp_rpkg.pkg = matches[0];
        rpkg = n_array_bsearch(pkg->reqpkgs, &tmp_rpkg);

        isneq = 1;
        /* compare the list */
        if (rpkg != NULL && rpkg->flags & REQPKG_MULTI) { 
            i = 0;
            isneq = 0;
            while (rpkg->adds[i] != NULL) {
                if (i+1 >= nmatched) {   /* diffrent length */
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
            rpkg = reqpkg_new(dpkg, flags | REQPKG_MULTI, nmatched - 1);
            n_array_push(pkg->reqpkgs, rpkg);
            n_array_sort(pkg->reqpkgs);
            if (dpkg->revreqpkgs == NULL)
                dpkg->revreqpkgs = n_array_new(2, NULL, NULL);
            n_array_push(dpkg->revreqpkgs, pkg);
            
            for (i=1; i<nmatched; i++) {
                dpkg = matches[i];
                
                rpkg->adds[i - 1] = reqpkg_new(dpkg, flags, 0);
                if (dpkg->revreqpkgs == NULL)
                    dpkg->revreqpkgs = n_array_new(2, NULL, NULL);
                n_array_push(dpkg->revreqpkgs, pkg);
            }
        }
    }
    
    return 1;
}


struct cnflpkg *cnflpkg_new(struct pkg *pkg, uint8_t flags)
{
    struct cnflpkg *cpkg;
    
    cpkg = pkg_alloc(sizeof(*cpkg));
    cpkg->pkg = pkg;
    cpkg->flags = flags;
    return cpkg;
}

int cnflpkg_cmp(struct cnflpkg *p1, struct cnflpkg *p2)
{
    return (size_t)p1->pkg - (size_t)p2->pkg;
}


static 
int pkgset_verify_conflicts(struct pkgset *ps, int mmode) 
{
    int i, j;

    msg(1, "$Verifying conflicts...\n");
    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg;

        pkg = n_array_nth(ps->pkgs, i);
        if (pkg->cnfls == NULL)
            continue;
        
        n_assert(n_array_size(pkg->cnfls));
        msg(4, "%d. %s\n", i, pkg_snprintf_s(pkg));
        for (j=0; j<n_array_size(pkg->cnfls); j++) {
            const struct capreq_idx_ent *ent;
            struct capreq *cnfl;
            char *cnflname;

            cnfl = n_array_nth(pkg->cnfls, j);
            cnflname = capreq_name(cnfl);
            
            if ((ent = capreq_idx_lookup(&ps->cap_idx, cnflname))) {
                if (setup_cnfl_pkgs(pkg, cnfl, mmode,
                                   (struct pkg **)ent->pkgs, ent->items)) {
                    continue;
                }
                
            } else {
                msg(4, " cnfl %-35s --> NOT FOUND\n",capreq_snprintf_s(cnfl));
            }
        }
    }

    if (verbose > 1) {
        int j;
        for (i=0; i<n_array_size(ps->pkgs); i++) {
            struct pkg *pkg = n_array_nth(ps->pkgs, i);
            if (pkg->cnflpkgs == NULL)
                continue;
            msg(2, "%s -> ", pkg_snprintf_s(pkg)); 
            for (j=0; j<n_array_size(pkg->cnflpkgs); j++) {
                struct cnflpkg *cpkg = n_array_nth(pkg->cnflpkgs, j);
                msg(2, "_%s%s, ", cpkg->flags & CNFLPKG_OB ? "*":"", 
                    pkg_snprintf_s(cpkg->pkg));
            }
            msg(2, "_\n");
        }
    }
    
    return 1;
}


static
int setup_cnfl_pkgs(struct pkg *pkg, struct capreq *cnfl, int strict,
                    struct pkg *suspkgs[], int npkgs)
{
    int i, nmatch = 0;
    int isobsl = cnfl_is_obsl(cnfl) ? CNFLPKG_OB : 0;
    
    msg(4, " cnfl %-35s --> ",  capreq_snprintf_s(cnfl));
    n_assert(npkgs > 0);
    
    for (i = 0; i < npkgs; i++) {
        struct pkg *spkg = suspkgs[i];
        struct cnflpkg *cnflpkg;
    
        if (capreq_has_ver(cnfl))  /* check version */
            if (!pkg_match_req(spkg, cnfl, strict)) 
                continue;
            
        /* do not conflict with myself */
        if (spkg == pkg) 
            continue;
        
        msg(4, "_%s, ", pkg_snprintf_s(spkg));

        cnflpkg = NULL;
        if (pkg->cnflpkgs) {
            struct cnflpkg tmp_spkg = { spkg, 0 };
            cnflpkg = n_array_bsearch(pkg->cnflpkgs, &tmp_spkg);
        }
        
        if (cnflpkg != NULL) {
            if (cnfl_is_obsl(cnfl)) 
                cnflpkg->flags |= CNFLPKG_OB;
            
        } else {
            cnflpkg = cnflpkg_new(spkg, isobsl);
            if (pkg->cnflpkgs == NULL) 
                pkg->cnflpkgs = n_array_new(n_array_size(pkg->cnfls)/2+2, NULL,
                                            (tn_fn_cmp)cnflpkg_cmp);

            if (cnfl_is_obsl(cnfl))
                cnflpkg->flags |= CNFLPKG_OB;
            
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

#if 0
tn_array *pspkg_obsoletedby(struct pkgset *ps, struct pkg *pkg, int bymarked)
{
    const struct capreq_idx_ent *ent;
    tn_array *obspkgs = NULL;
    
    if ((ent = capreq_idx_lookup(&ps->obs_idx, pkg->name))) {
        int i;
        for (i=0; i<ent->items; i++) {
            if (pkg_obsoletes_pkg(ent->pkgs[i], pkg)) {
                if (bymarked && !pkg_is_marked(pkg))
                    continue;

                if (obspkgs == NULL)
                    obspkgs = n_array_new(2, NULL, pkg_cmp_name_evr);
                n_array_push(obspkgs, pkg);
            }
        }
    }
    
    return obspkgs;
}
#endif
