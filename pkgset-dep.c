/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <trurl/trurl.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "capreq.h"
#include "capreqidx.h"
#include "pkg.h"
#include "pkgset.h"

void *pkg_na_malloc(const struct pkg *pkg, size_t size);

static struct pkg_unreq *pkg_unreq_new(struct capreq *req, int mismatch)
{
    struct pkg_unreq *unreq;
    char s[512];
    int n;

    n = capreq_snprintf(s, sizeof(s), req);

    unreq = n_malloc(sizeof(*unreq) + n + 1);
    unreq->mismatch = mismatch;
    memcpy(unreq->req, s, n + 1);
    return unreq;
}

static int add_unreq(tn_hash *unreqh, const struct pkg *pkg, struct capreq *req, int mismatch)
{
    tn_array *unreqs;
    const char *id = pkg_id(pkg);

    if ((unreqs = n_hash_get(unreqh, id)) == NULL) {
        unreqs = n_array_new(2, free, NULL);
        n_hash_insert(unreqh, id, unreqs);
    }

    n_array_push(unreqs, pkg_unreq_new(req, mismatch));
    return 1;
}


static
struct reqpkg *reqpkg_new(struct pkg *pkg, struct capreq *req, uint8_t flags, int nadds)
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

static int reqpkg_cmp(struct reqpkg *p1, struct reqpkg *p2)
{
    return pkg_cmp_id(p1->pkg, p2->pkg);
}

static tn_array *reqpkgs_array_new(int size)
{
    tn_array *re = n_array_new(size, NULL, (tn_fn_cmp)reqpkg_cmp);
    n_array_ctl(re, TN_ARRAY_AUTOSORTED);

    return re;
}

static
int add_reqpkg(tn_array *reqpkgs, struct capreq *req, struct pkg *pkg)
{
    struct reqpkg tmp = { NULL, NULL, 0 };
    struct reqpkg *rpkg;

    tmp.pkg = pkg;

    rpkg = n_array_bsearch(reqpkgs, &tmp);
    if (rpkg == NULL) {
        rpkg = reqpkg_new(pkg, req, 0, 0);
        n_array_push(reqpkgs, rpkg);

        //n_array_isort(di->reqpkgs);
        //if (pkg->revreqpkgs == NULL)
        //    pkg->revreqpkgs = n_array_new(2, NULL, (tn_fn_cmp)pkg_cmp_id);
        //n_array_push(dpkg->revreqpkgs, pkg);
    }

    if (capreq_is_prereq(req))
        rpkg->flags |= REQPKG_PREREQ;

    if (capreq_is_prereq_un(req))
        rpkg->flags |= REQPKG_PREREQ_UN;

    return 1;
}

static
int process_req(tn_array *reqpkgs, struct capreq *req, tn_array *matches)
{
    uint8_t flags;
    struct reqpkg *rpkg;
    struct reqpkg tmp_rpkg = {NULL, NULL, 0};

    n_assert(matches);

    if (n_array_size(matches) == 0)          /* selfmatched */
        return 1;

    if (n_array_size(matches) == 1)
        return add_reqpkg(reqpkgs, req, n_array_nth(matches, 0));

    flags = 0;
    flags |= capreq_is_prereq(req) ? REQPKG_PREREQ : 0;
    flags |= capreq_is_prereq_un(req) ? REQPKG_PREREQ_UN : 0;

    tmp_rpkg.pkg = n_array_nth(matches, 0);
    rpkg = n_array_bsearch(reqpkgs, &tmp_rpkg);

    int isneq = 1;
    /* compare the list */
    if (rpkg != NULL && rpkg->flags & REQPKG_MULTI) {
        int i = 0;
        isneq = 0;
        while (rpkg->adds[i] != NULL) {
            if (i+1 >= n_array_size(matches)) {   /* different length */
                isneq = 1;
                break;
            }

            if (rpkg->adds[i]->pkg != n_array_nth(matches, i + 1)) {
                isneq = 1;
                break;
            }
            i++;
        }
    }

    if (isneq) {
        struct pkg *pkg;

        pkg = n_array_nth(matches, 0);
        rpkg = reqpkg_new(pkg, req, flags | REQPKG_MULTI, n_array_size(matches) - 1);
        n_array_push(reqpkgs, rpkg);

        for (int i=1; i < n_array_size(matches); i++) {
            pkg = n_array_nth(matches, i);
            rpkg->adds[i - 1] = reqpkg_new(pkg, req, flags, 0);
        }
    }

    return 1;
}

static
int process_reqs(int indent, tn_array *reqpkgs, struct pkgset *ps, const struct pkg *pkg,
                 tn_hash *cache, bool strict, tn_hash *unreqh)
{
    int nerrors = 0;

    msgn_i(4, indent, "pkg %s", pkg_id(pkg));
    indent++;

    if (pkg->reqs == NULL)
        return 0;

    for (int i=0; i < n_array_size(pkg->reqs); i++) {
        struct capreq *req = n_array_nth(pkg->reqs, i);
        if (capreq_is_rpmlib(req))
            continue;

        const char *streq = capreq_stra(req);
        tn_array *matches = NULL;
        char key[512];
        uint32_t khash;
        int klen;

        //klen = n_snprintf(key, sizeof(key), "%s:%s", pkg_id(pkg), capreq_stra(req));
        klen = n_snprintf(key, sizeof(key), "%s", capreq_stra(req));
        khash = n_hash_compute_hash(cache, key, klen);
        if (n_hash_hexists(cache, streq, klen, khash)) {
            matches = n_hash_hget(cache, streq, klen, khash);

        } else {
            int found = pkgset_find_match_packages(ps, pkg, req, &matches, strict);
            if (found && matches == NULL)
                matches = pkgs_array_new(2);

            n_hash_hinsert(cache, streq, klen, khash, matches);
        }

        if (matches == NULL) { /* not found / unmatched */
            goto l_err_notfound;
        }

        if (n_array_size(matches) == 0) { /* selfmatches */
            msgn_i(4, indent, _(" req %-35s --> self/ internal"), streq);
            continue;

        } else {
            msg_i(4, indent, " req %-35s --> ", streq);
            for (int ii=0; ii < n_array_size(matches); ii++)
                msg_i(4, indent, "_%s, ", pkg_id(n_array_nth(matches, ii)));
            msg_i(4, indent, "_\n");
        }

        if (process_req(reqpkgs, req, matches))
            continue;

        goto l_err_match;   /* found but not fit */

    l_err_notfound:
        nerrors++;
        if (unreqh)
            add_unreq(unreqh, pkg, req, false);
        continue;

    l_err_match:
        nerrors++;
        if (unreqh)
            add_unreq(unreqh, pkg, req, true);
    }

    return nerrors;
}

tn_array *pkgset_get_required_packages_x(int indent, struct pkgset *ps,
                                         const struct pkg *pkg, tn_hash **unreqh)
{
    if (pkg->reqs == NULL) {
        return NULL;
    }

    if (ps->_depinfocache == NULL) {
        ps->_depinfocache = n_hash_new(n_array_size(ps->pkgs), (tn_fn_free)n_array_free);
    }

    if (unreqh && *unreqh == NULL) {
        *unreqh = n_hash_new(128, (tn_fn_free)n_array_free);
    }

    pkgset__index_caps(ps);

    tn_array *reqpkgs = reqpkgs_array_new(n_array_size(pkg->reqs)/2+2);
    process_reqs(indent, reqpkgs, ps, pkg, ps->_depinfocache, true,
                 unreqh ? *unreqh : NULL);

    if (n_array_size(reqpkgs) == 0) {
        n_array_cfree(&reqpkgs);
    }

    return reqpkgs;
}

tn_array *pkgset_get_required_packages(int indent, struct pkgset *ps,
                                       const struct pkg *pkg)
{
    return pkgset_get_required_packages_x(indent, ps, pkg, NULL);
}


static
tn_array *get_conflicted(int indent, struct pkgset *ps,
                         const struct pkg *pkg, struct capreq *cnfl, tn_array *re)
{
    const struct capreq_idx_ent *ent;
    const char *cnflname = capreq_name(cnfl);

    pkgset__index_caps(ps);

    if ((ent = capreq_idx_lookup(&ps->cap_idx, cnflname, capreq_name_len(cnfl)))) {
        struct pkg **suspkgs = (struct pkg **)ent->pkgs;
        int nmatch = 0;
        msg_i(4, indent, "cnfl %-35s --> ",  capreq_snprintf_s(cnfl));

        for (unsigned i = 0; i < ent->items; i++) {
            struct pkg *spkg = suspkgs[i];
            msg_i(4, indent, "sus %s->%s", pkg_id(pkg), pkg_id(spkg));
            /* bastard conflicts are direct */
            if (capreq_is_bastard(cnfl) && pkg_cmp_name(pkg, spkg) != 0)
                continue;

            if (capreq_has_ver(cnfl))  /* check version */
                if (!pkg_match_req(spkg, cnfl, 1 /* strict */))
                    continue;

            /* do not conflict with myself */
            if (spkg == pkg)
                continue;

            /* multilib */
            if (pkg_cmp_name_evr(spkg, pkg) == 0 && pkg_cmp_arch(spkg, pkg) != 0)
                continue;

            msg_i(4, indent, "_%s, ", pkg_id(spkg));

            struct reqpkg *cnflpkg = NULL;
            if (re) {
                struct reqpkg tmp_spkg = { spkg, NULL, 0 };
                cnflpkg = n_array_bsearch(re, &tmp_spkg);
            } else {
                re = reqpkgs_array_new(4);
            }

            if (cnflpkg != NULL) {
                if (capreq_is_obsl(cnfl))
                    cnflpkg->flags |= REQPKG_OBSOLETE;

            } else {
                cnflpkg = reqpkg_new(spkg, cnfl, REQPKG_CONFLICT, 0);
                if (capreq_is_obsl(cnfl))
                    cnflpkg->flags |= REQPKG_OBSOLETE;

                n_array_push(re, cnflpkg);
            }
            nmatch++;
        }

        if (nmatch == 0)
            msg_i(4, indent, "_UNMATCHED\n");
        else
            msg_i(4, indent, "\n");
    }

    return re;
}

tn_array *pkgset_get_conflicted_packages(int indent, struct pkgset *ps, const struct pkg *pkg)
{
    if (pkg->cnfls == NULL) {
        return NULL;
    }

    tn_array *re = NULL;
    for (int i=0; i < n_array_size(pkg->cnfls); i++) {
        struct capreq *cnfl = n_array_nth(pkg->cnfls, i);
        //if (capreq_is_obsl(cnfl)) // XXX
        //    continue;

        re = get_conflicted(indent, ps, pkg, cnfl, re);
    }

    if (re && n_array_size(re) == 0)
        n_array_cfree(&re);

    return re;
}


static
tn_array *get_reqby(int indent, struct pkgset *ps,
                    const struct pkg *pkg, struct capreq *cap, tn_array *re)
{
    const struct capreq_idx_ent *ent;
    const char *capname = capreq_name(cap);

    pkgset__index_reqs(ps);

    if ((ent = capreq_idx_lookup(&ps->req_idx, capname, capreq_name_len(cap)))) {
        struct pkg **suspkgs = (struct pkg **)ent->pkgs;
        int nmatch = 0;
        msg_i(4, indent, "cap %-35s --> ",  capreq_snprintf_s(cap));

        for (unsigned i = 0; i < ent->items; i++) {
            struct pkg *spkg = suspkgs[i];
            msg_i(4, indent, "sus %s->%s", pkg_id(pkg), pkg_id(spkg));

            if (capreq_has_ver(cap))  /* check version */
                if (!pkg_match_req(pkg, cap, 1 /* strict */))
                    continue;

            /* do not require myself */
            if (spkg == pkg)
                continue;

            /* multilib */
            if (pkg_cmp_name_evr(spkg, pkg) == 0 && pkg_cmp_arch(spkg, pkg) != 0)
                continue;

            msg_i(4, indent, "_%s, ", pkg_id(spkg));

            struct pkg *rrpkg = NULL;
            if (re) {
                rrpkg = n_array_bsearch(re, spkg);
            } else {
                re = pkgs_array_new(4);
            }

            if (rrpkg == NULL) {
                n_array_push(re, pkg_link(spkg));
            }
            nmatch++;
        }

        if (nmatch == 0)
            msg_i(4, indent, "_UNMATCHED\n");
        else
            msg_i(4, indent, "\n");
    }

    return re;
}

tn_array *pkgset_get_requiredby_packages(int indent, struct pkgset *ps,
                                         const struct pkg *pkg)
{
    tn_array *re = NULL;

    pkg_add_selfcap((struct pkg*)pkg); /* XXX */
    for (int i=0; i < n_array_size(pkg->caps); i++) {
        struct capreq *cap = n_array_nth(pkg->caps, i);
        re = get_reqby(indent, ps, pkg, cap, re);
    }

    if (re && n_array_size(re) == 0)
        n_array_cfree(&re);

    return re;
}
