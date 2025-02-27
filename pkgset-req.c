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
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <trurl/trurl.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"
#include "pkgmisc.h"
#include "capreq.h"
#include "fileindex.h"

extern int poldek_conf_MULTILIB;
extern void *pkg_na_malloc(struct pkg *pkg, size_t size);

tn_array *pkgset_search_provdir(struct pkgset *ps, const char *dir);

static int psreq_lookup(struct pkgset *ps, const struct capreq *req,
                        struct pkg ***suspkgs, struct pkg **pkgsbuf, int *npkgs);


static void isort_pkgs(struct pkg *pkgs[], size_t size)
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
  - if req is rpmlib() et consores, set npkgs to zero
  - otherwise suspkgs is pointed to array of "suspect" packages,
    Suspected packages are sorted descending by name and EVR.

*/
static int psreq_lookup(struct pkgset *ps, const struct capreq *req,
                        struct pkg ***suspkgs, struct pkg **pkgsbuf, int *npkgs)
{
    const struct capreq_idx_ent *ent;
    const char *reqname;
    int matched, pkgsbuf_size;

    reqname = capreq_name(req);
    pkgsbuf_size = *npkgs;
    *npkgs = 0;
    matched = 0;

    pkgset__index_caps(ps);
    if ((ent = capreq_idx_lookup(&ps->cap_idx, reqname, capreq_name_len(req)))) {
        *suspkgs = (struct pkg **)ent->pkgs;
        *npkgs = ent->items;
        matched = 1;

    } else if (capreq_is_file(req)) {
        int n = file_index_lookup(ps->file_idx, reqname, 0, pkgsbuf, pkgsbuf_size);

        n_assert(n >= 0);
        if (n) {
            *npkgs = n;
            matched = 1;
            *suspkgs = pkgsbuf;

        } else {                /* n is 0 */
            tn_array *pkgs;
            if ((pkgs = pkgset_search_provdir(ps, reqname))) {
                int i;
                n = 0;

                for (i=0; i < n_array_size(pkgs); i++) {
                    pkgsbuf[n++] = n_array_nth(pkgs, i);
                    if (n == pkgsbuf_size)
                        break;
                }

/* XXX: TOFIX: pkgsbuf is not free()d by caller, so pkg _refcnts must
   be decreased here */
#if 0
                while (n_array_size(pkgs)) {
                    pkgsbuf[n++] = n_array_shift(pkgs);
                    if (n == pkgsbuf_size)
                        break;
                }
#endif
                *npkgs = n;
                if (n) {
                    matched = 1;
                    *suspkgs = pkgsbuf;
                }
                n_array_free(pkgs);
            }
        }
    }

    /* disabled - well tested
      if (strncmp("rpmlib", capreq_name(req), 6) == 0 && !capreq_is_rpmlib(req))
         n_assert(0);
    */

    if (capreq_is_rpmlib(req) && matched) {
        int i;

        for (i=0; i<*npkgs; i++) {
            if (strcmp((*suspkgs)[i]->name, "rpm") != 0) {
                logn(LOGERR, _("%s: provides rpmlib cap \"%s\""),
                     pkg_id((*suspkgs)[i]), reqname);
                matched = 0;
            }
        }

        *suspkgs = NULL;
        *npkgs = 0;
    }

    if (!matched && pkgset_pm_satisfies(ps, req)) {
        matched = 1;
        msgn(4, _(" req %-35s --> PM_CAP"), capreq_snprintf_s(req));

        *suspkgs = NULL;
        *npkgs = 0;
    }

    return matched;
}

static int psreq_match_pkgs(const struct pkg *pkg, const struct capreq *req,
                            bool strict,
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

        msg(4, "_%s, ", pkg_id(spkg));
        nmatch++;

        if (pkg && spkg != pkg) { /* do not add itself (pkg may be NULL) */
            matches[n++] = spkg;

        } else {
            n = 0;
            break;
            //log(LOGERR, "%s: requires itself\n", pkg_id(pkg));
        }
    }

    if (n > 1)
        isort_pkgs(matches, n);

    msg(4, nmatch ? "\n" : "_UNMATCHED\n");

    *nmatched = n;
    return nmatch;
}

/* find packages satisfies req and (optionally) best fitted to pkg */
int pkgset_find_match_packages(struct pkgset *ps,
                               const struct pkg *pkg, const struct capreq *req,
                               tn_array **packages, bool strict)
{
    struct pkg **suspkgs, pkgsbuf[1024], **matches;
    int nsuspkgs = 0, nmatches = 0, found = 0;

    nsuspkgs = 1024;            /* size of pkgsbuf */
    found = psreq_lookup(ps, req, &suspkgs, (struct pkg **)pkgsbuf, &nsuspkgs);

    if (!found)
        return found;

    if (nsuspkgs == 0)          /* self match or rpmlib() or other internal caps */
        return found;

#if ENABLE_TRACE
    do {
        int i;
        DBGF("%s: found %d suspected packages: ", capreq_snprintf_s(req), nsuspkgs);
        for (i=0; i < nsuspkgs; i++)
            msg(0, "%s, ", pkg_id(suspkgs[i]));
        msg(0, "\n");
    } while(0);
#endif

    found = 0;
    matches = alloca(sizeof(*matches) * nsuspkgs);

    if (psreq_match_pkgs(pkg, req, strict, suspkgs, nsuspkgs, matches, &nmatches)) {
        found = 1;

        if (nmatches && packages) {
            int i;

            if (*packages == NULL)
                *packages = pkgs_array_new(nmatches);

            for (i=0; i < nmatches; i++)
                n_array_push(*packages, pkg_link(matches[i]));
        }
    }

    return found;
}
