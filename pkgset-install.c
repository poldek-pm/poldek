/*
  Copyright (C) 2000 - 2004 Pawel A. Gajda <mis@k2.net.pl>

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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fnmatch.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/nlist.h>
#include <trurl/nmalloc.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>


#include <vfile/vfile.h>
#include <vfile/p_open.h>

#include <sigint/sigint.h>

#define ENABLE_TRACE 0
#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgdir/pkgdir.h"
#include "pkgset.h"
#include "pkgmisc.h"
#include "misc.h"
#include "pkgset-req.h"
#include "arg_packages.h"
#include "dbpkgset.h"
#include "dbdep.h"
#include "poldek_term.h"
#include "poldek.h"
#include "poldek_intern.h"
#include "pm/pm.h"

#define DBPKG_ORPHANS_PROCESSED   (1 << 15) /* is its orphan processed ?*/
#define DBPKG_DEPS_PROCESSED      (1 << 16) /* is its deps processed? */
#define DBPKG_TOUCHED             (1 << 17)

#define PROCESS_AS_NEW        (1 << 0)
#define PROCESS_AS_ORPHAN     (1 << 1)

struct upgrade_s {
    tn_array       *avpkgs;
    tn_array       *install_pkgs;     /* pkgs to install */
    
    tn_hash        *db_deps;          /* cache of resolved db dependencies */

    struct dbpkg_set *uninst_set;
    struct pkgmark_set *dbpms;
    struct pkgmark_set *unmetpms;     /* to mark pkgs with unmet dependencies */
    
    tn_array       *orphan_dbpkgs;    /* array of orphaned dbpkg*s */
    
    int            strict;
    int            ndberrs;
    int            ndep;
    int            ninstall;

    int            nerr_dep;
    int            nerr_cnfl;
    int            nerr_dbcnfl;
    int            nerr_fatal;
    
    struct poldek_ts  *ts;

    tn_hash        *db_pkgs;    /* used by mapfn_mark_newer_pkg() */
    int             nmarked;

    tn_array       *pkg_stack;  /* stack for current processed packages  */
};

int do_poldek_ts_install(struct poldek_ts *ts, struct poldek_iinf *iinf);

static
int process_pkg_conflicts(int indent, struct pkg *pkg,
                          struct pkgset *ps, struct upgrade_s *upg);

static
int process_pkg_deps(int indent, struct pkg *pkg,
                     struct pkgset *ps, struct upgrade_s *upg, int process_as);

static
int select_best_pkg(const struct pkg *marker,
                    struct pkg **candidates, int npkgs,
                    struct pkgset *ps, struct upgrade_s *upg);

static
int pkg_drags(struct pkg *pkg, struct pkgset *ps, struct upgrade_s *upg);

/* anyone of pkgs is marked? */
static inline int one_is_marked(struct pkgmark_set *pms, struct pkg *pkgs[],
                                int npkgs)
{
    int i;

    for (i=0; i < npkgs; i++) 
        if (pkg_is_marked(pms, pkgs[i])) 
            return 1;
    
    return 0;
}

/* RET: 0 - not installable,  1 - installable,  -1 - something wrong */
static
int is_installable(struct pkg *pkg, struct poldek_ts *ts, int is_hand_marked) 
{
    int cmprc = 0, npkgs, install = 1, freshen = 0, force;

    freshen = ts->getop(ts, POLDEK_OP_FRESHEN);
    force = ts->getop(ts, POLDEK_OP_FORCE);
    npkgs = pkgdb_is_pkg_installed(ts->db, pkg, &cmprc);
    
    if (npkgs < 0) 
        die();

    n_assert(npkgs >= 0);

    install = 1;
    
    if (npkgs == 0) {
        if (is_hand_marked && freshen)
            install = 0;
        
    } else if (is_hand_marked && npkgs > 1 &&
               poldek_ts_issetf(ts, POLDEK_TS_UPGRADE) && force == 0) {
        logn(LOGERR, _("%s: multiple instances installed, give up"), pkg->name);
        install = -1;
        
    } else {
        if (pkg_is_scored(pkg, PKG_HELD)) {
            logn(LOGERR, _("%s: refusing to upgrade held package"),
                pkg_snprintf_s(pkg));
            install = 0;
            
        } else if (cmprc <= 0 && force == 0 &&
                   (poldek_ts_issetf(ts, POLDEK_TS_UPGRADE) || cmprc == 0)) {
            char *msg = "%s: %s version installed, %s";
            char *eqs = cmprc == 0 ? "equal" : "newer";
            char *skiped =  "skipped";
            char *giveup =  "give up";

            install = 0;
            
            if (cmprc == 0 && poldek_ts_issetf(ts, POLDEK_TS_REINSTALL)) {
                install = 1;
                
            } else if (cmprc < 0 && poldek_ts_issetf(ts, POLDEK_TS_DOWNGRADE)) {
                install = 1;
            
            } else if (!is_hand_marked) {
                logn(LOGERR, msg, pkg_snprintf_s(pkg), eqs, giveup);
                install = -1;
                
            } else if (is_hand_marked && !freshen) { /* msg without "freshen" */
                msgn(0, msg, pkg_snprintf_s(pkg), eqs, skiped);
                
            }
        }
    }

    return install;
}

static
struct pkg *select_supersede_pkg(const struct pkg *pkg, struct pkgset *ps,
                                 struct upgrade_s *upg) 
{
    const struct capreq_idx_ent *ent;
    struct pkg *bypkg = NULL;
    
    if ((ent = capreq_idx_lookup(&ps->obs_idx, pkg->name))) {
        int i, best_i;
        struct pkg **ent_pkgs = (struct pkg**)ent->crent_pkgs;
        best_i = select_best_pkg(pkg, ent_pkgs, ent->items, ps, upg);

        for (i=best_i; i < ent->items; i++) {
            if (strcmp(pkg->name, ent_pkgs[i]->name) == 0)
                continue;
            DBGF("found %s <- %s, %d, %d\n", pkg_snprintf_s(pkg),
                 pkg_snprintf_s0(ent_pkgs[i]),
                 pkg_caps_obsoletes_pkg_caps(ent_pkgs[i], pkg), 
                 pkg_caps_obsoletes_pkg_caps(pkg, ent_pkgs[i]));
            
            if (pkg_caps_obsoletes_pkg_caps(ent_pkgs[i], pkg) &&
                !pkg_caps_obsoletes_pkg_caps(pkg, ent_pkgs[i])) {
                
                bypkg = ent_pkgs[i];
                break;
            }
        }
    }
    DBGF("%s -> %s\n", pkg_snprintf_s(pkg), bypkg ? pkg_snprintf_s(bypkg) : "NONE");
    return bypkg;
}

static
int other_version_marked(struct pkgmark_set *pms, struct pkg *pkg,
                         tn_array *pkgs, struct capreq *req)
{
    int i;
    
    n_array_sort(pkgs);
    i = n_array_bsearch_idx_ex(pkgs, pkg, (tn_fn_cmp)pkg_cmp_name); 
    if (i < 0)
        return 0;

    DBGF("%s %d\n", pkg_snprintf_s0(pkg), i);
    for (; i < n_array_size(pkgs); i++) {
        struct pkg *p = n_array_nth(pkgs, i);

        if (strcmp(p->name, pkg->name) != 0)
            break;
        
        if (p != pkg && pkg_is_marked(pms, p)) {
            if (req == NULL || pkg_satisfies_req(p, req, 0)) {
                DBGF("%s -> yes, %s\n", pkg_snprintf_s0(pkg),
                     pkg_snprintf_s1(p));
                return 1;
            }
        }
    }

    return 0;
}


static
struct pkg *select_pkg(const char *name, tn_array *pkgs,
                       struct upgrade_s *upg)
{
    struct pkg tmpkg, *curr_pkg, *pkg, *selected_pkg;
    char prefix1[128], prefix2[128], *p;
    int i;
    
    tmpkg.name = (char*)name;

    n_array_sort(pkgs);
    i = n_array_bsearch_idx_ex(pkgs, &tmpkg, (tn_fn_cmp)pkg_cmp_name);
    DBGF("%s -> %d\n", name, i);
    if (i < 0)
        return NULL;

    selected_pkg = NULL;
    pkg = n_array_nth(pkgs, i);
    curr_pkg = n_array_nth(upg->pkg_stack, n_array_size(upg->pkg_stack) - 1);
    

    snprintf(prefix1, sizeof(prefix1), "%s", name);
    if ((p = strchr(prefix1, '-')))
        *p = '\0';

    snprintf(prefix2, sizeof(prefix2), "%s", curr_pkg->name);
    if ((p = strchr(prefix2, '-')))
        *p = '\0';

    DBGF("current pkg %s, name = %s, p1, p2 = %s, %s\n", pkg_snprintf_s(curr_pkg), name,
           prefix1, prefix2);
    
    if (strcmp(prefix1, prefix2) != 0)
        return pkg;
    
    for (; i < n_array_size(pkgs); i++) {
        struct pkg *p = n_array_nth(pkgs, i);
        if (strcmp(p->name, name) != 0)
            break;
        
        if (pkg_cmp_evr(p, curr_pkg) == 0) {
            if (selected_pkg && pkg_cmp_evr(selected_pkg, curr_pkg) > 0)
                selected_pkg = NULL;
            
            if (selected_pkg == NULL)
                selected_pkg = p;
            DBGF("%s [yes (higher ver)]\n", pkg_snprintf_s(selected_pkg));
            break;
            
        } else if (selected_pkg == NULL && pkg_cmp_ver(p, curr_pkg) == 0) {
            selected_pkg = p;
            DBGF("%s [maybe (evr are eq)]\n", pkg_snprintf_s(selected_pkg));
            
        } else {
            DBGF("%s [no (lower ver)]\n", pkg_snprintf_s(p));
        }
    }

    if (selected_pkg == NULL)
        selected_pkg = pkg;
    
    return selected_pkg;
}


static
int select_best_pkg(const struct pkg *marker,
                    struct pkg **candidates, int npkgs,
                    struct pkgset *ps, struct upgrade_s *upg)
{
    int *ncnfls, i, j, i_best, cnfl_min;
    int i_ver_eq = -1, i_evr_eq = -1;

    DBGF("marker=%s (%d)\n", marker ? pkg_snprintf_s(marker) : "(nil)", npkgs);
    n_assert(npkgs > 0);
    if (npkgs == 1)
        return 0;

    ncnfls = alloca(npkgs * sizeof(*ncnfls));
    for (i=0; i < npkgs; i++)
        ncnfls[i] = 0;
        
    for (i=0; i < npkgs; i++) {
        struct pkg *pkg = candidates[i];

        DBGF("%d. %s %s (color white %d, marked %d, %p)\n", i, 
             marker ? pkg_snprintf_s(marker) : "(nil)", pkg_snprintf_s0(pkg),
             pkg_is_color(pkg, PKG_COLOR_WHITE),
             pkg_is_marked(upg->ts->pms, pkg), pkg);

        if (marker && pkg_eq_name_prefix(marker, pkg)) {
            if (i_evr_eq == -1 && pkg_cmp_evr(marker, pkg) == 0)
                i_evr_eq = i;
            
            if (i_ver_eq == -1 && pkg_cmp_ver(marker, pkg) == 0)
                i_ver_eq = i;
        }
        
        if (pkg->cnflpkgs != NULL)
            for (j = 0; j < n_array_size(pkg->cnflpkgs); j++) {
                struct reqpkg *cpkg = n_array_nth(pkg->cnflpkgs, j);
                if (pkg_is_marked(upg->ts->pms, cpkg->pkg))
                    ncnfls[i]++;
            }
    }

    if (i_evr_eq > -1 && ncnfls[i_evr_eq] == 0)
        return i_evr_eq;

    if (i_ver_eq > -1 && ncnfls[i_ver_eq] == 0)
        return i_ver_eq;

    cnfl_min = INT_MAX;
    i_best = -1;
    for (i=0; i < npkgs; i++) {
        DBGF("%d. %s %d\n", i, pkg_snprintf_s(candidates[i]), ncnfls[i]);
        if (cnfl_min > ncnfls[i]) {
            cnfl_min = ncnfls[i];
            i_best = i;
        }
    }
    
    DBGF("[after cnfls] i_best = %d\n", i_best);
    if (cnfl_min == 0) {
        int n = INT_MAX, *nmarks;
        
        nmarks = alloca(npkgs * sizeof(*nmarks));
        
        for (i=0; i < npkgs; i++) {
            if (other_version_marked(upg->ts->pms, candidates[i],
                                     upg->avpkgs, NULL)) {
                DBGF("%d. %s other version is already marked, skipped\n",
                     i, pkg_snprintf_s(candidates[i]));
                continue;
            }

            nmarks[i] = pkg_drags(candidates[i], ps, upg);
            DBGF("%d. %s -> %d\n", i, pkg_snprintf_s(candidates[i]), nmarks[i]);
            if (n > nmarks[i])
                n = nmarks[i];

            if (n == 0 && ncnfls[i] == 0) {
                i_best = i;
                break;
            }
        }
    }

    if (i_best == -1) 
        i_best = 0;
    DBGF("RET %d. %s\n", i_best, pkg_snprintf_s(candidates[i_best]));
    return i_best;
}


#define FINDREQ_BESTSEL    0
#define FINDREQ_NOBESTSEL  1
/* lookup in pkgset */
static
int do_find_req(const struct pkg *pkg, struct capreq *req,
                struct pkg **best_pkg, struct pkg ***candidates,
                struct pkgset *ps, struct upgrade_s *upg, int nobest)
{
    struct pkg **suspkgs, pkgsbuf[1024];
    int    nsuspkgs = 0, found = 0;

    
    *best_pkg = NULL;
    if (candidates)
        *candidates = NULL;
    
    found = psreq_lookup(ps, req, &suspkgs, (struct pkg **)pkgsbuf, &nsuspkgs);
        
    if (found && nsuspkgs) {
        struct pkg **matches;
        int nmatches = 0;

        found = 0;
        matches = alloca(sizeof(*matches) * nsuspkgs);
        if (psreq_match_pkgs(pkg, req, upg->strict, suspkgs,
                             nsuspkgs, matches, &nmatches)) {
            found = 1;
            
            /* already not marked for upgrade */
            if (nmatches > 0 && !one_is_marked(upg->ts->pms, matches,
                                               nmatches)) {
                int best_i = 0;

                if (nobest == 0 && nmatches > 1)
                    best_i = select_best_pkg(pkg, matches, nmatches, ps, upg);
                *best_pkg = matches[best_i];

                if (other_version_marked(upg->ts->pms, *best_pkg, ps->pkgs,
                                         NULL)) {
                    found = 0;
                    *best_pkg = NULL;
                    
                } else if (nmatches > 1 && candidates) {
                    struct pkg **pkgs;
                    int i;
                    
                    pkgs = n_malloc(sizeof(*pkgs) * (nmatches + 1));
                    for (i=0; i < nmatches; i++)
                        pkgs[i] = matches[i];
                    
                    pkgs[nmatches] = NULL;
                    *candidates = pkgs;
                }
            }
        }
    }
    

    return found;
}

static inline
int find_req(const struct pkg *pkg, struct capreq *req,
             struct pkg **best_pkg, struct pkg ***candidates,
             struct pkgset *ps, struct upgrade_s *upg) 
{
    return do_find_req(pkg, req, best_pkg, candidates, ps, upg, FINDREQ_BESTSEL);
}


static
int installset_provides(const struct pkg *pkg, struct capreq *cap,
                        struct pkgset *ps, struct upgrade_s *upg)
{
    struct pkg *tomark = NULL;

    if (find_req(pkg, cap, &tomark, NULL, ps, upg) && tomark == NULL) {
        //printf("cap satisfied %s\n", capreq_snprintf_s(cap));
        return 1;
    }
    return 0;
}

static
int installset_provides_capn(const struct pkg *pkg, const char *capn,
                             struct pkgset *ps, struct upgrade_s *upg)
{
    struct capreq *cap;
    
    capreq_new_name_a(capn, cap);
    return installset_provides(pkg, cap, ps, upg);
}



#define mark_package(a, b) do_mark_package(a, b, PKGMARK_MARK);

static int do_mark_package(struct pkg *pkg, struct upgrade_s *upg,
                           unsigned mark)
{
    int rc;
    
#if 0
    static struct pkg *rpm = NULL;

    if (strcmp(pkg->name, "rpm") == 0 && strcmp(pkg->ver, "4.1") == 0) {
        rpm = pkg;
    }
    if (rpm)
        log(LOGNOTICE, "DUPA %s(%p): %d\n", pkg_snprintf_s(rpm),
            rpm, pkg_is_marked(rpm));
#endif    
    
    n_assert(pkg_is_marked(upg->ts->pms, pkg) == 0);
    if ((rc = is_installable(pkg, upg->ts,
                             pkg_is_marked_i(upg->ts->pms, pkg))) <= 0) {
        upg->nerr_fatal++; 
        return 0;
    }
    
    DBGF("%s, is_installable = %d\n", pkg_snprintf_s(pkg), rc);
    pkg_unmark_i(upg->ts->pms, pkg);

    n_assert(!pkg_has_unmetdeps(upg->unmetpms, pkg));

    if (rc > 0) {
        if (mark == PKGMARK_MARK) {
            pkg_hand_mark(upg->ts->pms, pkg);
            
        } else {
            pkg_dep_mark(upg->ts->pms, pkg);
            upg->ndep++;
        }
        n_array_push(upg->install_pkgs, pkg);
    }

    return rc >= 0;
}

static
void message_depmark(int indent, const struct pkg *marker,
                     const struct pkg *pkg, 
                     const struct capreq *marker_req, int process_as)
{
    const char *reqstr = _("cap");
    const char *marker_prefix = "";

    
    if (process_as == PROCESS_AS_ORPHAN)
        marker_prefix = _("orphaned ");

    if (capreq_is_cnfl(marker_req))
        reqstr = _("cnfl");
            
    msgn_i(1, indent, _("%s%s marks %s (%s %s)"), marker_prefix, 
          pkg_snprintf_s(marker), pkg_snprintf_s0(pkg),
          reqstr, capreq_snprintf_s(marker_req));
}

static
int marked_for_removal_by_req(struct pkg *pkg, const struct capreq *req,
                              struct upgrade_s *upg)
{
    const struct pkg *ppkg;
    int rc;

    if (pkg_is_rm_marked(upg->ts->pms, pkg))
        return 1;
    
    rc = ((ppkg = dbpkg_set_provides(upg->uninst_set, req)) &&
           pkg_cmp_name_evr(ppkg, pkg) == 0);
    
    if (rc)
        pkg_rm_mark(upg->ts->pms, pkg);
    
    return rc;
}


static
int marked_for_removal(struct pkg *pkg, struct upgrade_s *upg)
{
    if (pkg_is_rm_marked(upg->ts->pms, pkg))
        return 1;

    if (dbpkg_set_has_pkg(upg->uninst_set, pkg))
        pkg_rm_mark(upg->ts->pms, pkg);
    
    return pkg_is_rm_marked(upg->ts->pms, pkg);
}

static
int dep_mark_package(struct pkg *pkg,
                     struct pkg *bypkg, struct capreq *byreq,
                     struct upgrade_s *upg)
{
    if (pkg_has_unmetdeps(upg->unmetpms, pkg)) {
        logn(LOGERR, _("%s: skip follow %s cause it's dependency errors"),
             pkg_snprintf_s(bypkg), pkg_snprintf_s0(pkg));
        
        pkg_set_unmetdeps(upg->unmetpms, bypkg);
        upg->nerr_dep++;
        return 0;
    }

    if (marked_for_removal_by_req(pkg, byreq, upg)) {
        logn(LOGERR, _("%s: dependency loop - "
                       "package already marked for removal"), pkg_snprintf_s(pkg));
        upg->nerr_fatal++; 
        return 0;
    }
    
    return do_mark_package(pkg, upg, PKGMARK_DEP);
}

static
int do_greedymark(int indent, struct pkg *pkg, struct pkg *oldpkg,
                  struct capreq *unresolved_req,
                  struct pkgset *ps, struct upgrade_s *upg) 
{
    
    if (pkg_is_marked(upg->ts->pms, pkg))
        n_assert(0);
    
    if (pkg_cmp_evr(pkg, oldpkg) <= 0)
        return 0;
    
    msgn_i(1, indent, _("greedy upgrade %s to %s-%s (unresolved %s)"),
           pkg_snprintf_s(oldpkg), pkg->ver, pkg->rel,
           capreq_snprintf_s(unresolved_req));

    if (dep_mark_package(pkg, NULL, unresolved_req, upg))
        process_pkg_deps(indent, pkg, ps, upg, PROCESS_AS_NEW);
    return 1;
}



/* add to upg->orphan_dbpkgs packages required by pkg */
static
int process_pkg_orphans(struct pkg *pkg, struct pkgset *ps,
                        struct upgrade_s *upg)
{
    unsigned ldflags = PKG_LDNEVR | PKG_LDREQS;
    int i, k, n = 0;
    struct pkgdb *db;

    if (sigint_reached())
        return 0;
    
    db = upg->ts->db;
    DBGF("%s\n", pkg_snprintf_s(pkg));
    MEMINF("process_pkg_orphans:");

    if (!installset_provides_capn(pkg, pkg->name, ps, upg)) 
        n += pkgdb_get_pkgs_requires_capn(db, upg->orphan_dbpkgs, pkg->name,
                                          upg->uninst_set->dbpkgs, ldflags);
        
    if (pkg->caps)
        for (i=0; i < n_array_size(pkg->caps); i++) {
            struct capreq *cap = n_array_nth(pkg->caps, i);

            if (installset_provides(pkg, cap, ps, upg)) 
                continue;
            
            n += pkgdb_get_pkgs_requires_capn(db, upg->orphan_dbpkgs,
                                              capreq_name(cap),
                                              upg->uninst_set->dbpkgs, ldflags);
            //printf("cap %s\n", capreq_snprintf_s(cap));
        }
    
    if (pkg->fl == NULL)
        return n;
    
    for (i=0; i < n_tuple_size(pkg->fl); i++) {
        struct pkgfl_ent *flent = n_tuple_nth(pkg->fl, i);
        char path[PATH_MAX], *endp;

        endp = path;
        if (*flent->dirname != '/')
            *endp++ = '/';
        
        endp = n_strncpy(endp, flent->dirname, sizeof(path));
        
            
        for (k=0; k < flent->items; k++) {
            struct flfile *file = flent->files[k];
            int path_left_size;
                
            if (*(endp - 1) != '/')
                *endp++ = '/';
                
            path_left_size = sizeof(path) - (endp - path);
            n_strncpy(endp, file->basename, path_left_size);

            if (!installset_provides_capn(pkg, path, ps, upg)) 
                n += pkgdb_get_pkgs_requires_capn(db, upg->orphan_dbpkgs, path,
                                                  upg->uninst_set->dbpkgs,
                                                  ldflags);
        }
    }
    
    return n;
}

static
int verify_unistalled_cap(int indent, struct capreq *cap, struct pkg *pkg,
                          struct pkgset *ps, struct upgrade_s *upg) 
{
    struct db_dep *db_dep;
    struct capreq *req;

    DBGF("VUN %s: %s\n", pkg_snprintf_s(pkg), capreq_snprintf_s(cap));
    if ((db_dep = db_deps_contains(upg->db_deps, cap, 0)) == NULL) {
        DBG("  [1] -> NO in db_deps\n");
        return 1;
    }

    if (db_dep->spkg && pkg_is_marked(upg->ts->pms, db_dep->spkg)) {
        DBG("  [1] -> marked %s\n", pkg_snprintf_s(db_dep->spkg));
        return 1;
    }
    
    DBGF("spkg %s\n", db_dep->spkg ? pkg_snprintf_s(db_dep->spkg) : "NO");
    req = db_dep->req;

    // still satisfied by db? 
    if (pkgdb_match_req(upg->ts->db, req, upg->strict,
                        upg->uninst_set->dbpkgs)) {
        DBG("  [1] -> satisfied by db\n");
        return 1;
    }

    if (db_dep->spkg && installset_provides(NULL, req, ps, upg)) {
        if (poldek_VERBOSE > 1)
            logn(LOGWARN, "cap %s satisfied by install set, shouldn't happen",
                 capreq_snprintf_s(req));
        DBGF("cap %s satisfied by install set\n", capreq_snprintf_s(req));
        return 1;
    }
    
    if (db_dep->spkg && !marked_for_removal_by_req(db_dep->spkg, req, upg) &&
        !other_version_marked(upg->ts->pms, db_dep->spkg, ps->pkgs, req)) {
        struct pkg *marker;
        
        n_assert(n_array_size(db_dep->pkgs));
        marker = n_array_nth(db_dep->pkgs, 0);
        message_depmark(indent, marker, db_dep->spkg, req, PROCESS_AS_ORPHAN);
        if (!dep_mark_package(db_dep->spkg, marker, req, upg))
            return 0;
        return process_pkg_deps(indent, db_dep->spkg, ps, upg, PROCESS_AS_NEW);
    }
    
    if (db_dep->flags & PROCESS_AS_NEW) {
        int i, n;
        char errmsg[4096];

        n = n_snprintf(errmsg, sizeof(errmsg), _("%s is required by "), 
                       capreq_snprintf_s(req));
        
        for (i=0; i < n_array_size(db_dep->pkgs); i++) {
            struct pkg *p = n_array_nth(db_dep->pkgs, i);
        
            n_snprintf(&errmsg[n], sizeof(errmsg) - n, "%s%s",
                       (p->flags & PKG_DBPKG) ? "" : "already marked ", 
                       pkg_snprintf_s(p));
            
            logn(LOGERR, "%s", errmsg);
        }
        
        pkg_set_unmetdeps(upg->unmetpms, pkg);
        upg->nerr_dep++;
            
                
    } else if (db_dep->flags & PROCESS_AS_ORPHAN) {
        int i;
        tn_array *pkgs;

        n_assert(db_dep->pkgs);
        pkgs = n_array_clone(db_dep->pkgs);
        for (i=0; i < n_array_size(db_dep->pkgs); i++) {
            struct pkg *pp = n_array_nth(db_dep->pkgs, i);
            if (n_array_has_free_fn(db_dep->pkgs))
                pp = pkg_link(pp);
            
            n_array_push(pkgs, pp);
        }
        

        for (i=0; i < n_array_size(pkgs); i++) {
            //for (i=0; db_dep->pkgs && i < n_array_size(db_dep->pkgs); i++) {
            struct pkg *opkg = n_array_nth(pkgs, i);
            struct pkg *p;
            int not_found = 1;

            
            if (pkg_cmp_name_evr(opkg, pkg) == 0) /* packages orphanes itself */
                continue;
                
            if ((p = select_pkg(opkg->name, ps->pkgs, upg))) {
                //if (strcmp(p->name, "kdegames") == 0) {
                //    printf("DUPA\n");
                //}
                
                if (pkg_is_marked_i(upg->ts->pms, p))
                    mark_package(p, upg);

                if (pkg_is_marked(upg->ts->pms, p)) {
                    process_pkg_deps(-2, p, ps, upg, PROCESS_AS_NEW);
                    not_found = 0;
                        
                } else if (upg->ts->getop(upg->ts, POLDEK_OP_GREEDY)) {
                    if (do_greedymark(indent, p, opkg, req, ps, upg))
                        not_found = 0;
                }
            }
            
            if (not_found) {
                logn(LOGERR, _("%s (cap %s) is required by %s"),
                     pkg_snprintf_s(pkg), capreq_snprintf_s(req),
                     pkg_snprintf_s0(opkg));
                
                
                pkg_set_unmetdeps(upg->unmetpms, pkg);
                upg->nerr_dep++;
            }
        }
        n_array_free(pkgs);
    }
    

    
    return 1;
}
    

static
void process_pkg_obsl(int indent, struct pkg *pkg, struct pkgset *ps,
                      struct upgrade_s *upg)
{
    tn_array *orphans;
    struct pkgdb *db = upg->ts->db;
    int n, i;
    unsigned getflags = PKGDB_GETF_OBSOLETEDBY_NEVR;
    
    
    if (!poldek_ts_issetf(upg->ts, POLDEK_TS_UPGRADE))
        return;

    if (sigint_reached())
        return;
    
    DBGF("%s\n", pkg_snprintf_s(pkg));

    if (upg->ts->getop(upg->ts, POLDEK_OP_OBSOLETES))
        getflags |= PKGDB_GETF_OBSOLETEDBY_OBSL;

    if (poldek_ts_issetf(upg->ts, POLDEK_TS_DOWNGRADE))
        getflags |= PKGDB_GETF_OBSOLETEDBY_REV;

    n = pkgdb_get_obsoletedby_pkg(db, upg->uninst_set->dbpkgs, pkg, getflags,
                                  PKG_LDWHOLE_FLDEPDIRS);
    
    DBGF("%s, n = %d\n", pkg_snprintf_s(pkg), n);
    if (n == 0)
        return;
    
    n = 0;
    for (i=0; i < n_array_size(upg->uninst_set->dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(upg->uninst_set->dbpkgs, i);
        if (pkgmark_isset(upg->dbpms, dbpkg, DBPKG_TOUCHED))
            continue;
        
        msgn_i(1, indent, _("%s obsoleted by %s"), pkg_snprintf_s(dbpkg),
               pkg_snprintf_s0(pkg));
        pkg_rm_mark(upg->ts->pms, dbpkg);
        db_deps_remove_pkg(upg->db_deps, dbpkg);
        db_deps_remove_pkg_caps(upg->db_deps, pkg,
                                (ps->flags & PSET_DBDIRS_LOADED) == 0);

        pkgmark_set(upg->dbpms, dbpkg, 1, DBPKG_TOUCHED);
	DBGF("verifyuninstalled %s caps\n", pkg_snprintf_s(dbpkg));
        if (dbpkg->caps) {
            int j;
            for (j=0; j < n_array_size(dbpkg->caps); j++) {
                struct capreq *cap = n_array_nth(dbpkg->caps, j);
                verify_unistalled_cap(indent, cap, dbpkg, ps, upg);
            }
        }
	DBGF("verifyuninstalled %s files? => %s \n", pkg_snprintf_s(dbpkg), 
		dbpkg->fl ? "YES" : "NO");
        if (dbpkg->fl) {
            struct capreq *cap;
            int j, k;
            
            cap = alloca(sizeof(cap) + PATH_MAX);
            memset(cap, 0, sizeof(*cap));
            cap->_buf[0] = '\0';
            
            for (j=0; j < n_tuple_size(dbpkg->fl); j++) {
                struct pkgfl_ent *flent = n_tuple_nth(dbpkg->fl, j);
                char *path, *endp;
                int path_left_size;
                
                endp = path = &cap->_buf[1];
                
                // not needed cause depdirs module is used 
                //if (n_array_bsearch(ps->depdirs, flent->dirname) == NULL)
                //    continue;
                
                if (*flent->dirname != '/')
                    *endp++ = '/';
                
                endp = n_strncpy(endp, flent->dirname, PATH_MAX);
                if (*(endp - 1) != '/')
                    *endp++ = '/';
                    
                path_left_size = PATH_MAX - (endp - path);
                for (k=0; k < flent->items; k++) {
                    struct flfile *file = flent->files[k];
                    
                    n_strncpy(endp, file->basename, path_left_size);
                    verify_unistalled_cap(indent, cap, dbpkg, ps, upg);
                }
            }
        }
        n += process_pkg_orphans(dbpkg, ps, upg);
    }

    if (n == 0)
        return;

    orphans = pkgs_array_new(n_array_size(upg->orphan_dbpkgs));
    for (i=0; i<n_array_size(upg->orphan_dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(upg->orphan_dbpkgs, i);
        if (pkgmark_isset(upg->dbpms, dbpkg, DBPKG_DEPS_PROCESSED))
                continue;
        pkgmark_set(upg->dbpms, dbpkg, 1, DBPKG_DEPS_PROCESSED);
        n_array_push(orphans, pkg_link(dbpkg));
    }

    for (i=0; i<n_array_size(orphans); i++) {
        struct pkg *pkg = n_array_nth(orphans, i);
        process_pkg_deps(indent, pkg, ps, upg, PROCESS_AS_ORPHAN);
    }
    n_array_free(orphans);
}

static
int pkg_drags(struct pkg *pkg, struct pkgset *ps, struct upgrade_s *upg)
{
    int i, ntoinstall = 0;

    
    if (upg->nerr_fatal || pkg->reqs == NULL)
        return ntoinstall;
    
    DBGF("start %s\n", pkg_snprintf_s(pkg));
    
    for (i=0; i < n_array_size(pkg->reqs); i++) {
        struct capreq *true_req, *req = NULL;
        struct pkg    *tomark = NULL;
        
        true_req = n_array_nth(pkg->reqs, i);
        
        if (capreq_is_rpmlib(true_req)) 
            continue;

        capreq_new_name_a(capreq_name(true_req), req);
        //req = capreq_new(capreq_name(true_req), 0, 0, 0, 0, 0);
        
        if (do_find_req(pkg, req, &tomark, NULL, ps, upg, FINDREQ_NOBESTSEL)) {
            if (tomark == NULL) /* satisfied by already being installed set */
                continue;
        }
        DBGF("req %s tomark=%s\n", capreq_snprintf_s(true_req),
             tomark ? pkg_snprintf_s(tomark) : "NONE");
        /* cached */
        if (db_deps_provides(upg->db_deps, req, DBDEP_DBSATISFIED)) {
            DBGF("%s: satisfied by db [cached]\n", capreq_snprintf_s(req));
            
        } else if (tomark && marked_for_removal(tomark, upg)) {
            DBGF("%s: marked for removal\n", pkg_snprintf_s(tomark));
            
        } else if (pkgdb_match_req(upg->ts->db, req, 1,
                                   upg->uninst_set->dbpkgs)) {

            DBGF("%s: satisfied by dbX\n", capreq_snprintf_s(req));
            //dbpkg_set_dump(upg->uninst_set);
            //db_deps_add(upg->db_deps, true_req, pkg, tomark,
            //            PROCESS_AS_NEW | DBDEP_DBSATISFIED);
            
        } else if (tomark || tomark == NULL) { /* don't care found or not */
            ntoinstall++;
        }
    }
    DBGF("end %s -> %d\n", pkg_snprintf_s(pkg), ntoinstall);
    return ntoinstall;
}

static
struct pkg *select_successor(const struct pkg *pkg,
                             struct pkgset *ps, struct upgrade_s *upg,
                             int *by_obsoletes)
{
    struct pkg *p = NULL;

    *by_obsoletes = 0;
    p = select_pkg(pkg->name, ps->pkgs, upg);
    if ((p == NULL || pkg_cmp_evr(p, pkg) == 0) &&
        upg->ts->getop(upg->ts, POLDEK_OP_OBSOLETES)) {
        p = select_supersede_pkg(pkg, ps, upg);
        *by_obsoletes = 1;
    }
    return p;
}

static
int process_pkg_reqs(int indent, struct pkg *pkg, struct pkgset *ps,
                     struct upgrade_s *upg, int process_as) 
{
    struct pkg *obspkg;
    int i, obspkg_by_obsoletes, obspkg_checked = 0;

    if (sigint_reached())
        return 0;

    if (upg->nerr_fatal)
        return 0;

    if (pkg->reqs == NULL)
        return 1;

    DBGF("%s, greedy %d\n", pkg_snprintf_s(pkg),
         upg->ts->getop(upg->ts, POLDEK_OP_GREEDY));

    obspkg = NULL;
    obspkg_by_obsoletes = 0;
    if (process_as == PROCESS_AS_ORPHAN &&
        upg->ts->getop(upg->ts, POLDEK_OP_AGGREEDY)) {
        struct pkg *p;
        int ndragged = 0;

        if (pkg_drags(pkg, ps, upg) == 0) {
            p = select_successor(pkg, ps, upg, &obspkg_by_obsoletes);
            if (p && (ndragged = pkg_drags(p, ps, upg)) > 0) {
                DBGF("OMIT select_successor %s -> %s (%d)\n",
                     pkg_snprintf_s(pkg), pkg_snprintf_s0(p), ndragged);
                p = NULL;
                /* it is possible that select_successor() gives equal package version */
            } else if (p == NULL || pkg_cmp_evr(p, pkg) > 0) {
                obspkg = p;
                obspkg_checked = 1; 
            }
        }
    }
    
    for (i=0; i < n_array_size(pkg->reqs); i++) {
        struct capreq *req;
        struct pkg    *tomark = NULL;
        struct pkg    **tomark_candidates = NULL;
        struct pkg    ***tomark_candidates_ptr = NULL;
        char          *reqname;
        
        req = n_array_nth(pkg->reqs, i);
        
        if (capreq_is_rpmlib(req)) {
            if (process_as == PROCESS_AS_NEW && !capreq_is_satisfied(req)) {
                logn(LOGERR, _("%s: rpmcap %s not found, upgrade rpm."),
                     pkg_snprintf_s(pkg), capreq_snprintf_s(req));
                pkg_set_unmetdeps(upg->unmetpms, pkg);
                upg->nerr_dep++;
            }
            continue;
        }

        /* obsoleted by greedy mark */
        if (process_as == PROCESS_AS_ORPHAN && marked_for_removal(pkg, upg)) {
            DBGF("%s: obsoleted, return\n", pkg_snprintf_s(pkg));
            db_deps_remove_pkg(upg->db_deps, pkg);
            return 1;
        }
        
        reqname = capreq_name(req);
        if (capreq_has_ver(req)) {
            reqname = alloca(256);
            capreq_snprintf(reqname, 256, req);
        }

        DBGF("req %s\n", capreq_snprintf_s(req));

        if (upg->ts->getop(upg->ts, POLDEK_OP_EQPKG_ASKUSER) && upg->ts->askpkg_fn)
            tomark_candidates_ptr = &tomark_candidates;
        
        if (find_req(pkg, req, &tomark, tomark_candidates_ptr, ps, upg)) {
            if (tomark == NULL) {
                msg_i(3, indent, "%s: satisfied by install set\n",
                      capreq_snprintf_s(req));
                
                goto l_end_loop;
            }
        }

        DBGF("%s: TOMARK %s\n", pkg_snprintf_s1(pkg),
             tomark ? pkg_snprintf_s0(tomark) : "NULL");

        /* don't check foreign dependencies */
        if (process_as == PROCESS_AS_ORPHAN) {
#if 0   /* buggy,  TODO - unmark foreign on adding to uninst_set */
            if (db_deps_provides(upg->db_deps, req, DBDEP_FOREIGN)) {
                
                msg_i(3, indent, "%s: %s skipped foreign req [cached]\n",
                      pkg_snprintf_s(pkg), reqname);
                goto l_end_loop;
            }
#endif
            /* obspkg !=  NULL => do aggresive upgrade */
            if (!dbpkg_set_provides(upg->uninst_set, req)) {
                msg_i(3, indent, "%s: %s skipped foreign req\n",
                     pkg_snprintf_s(pkg), reqname);
                
                db_deps_add(upg->db_deps, req, pkg, tomark,
                            process_as | DBDEP_FOREIGN);
                goto l_end_loop;
            }
        }
        
        /* cached */
        if (db_deps_provides(upg->db_deps, req, DBDEP_DBSATISFIED)) {
            msg_i(3, indent, "%s: satisfied by db [cached]\n",
                  capreq_snprintf_s(req));
            DBGF("%s: satisfied by db [cached]\n", capreq_snprintf_s(req));
            
            
        } else if (pkgdb_match_req(upg->ts->db, req, upg->strict,
                                   upg->uninst_set->dbpkgs)) {

            DBGF("%s: satisfied by dbY\n", capreq_snprintf_s(req));
            msg_i(3, indent, "%s: satisfied by db\n", capreq_snprintf_s(req));
            //dbpkg_set_dump(upg->uninst_set);
            db_deps_add(upg->db_deps, req, pkg, tomark,
                        process_as | DBDEP_DBSATISFIED);
            
        } else if (obspkg == NULL && tomark &&
                   upg->ts->getop(upg->ts, POLDEK_OP_FOLLOW)) {
            struct pkg *real_tomark = tomark;
            if (tomark_candidates) {
                int n;
                n = upg->ts->askpkg_fn(capreq_snprintf_s(req),
                                         tomark_candidates, tomark);
                real_tomark = tomark_candidates[n];
                free(tomark_candidates);
                tomark_candidates = NULL;
            }
            
            if (marked_for_removal_by_req(real_tomark, req, upg)) {
                logn(LOGERR, _("%s (cap %s) is required by %s%s"),
                     pkg_snprintf_s(real_tomark), capreq_snprintf_s(req),
                     (pkg->flags & PKG_DBPKG) ? "" : " already marked", 
                     pkg_snprintf_s0(pkg));
                upg->nerr_dep++;
                
            } else {
                //printf("DEPM %s\n", pkg_snprintf_s0(tomark));
                message_depmark(indent, pkg, real_tomark, req, process_as);
                if (dep_mark_package(real_tomark, pkg, req, upg)) 
                    process_pkg_deps(indent, real_tomark, ps, upg, PROCESS_AS_NEW);
            }
            
        } else {
            if (process_as == PROCESS_AS_NEW) {
                logn(LOGERR, _("%s: req %s not found"),
                    pkg_snprintf_s(pkg), capreq_snprintf_s(req));
                pkg_set_unmetdeps(upg->unmetpms, pkg);
                upg->nerr_dep++;
                
                
            } else if (process_as == PROCESS_AS_ORPHAN) {
                int not_found = 1, by_obsoletes = 0;
                struct pkg *p;
                
                if (obspkg_checked) {
                    if (obspkg)
                        msg_i(3, indent, "aggresive upgrade %s to %s\n",
                              pkg_snprintf_s(pkg), pkg_snprintf_s0(obspkg));
                    p = obspkg;
                    by_obsoletes = obspkg_by_obsoletes;
                } else {
                    p = select_successor(pkg, ps, upg, &by_obsoletes);
                }
                
#if 0           /* code "moved" to func beginnig cause to aggresive_greedy */
                p = select_pkg(pkg->name, ps->pkgs, upg);
                if (p == NULL && upg->ts->getop(upg->ts, POLDEK_OP_OBSOLETES)) {
                    if (obspkg)
                        p = obspkg;
                    else 
                        p = select_supersede_pkg(pkg, ps);
                    by_obsoletes = 1;
                }
#endif           

                if (p != NULL) {
                    if (pkg_is_marked_i(upg->ts->pms, p) ||
                        (by_obsoletes && !pkg_is_marked(upg->ts->pms, p)))
                        mark_package(p, upg);
                        
                    if (pkg_is_marked(upg->ts->pms, p)) {
                        process_pkg_deps(-2, p, ps, upg, PROCESS_AS_NEW);
                        not_found = 0;
                        
                    } else if (upg->ts->getop(upg->ts, POLDEK_OP_GREEDY)) {
                        n_assert(!pkg_is_marked(upg->ts->pms, p));
                        if (do_greedymark(indent, p, pkg, req, ps, upg))
                            not_found = 0;
                    }
                }

                if (not_found) {
                    logn(LOGERR, _("%s is required by %s"),
                         capreq_snprintf_s(req), pkg_snprintf_s(pkg));
                    pkg_set_unmetdeps(upg->unmetpms, pkg);
                    upg->nerr_dep++;
                }
            }
        }
        
    l_end_loop:
        if (tomark_candidates)
            free(tomark_candidates);
    }

    return 1;
}


static
int process_pkg_deps(int indent, struct pkg *pkg, struct pkgset *ps,
                     struct upgrade_s *upg, int process_as) 
{
    
    n_assert(process_as == PROCESS_AS_NEW || process_as == PROCESS_AS_ORPHAN);
    
    if (upg->nerr_fatal || sigint_reached())
        return 0;

    indent += 2;
    if (!pkg_is_color(pkg, PKG_COLOR_WHITE)) { /* processed */
        //msg_i(1, indent, "CHECKED%s%s dependencies...\n",
        //      process_as == PROCESS_AS_ORPHAN ? " orphaned ":" ",
        //      pkg_snprintf_s(pkg));
        return 0;
    }

#if 0
    {
        
        static struct pkg *rpm = NULL;
        
        if (strcmp(pkg->name, "rpm") == 0 && strcmp(pkg->ver, "4.1") == 0) {
            rpm = pkg;
        }
        if (rpm)
            log(LOGNOTICE, "DD %s(%p): %d\n", pkg_snprintf_s(rpm), rpm, pkg_is_marked(rpm));
    }
    
#endif    

    if (process_as == PROCESS_AS_NEW)
        n_array_push(upg->pkg_stack, pkg);

    DBGF("PROCESSING [%d] %s as %s\n", indent, pkg_snprintf_s(pkg),
         process_as == PROCESS_AS_NEW ? "NEW" : "ORPHAN");
    msg_i(3, indent, "Checking%s%s dependencies...\n",
          process_as == PROCESS_AS_ORPHAN ? " orphaned ":" ",
          pkg_snprintf_s(pkg));

    pkg_set_color(pkg, PKG_COLOR_GRAY); /* dep processed */

    if (process_as == PROCESS_AS_NEW) 
        process_pkg_obsl(indent, pkg, ps, upg);

    if (pkg->reqs)
        process_pkg_reqs(indent, pkg, ps, upg, process_as);

    if (process_as == PROCESS_AS_NEW) {
        process_pkg_conflicts(indent, pkg, ps, upg);
        //process_pkg_obsl(pkg, ps, upg, indent);
    }

    DBGF("END PROCESSING [%d] %s as %s\n", indent, pkg_snprintf_s(pkg),
         process_as == PROCESS_AS_NEW ? "NEW" : "ORPHAN");

    if (process_as == PROCESS_AS_NEW)
        n_array_pop(upg->pkg_stack);
    return 1;
}


static
int find_replacement(struct upgrade_s *upg, struct pkg *pkg, struct pkg **rpkg)
{
    const struct capreq_idx_ent *ent;
    struct pkg *bypkg = NULL;


    *rpkg = NULL;
    
    if ((bypkg = pkgset_lookup_1package(upg->ts->ctx->ps, pkg->name)) &&
        pkg_cmp_name_evr(bypkg, pkg) > 0) {
        
        *rpkg = bypkg;
        
    } else if ((ent = capreq_idx_lookup(&upg->ts->ctx->ps->obs_idx,
                                        pkg->name))) {
        int i;
        
        for (i=0; i < ent->items; i++) {
            if (pkg_caps_obsoletes_pkg_caps(ent->crent_pkgs[i], pkg) &&
                pkg_cmp_name_evr(ent->crent_pkgs[i], pkg) > 0) {
                
                *rpkg = ent->crent_pkgs[i];
                break;
            }
        }
    }

    if (*rpkg && pkg_is_marked(upg->ts->pms, *rpkg)) {
        *rpkg = NULL;
        return 1;
    }
    	
    return (*rpkg != NULL);
}


static
int resolve_conflict(int indent,
                     struct pkg *pkg, const struct capreq *cnfl,
                     struct pkg *dbpkg, struct pkgset *ps,
                     struct upgrade_s *upg) 
{
    struct pkg *tomark = NULL;
    struct capreq *req = (struct capreq*)cnfl;
    int found = 0, by_replacement = 0;

    if (!upg->ts->getop(upg->ts, POLDEK_OP_FOLLOW))
        return 0;

    if (!capreq_versioned(req))
        return 0;
#if 0    
    printf("B %s -> ", capreq_snprintf_s(req));
    capreq_revrel(req);
    printf("%s -> ", capreq_snprintf_s(req));
    capreq_revrel(req);
    printf("%s\n", capreq_snprintf_s(req));
#endif

    DBGF("find_req %s %s\n", pkg_snprintf_s(pkg), capreq_snprintf_s(req));
    capreq_revrel(req);
    DBGF("find_req %s %s\n", pkg_snprintf_s(pkg), capreq_snprintf_s(req));
    
    found = find_req(pkg, req, &tomark, NULL, ps, upg);
    capreq_revrel(req);
    
    if (!found) {
        found = find_replacement(upg, dbpkg, &tomark);
        by_replacement = 1;
    }
    	
    if (!found)
        return 0;
        
    if (tomark == NULL)         /* already in install set */
        return found;

    found = 0;
    
    if (by_replacement || pkg_obsoletes_pkg(tomark, dbpkg)) {
        if (pkg_is_marked_i(upg->ts->pms, tomark)) {
            //msg_i(1, indent, "%s 'MARX' => %s (cnfl %s)\n",
            //      pkg_snprintf_s(pkg), pkg_snprintf_s0(tomark),
            //      capreq_snprintf_s(req));
            found = mark_package(tomark, upg);
            indent = -2;
                
        } else {
            //msg_i(1, indent, "%s 'DEPMARX' => %s (cnfl %s)\n",
            //      pkg_snprintf_s(pkg), pkg_snprintf_s0(tomark),
            //      capreq_snprintf_s(req));
            message_depmark(indent, pkg, tomark, req, PROCESS_AS_NEW);
            found = dep_mark_package(tomark, pkg, req, upg);
        }
        
        if (found)
            process_pkg_deps(indent, tomark, ps, upg, PROCESS_AS_NEW);
    }
    
    return found;
}

/* rpmlib() detects conflicts internally, header*() API usage is too slow */
#undef ENABLE_FILES_CONFLICTS
//#define ENABLE_FILES_CONFLICTS
#ifdef ENABLE_FILES_CONFLICTS
static
int is_file_conflict(const struct pkg *pkg,
                     const char *dirname, const struct flfile *flfile,
                     struct pkg *dbpkg) 
{
    int i, j, is_cnfl = 0;
    
    for (i=0; i < n_array_size(dbpkg->pkg->fl); i++) {
        struct pkgfl_ent *flent = n_array_nth(dbpkg->pkg->fl, i);

        if (strcmp(flent->dirname, dirname) != 0)
            continue;

        for (j=0; j < flent->items; j++) {
            struct flfile *file = flent->files[j];
            if (strcmp(file->basename, flfile->basename) != 0)
                continue;
            printf("is_file_conflict %s %s\n", dirname, flfile->basename);
            if ((is_cnfl = flfile_cnfl(flfile, file, 0)))
                goto l_end;
        }

        
        
    }

 l_end:
    if (is_cnfl) {
        logn(LOGERR, _("%s: /%s/%s%s: conflicts with %s's one"),
            pkg_snprintf_s(pkg),
            dirname, flfile->basename,
            S_ISDIR(flfile->mode) ? "/" : "", dbpkg_snprintf_s(dbpkg));
            
    } else if (verbose > 1) {
        msg(3, "/%s/%s%s: shared between %s and %s\n",
            dirname, flfile->basename,
            S_ISDIR(flfile->mode) ? "/" : "",
            pkg_snprintf_s(pkg), dbpkg_snprintf_s(dbpkg));
    }
    
    return is_cnfl;
}


static 
int find_file_conflicts(struct pkgfl_ent *flent, struct pkg *pkg,
                        struct pkgdb *db, tn_array *uninst_dbpkgs,
                        int strict) 
{
    int i, j, ncnfl = 0;
    
    for (i=0; i<flent->items; i++) {
        tn_array *cnfldbpkgs = NULL;
        char path[PATH_MAX];
        
        snprintf(path, sizeof(path), "/%s/%s", flent->dirname,
                 flent->files[i]->basename);


        //if (strcmp(path, "/usr/X11R6/lib/X11/XErrorDB") == 0)
          
        
        //flent->files[i]->basename
        cnfldbpkgs = rpm_get_file_conflicted_dbpkgs(db->dbh,
                                                    flent->files[i]->basename,
                                                    cnfldbpkgs, 
                                                    uninst_dbpkgs,
                                                    PKG_LDWHOLE_FLDEPDIRS);
        
        printf("** PATH = %s -> %d ", path, cnfldbpkgs ? n_array_size(cnfldbpkgs):0);
        
        if (cnfldbpkgs == NULL)
            cnfldbpkgs = rpm_get_file_conflicted_dbpkgs(db->dbh, path,
                                                        cnfldbpkgs,
                                                        uninst_dbpkgs,
                                                        PKG_LDWHOLE_FLDEPDIRS);

        printf("-> %d\n", cnfldbpkgs ? n_array_size(cnfldbpkgs):0);
        if (cnfldbpkgs == NULL) 
            continue;
        
        for (j=0; j<n_array_size(cnfldbpkgs); j++) {
            struct pkg *dbpkg = n_array_nth(cnfldbpkgs, j);
            printf("CHECK = %s against %s\n", path, dbpkg_snprintf_s(dbpkg));
            ncnfl += is_file_conflict(pkg, flent->dirname, flent->files[i],
                                      n_array_nth(cnfldbpkgs, j));
        }
        
        n_array_free(cnfldbpkgs);
    }
    
    return ncnfl;
}



static
int find_db_files_conflicts(struct pkg *pkg, struct pkgdb *db, struct pkgset *ps, 
                            tn_array *uninst_dbpkgs, int strict)
{
    tn_array *fl;
    int i, ncnfl = 0;

    
    if (pkg->fl)
        for (i=0; i < n_array_size(pkg->fl); i++) {
            ncnfl += find_file_conflicts(n_array_nth(pkg->fl, i), pkg, db,
                                         uninst_dbpkgs, strict);
        }

    if (ncnfl)                  /* skip the rest conflicts test */
        return ncnfl;
    
    n_assert(pkg->pkgdir->vf->vf_stream != NULL);
    
    fseek(pkg->pkgdir->vf->vf_stream, pkg->other_files_offs, SEEK_SET);

    fl = pkgfl_restore_f(pkg->pkgdir->vf->vf_stream, ps->depdirs, 0);
    for (i=0; i < n_array_size(fl); i++) {
        ncnfl += find_file_conflicts(n_array_nth(fl, i), pkg, db,
                                     uninst_dbpkgs, strict);
    }
    
    n_array_free(fl);
    return ncnfl;
}
#endif /* ENABLE_FILES_CONFLICTS */


/* check if cnfl conflicts with db */
static
int find_db_conflicts_cnfl_w_db(int indent,
                                struct pkg *pkg,
                                const struct capreq *cnfl,
                                tn_array *dbpkgs,
                                struct pkgset *ps, struct upgrade_s *upg)
{
    int i, ncnfl = 0;
    tn_hash *ht = NULL;

    msgn_i(4, indent, "Processing conflict %s:%s...", pkg_snprintf_s(pkg),
           capreq_snprintf_s(cnfl));
    
    if (upg->ts->getop(upg->ts, POLDEK_OP_ALLOWDUPS) &&
        n_array_size(dbpkgs) > 1) {
        
        ht = n_hash_new(21, NULL);
        n_hash_ctl(ht, TN_HASH_NOCPKEY);
        
        for (i=0; i<n_array_size(dbpkgs); i++) {
            struct pkg *dbpkg = n_array_nth(dbpkgs, i);
            if (n_hash_exists(ht, dbpkg->name))
                continue;
            
            if (!pkg_match_req(dbpkg, cnfl, 1)) {
                msgn_i(5, indent, "%s: conflict disarmed by %s",
                       capreq_snprintf_s(cnfl), pkg_snprintf_s(dbpkg));
                n_hash_insert(ht, dbpkg->name, pkg);
            }
        }
    }
    
    for (i=0; i<n_array_size(dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(dbpkgs, i);
        
        msg_i(6, indent, "%d. %s (%s) <-> %s ?\n", i, pkg_snprintf_s(pkg),
            capreq_snprintf_s(cnfl), pkg_snprintf_s0(dbpkg));
        
        if (ht && n_hash_exists(ht, dbpkg->name))
            continue;
        
        if (pkg_match_req(dbpkg, cnfl, 1)) {
            if (!resolve_conflict(indent, pkg, cnfl, dbpkg, ps, upg)) {
                logn(LOGERR, _("%s (cnfl %s) conflicts with installed %s"),
                    pkg_snprintf_s(pkg), capreq_snprintf_s(cnfl),
                    pkg_snprintf_s0(dbpkg));
                ncnfl++;
            }
        }
    }

    if (ht)
        n_hash_free(ht);
    
    return ncnfl;
}

/* check if db cnfl conflicts with cap */
static
int find_db_conflicts_dbcnfl_w_cap(int indent,
                                   struct pkg *pkg, const struct capreq *cap,
                                   tn_array *dbpkgs,
                                   struct pkgset *ps, struct upgrade_s *upg) 
{
    int i, j, ncnfl = 0;


    for (i = 0; i<n_array_size(dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(dbpkgs, i);
        
        msg(6, "%s (%s) <-> %s ?\n", pkg_snprintf_s(pkg),
            capreq_snprintf_s(cap), pkg_snprintf_s0(dbpkg));
        
        for (j = 0; j < n_array_size(dbpkg->cnfls); j++) {
            struct capreq *cnfl = n_array_nth(dbpkg->cnfls, j);
            if (cap_match_req(cap, cnfl, 1)) {
                if (!resolve_conflict(indent, pkg, cnfl, dbpkg, ps, upg)) {
                    logn(LOGERR, _("%s (cap %s) conflicts with installed %s (%s)"),
                        pkg_snprintf_s(pkg), capreq_snprintf_s(cap), 
                        pkg_snprintf_s0(dbpkg), capreq_snprintf_s0(cnfl));
                    ncnfl++;
                }
            }
        }
    }
    
    return ncnfl;
}

static
int process_pkg_conflicts(int indent, struct pkg *pkg, struct pkgset *ps,
                          struct upgrade_s *upg)
{
    struct pkgdb *db;
    int i, n, ncnfl = 0;

    db = upg->ts->db;

    if (!upg->ts->getop(upg->ts, POLDEK_OP_CONFLICTS))
        return 1;
    
    /* conflicts in install set */
    if (pkg->cnflpkgs != NULL)
        for (i = 0; i < n_array_size(pkg->cnflpkgs); i++) {
            struct reqpkg *cpkg = n_array_nth(pkg->cnflpkgs, i);

            if (pkg_is_marked(upg->ts->pms, cpkg->pkg)) {
                logn(LOGERR, _("%s conflicts with %s"),
                    pkg_snprintf_s(pkg), pkg_snprintf_s0(cpkg->pkg));
                upg->nerr_cnfl++;
                ncnfl++;
            }
        }

    
    /* conflicts with db packages */

    for (i = 0; i < n_array_size(pkg->caps); i++) {
        struct capreq *cap = n_array_nth(pkg->caps, i);
        tn_array *dbpkgs;
        
        if ((ps->flags & PSET_VRFY_MERCY) && capreq_is_bastard(cap))
            continue;
        
        msg_i(3, indent, "cap %s\n", capreq_snprintf_s(cap));
        dbpkgs = pkgdb_get_conflicted_dbpkgs(db, cap,
                                             upg->uninst_set->dbpkgs,
                                             PKG_LDWHOLE_FLDEPDIRS);
        if (dbpkgs == NULL)
            continue;
            
            
        n = find_db_conflicts_dbcnfl_w_cap(indent, pkg, cap, dbpkgs, ps, upg);
        upg->nerr_cnfl += n;
        upg->nerr_dbcnfl += n;
        ncnfl += n;
        n_array_free(dbpkgs);
        if (n)
            pkg_set_unmetdeps(upg->unmetpms, pkg);
    }
        
        
    if (pkg->cnfls != NULL)
        for (i = 0; i < n_array_size(pkg->cnfls); i++) {
            struct capreq *cnfl = n_array_nth(pkg->cnfls, i);
            tn_array *dbpkgs;
                
            if (capreq_is_obsl(cnfl))
                continue;

            msg_i(3, indent, "cnfl %s\n", capreq_snprintf_s(cnfl));
                
            dbpkgs = pkgdb_get_provides_dbpkgs(db, cnfl,
                                               upg->uninst_set->dbpkgs,
                                               PKG_LDWHOLE_FLDEPDIRS);
            if (dbpkgs == NULL)
                continue;
                
            n = find_db_conflicts_cnfl_w_db(indent, pkg, cnfl, dbpkgs, ps, upg);
            upg->nerr_cnfl += n;
            upg->nerr_dbcnfl += n;
            ncnfl += n;
            n_array_free(dbpkgs);
                
            if (n)
                pkg_set_unmetdeps(upg->unmetpms, pkg);
        }
        
#ifdef ENABLE_FILES_CONFLICTS  /* too slow, needs rpmlib API modifcations */
    msgn(1, "%s's files...", pkg_snprintf_s(pkg));
    ncnfl += find_db_files_conflicts(pkg, upg->ts->db, ps, 
                                     upg->uninst_set->dbpkgs, upg->strict);
#endif        

        
    return ncnfl == 0;
}


#if 0
static
int find_conflicts(struct upgrade_s *upg, int *install_set_cnfl) 
{
    int i, j, ncnfl = 0, nisetcnfl = 0;
    rpmdb dbh;

    dbh = upg->ts->db->dbh;
    
    for (i=0; i<n_array_size(upg->install_pkgs); i++) {
        struct pkg *pkg = n_array_nth(upg->install_pkgs, i);
        
        msg(3, " checking %s\n", pkg_snprintf_s(pkg));
        
        if (pkg->cnflpkgs != NULL)
            for (j = 0; j < n_array_size(pkg->cnflpkgs); j++) {
                struct cnflpkg *cpkg = n_array_nth(pkg->cnflpkgs, j);
                
                if (pkg_is_marked(cpkg->pkg)) {
                    logn(LOGERR, "%s conflicts with %s",
                        pkg_snprintf_s(pkg), pkg_snprintf_s0(cpkg->pkg));
                    ncnfl++;
                    nisetcnfl++;
                }
            }

        for (j = 0; j < n_array_size(pkg->caps); j++) {
            struct capreq *cap = n_array_nth(pkg->caps, j);
            tn_array *dbpkgs;

            msg_i(3, 3, "cap %s\n", capreq_snprintf_s(cap));
            dbpkgs = rpm_get_conflicted_dbpkgs(dbh, cap,
                                               upg->uninst_set->dbpkgs,
                                               PKG_LDWHOLE_FLDEPDIRS);
            if (dbpkgs == NULL)
                continue;
            
            
            ncnfl += find_db_conflicts2(pkg, cap, dbpkgs, 0);
            n_array_free(dbpkgs);
        }
        
        
        if (pkg->cnfls != NULL)
            for (j = 0; j < n_array_size(pkg->cnfls); j++) {
                struct capreq *cnfl = n_array_nth(pkg->cnfls, j);
                tn_array *dbpkgs;
                
                if (capreq_is_obsl(cnfl))
                    continue;

                msg_i(3, 3, "cnfl %s\n", capreq_snprintf_s(cnfl));
                
                dbpkgs = rpm_get_provides_dbpkgs(dbh, cnfl,
                                                 upg->uninst_set->dbpkgs,
                                                 PKG_LDWHOLE_FLDEPDIRS);
                if (dbpkgs != NULL) {
                    ncnfl += find_db_conflicts(pkg, cnfl, dbpkgs, 1);
                    n_array_free(dbpkgs);
                }
            }
        
#ifdef ENABLE_FILES_CONFLICTS  /* too slow, needs rpmlib API modifcations */
        msgn_i(1, 3, "files...");
        ncnfl += find_db_files_conflicts(pkg, upg->ts->db,
                                         upg->uninst_set->dbpkgs, upg->strict);
#endif        
    }
     
    *install_set_cnfl = nisetcnfl;
    return ncnfl;
}
#endif


static int valid_arch_os(struct poldek_ts *ts, tn_array *pkgs) 
{
    int i, nerr = 0;
    
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        
        if (!ts->getop(ts, POLDEK_OP_IGNOREARCH) && pkg->_arch &&
            !pm_machine_score(ts->pmctx, PMMSTAG_ARCH, pkg_arch(pkg))) {
            logn(LOGERR, _("%s: package is for a different architecture (%s)"),
                 pkg_snprintf_s(pkg), pkg_arch(pkg));
            nerr++;
        }
    
        if (!ts->getop(ts, POLDEK_OP_IGNOREOS) && pkg->_os &&
            !pm_machine_score(ts->pmctx, PMMSTAG_OS, pkg_os(pkg))) {
            logn(LOGERR, _("%s: package is for a different operating "
                           "system (%s)"), pkg_snprintf_s(pkg), pkg_os(pkg));
            nerr++;
        }
    }
    
    return nerr == 0;
}


static void show_dbpkg_list(const char *prefix, tn_array *dbpkgs)
{
    int   i, ncol = 2, npkgs = 0;
    int   term_width;
    char  *p, *colon = ", ";

    
    term_width = poldek_term_get_width() - 5;
    ncol = strlen(prefix) + 1;
    
    npkgs = n_array_size(dbpkgs);
    if (npkgs == 0)
        return;
    msg(1, "%s ", prefix);
    
    for (i=0; i < n_array_size(dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(dbpkgs, i);
        p = pkg_snprintf_s(dbpkg);
        if (ncol + (int)strlen(p) >= term_width) {
            ncol = 3;
            msg(1, "_\n%s ", prefix);
        }
        
        if (--npkgs == 0)
            colon = "";
            
        msg(1, "_%s%s", p, colon);
        ncol += strlen(p) + strlen(colon);
    }
    msg(1, "_\n");
}


static void print_install_summary(struct upgrade_s *upg) 
{
    int i, n;
    long int size_download = 0, size_install = 0;
    
    for (i=0; i < n_array_size(upg->install_pkgs); i++) {
        struct pkg *pkg = n_array_nth(upg->install_pkgs, i);
        if (pkg->pkgdir && (vf_url_type(pkg->pkgdir->path) & VFURL_REMOTE))
            size_download += pkg->fsize;
        size_install += pkg->size;
    }
    
    n = n_array_size(upg->install_pkgs);
#ifndef ENABLE_NLS    
    msg(1, "There are %d package%s to install", n, n > 1 ? "s":"");
    if (upg->ndep) 
        msg(1, _("_ (%d marked by dependencies)"), upg->ndep);
    
#else
    msg(1, ngettext("There are %d package to install",
                    "There are %d packages to install", n), n);

    if (upg->ndep) 
        msg(1, ngettext("_ (%d marked by dependencies)",
                        "_ (%d marked by dependencies)", upg->ndep), upg->ndep);
#endif

    if (n_array_size(upg->uninst_set->dbpkgs))
        msg(1, _("_, %d to uninstall"), n_array_size(upg->uninst_set->dbpkgs));
    msg(1, "_:\n");

    
    if (n_array_size(upg->install_pkgs) > 2) {
        n_array_sort(upg->install_pkgs);
        packages_iinf_display(1, "I", upg->install_pkgs, upg->ts->pms,
                              PKGMARK_MARK);

        packages_iinf_display(1, "D", upg->install_pkgs, upg->ts->pms,
                              PKGMARK_DEP);
        show_dbpkg_list("R", upg->uninst_set->dbpkgs);
        
    } else {
        for (i=0; i<n_array_size(upg->install_pkgs); i++) {
            struct pkg *pkg = n_array_nth(upg->install_pkgs, i);
            msg(1, "%c %s\n", pkg_is_dep_marked(upg->ts->pms, pkg) ? 'D' : 'I',
                pkg_snprintf_s(pkg));
        }

        for (i=0; i<n_array_size(upg->uninst_set->dbpkgs); i++) {
            struct pkg *dbpkg = n_array_nth(upg->uninst_set->dbpkgs, i);
            msg(1, "R %s\n", pkg_snprintf_s(dbpkg));
        }
    }
    if (upg->ts->fetchdir == NULL)
        packages_fetch_summary(upg->ts->pmctx, upg->install_pkgs,
                               upg->ts->cachedir, upg->ts->fetchdir ? 1 : 0);
}

static int verify_holds(struct upgrade_s *upg)
{
    int i, j, rc = 1;


    if (poldek_ts_issetf(upg->ts, POLDEK_TS_UPGRADE) == 0)
        return 1;

    if (upg->ts->hold_patterns == NULL)
        return 1;

    for (i=0; i < n_array_size(upg->uninst_set->dbpkgs); i++) {
        struct pkg *dbpkg; 
        struct pkgscore_s psc;

        dbpkg = n_array_nth(upg->uninst_set->dbpkgs, i);
        pkgscore_match_init(&psc, dbpkg);
        
        for (j=0; j < n_array_size(upg->ts->hold_patterns); j++) {
            const char *mask = n_array_nth(upg->ts->hold_patterns, j);
            
            if (pkgscore_match(&psc, mask)) {
                logn(LOGERR, _("%s: refusing to uninstall held package"),
                     pkg_snprintf_s(dbpkg));
                rc = 0;
                break;
            }
        }
    }
    
    return rc;
}

static void mapfn_clean_pkg_flags(struct pkg *pkg) 
{
    pkg_set_color(pkg, PKG_COLOR_WHITE);
}


static
void update_poldek_iinf(struct poldek_iinf *iinf, struct upgrade_s *upg,
                         int vrfy)
{
    int i, is_installed = 1;

    
    if (vrfy)
        pkgdb_reopen(upg->ts->db, O_RDONLY);
    
    for (i=0; i<n_array_size(upg->install_pkgs); i++) {
        struct pkg *pkg = n_array_nth(upg->install_pkgs, i);
        
        if (vrfy) {
            int cmprc = 0;
            
            is_installed = pkgdb_is_pkg_installed(upg->ts->db, pkg, &cmprc);
            if (is_installed && cmprc != 0) 
                is_installed = 0;
        }
        
        if (is_installed)
            n_array_push(iinf->installed_pkgs, pkg_link(pkg));
    }
    
    if (vrfy == 0)
        is_installed = 0;
    
    for (i=0; i < n_array_size(upg->uninst_set->dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(upg->uninst_set->dbpkgs, i);
        struct pkg *pkg = dbpkg;


        if (vrfy) {
            int cmprc = 0;
            
            is_installed = pkgdb_is_pkg_installed(upg->ts->db, pkg, &cmprc);
            if (is_installed && cmprc != 0) 
                is_installed = 0;
        }

        if (is_installed == 0)
            n_array_push(iinf->uninstalled_pkgs, pkg_link(pkg));
    }

    if (vrfy) 
        pkgdb_close(upg->ts->db);
}


/* process packages to install:
   - check dependencies
   - mark unresolved dependencies found in pkgset
   - check conflicts
 */
static
int do_install(struct pkgset *ps, struct upgrade_s *upg,
               struct poldek_iinf *iinf)
{
    int rc, nerr = 0, any_err = 0;
    tn_array *pkgs;
    struct poldek_ts *ts;
    int i;

    ts = upg->ts;

    msgn(1, _("Processing dependencies..."));
    n_array_map(ps->pkgs, (tn_fn_map1)mapfn_clean_pkg_flags);

    for (i = n_array_size(ps->ordered_pkgs) - 1; i > -1; i--) {
        struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
        if (pkg_is_hand_marked(upg->ts->pms, pkg)) 
            process_pkg_deps(-2, pkg, ps, upg, PROCESS_AS_NEW);
        
        if (sigint_reached())
            break;
    }

    pkgs = n_array_new(64, NULL, NULL);
    for (i = n_array_size(ps->ordered_pkgs) - 1; i > -1; i--) {
        struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
        if (pkg_is_marked(upg->ts->pms, pkg))
            n_array_push(pkgs, pkg);
    }
    
    if (upg->nerr_fatal || sigint_reached())
        return 0;

    n_array_sort(upg->install_pkgs);
    print_install_summary(upg);
    pkgdb_close(ts->db); /* release db as soon as possible */

    if (upg->nerr_dep || upg->nerr_cnfl) {
        char errmsg[256];
        int n = 0;
        
        any_err++;
        if (upg->nerr_dep) {
#ifndef ENABLE_NLS
            n += n_snprintf(&errmsg[n], sizeof(errmsg) - n,
                           "%d unresolved dependencies", upg->nerr_dep);
#else
            n += n_snprintf(&errmsg[n], sizeof(errmsg) - n,
                           ngettext("%d unresolved dependency",
                                    "%d unresolved dependencies", upg->nerr_dep),
                           upg->nerr_dep);
#endif    
            
            if (ts->getop_v(ts, POLDEK_OP_NODEPS, POLDEK_OP_RPMTEST, 0))
                upg->nerr_dep = 0;
            else
                nerr++;
        }
        
        if (upg->nerr_cnfl) {
            n += n_snprintf(&errmsg[n], sizeof(errmsg) - n,
                            "%s%d conflicts", n ? ", ":"", upg->nerr_cnfl);
            if (ts->getop_v(ts, POLDEK_OP_FORCE, POLDEK_OP_RPMTEST, 0))
                upg->nerr_cnfl = 0;
            else
                nerr++;
        }

        logn(LOGERR, "%s", errmsg);
    }
    
    
    rc = (any_err == 0);
    if (nerr)
        return 0;
    
    if ((ts->getop_v(ts, POLDEK_OP_JUSTPRINT, POLDEK_OP_JUSTPRINT_N,
                     POLDEK_OP_JUSTFETCH, 0)) == 0)
        if (!valid_arch_os(upg->ts, upg->install_pkgs)) 
            return 0;


    if (ts->getop_v(ts, POLDEK_OP_JUSTPRINT, POLDEK_OP_JUSTPRINT_N, 0)) {
        rc = packages_dump(ps->pkgs, ts->dumpfile,
                           ts->getop(ts, POLDEK_OP_JUSTPRINT_N) == 0);
        return rc;
    }

    /* poldek's test only  */
    if (ts->getop(ts, POLDEK_OP_TEST) && !ts->getop(ts, POLDEK_OP_RPMTEST))
        return rc;
    
    if (ts->getop(ts, POLDEK_OP_JUSTFETCH)) {
        const char *destdir = ts->fetchdir;
        if (destdir == NULL)
            destdir = ts->cachedir;

        rc = packages_fetch(ts->pmctx, upg->install_pkgs, destdir,
                            ts->fetchdir ? 1 : 0);

    } else if (!ts->getop(ts, POLDEK_OP_HOLD) || (rc = verify_holds(upg))) {
        int is_test = ts->getop(ts, POLDEK_OP_RPMTEST);

        if (!is_test && ts->getop(ts, POLDEK_OP_CONFIRM_INST) && ts->ask_fn) {
            if (!ts->ask_fn(1, _("Proceed? [Y/n]")))
                return 1;
        }
        
        if (!ts->getop(ts, POLDEK_OP_NOFETCH))
            if (!packages_fetch(ts->pmctx, pkgs, ts->cachedir, 0))
                return 0;

        rc = pm_pminstall(ts->db, pkgs, upg->uninst_set->dbpkgs, ts);
        
        if (!is_test && iinf)
            update_poldek_iinf(iinf, upg, rc <= 0);

        if (rc && !ts->getop_v(ts, POLDEK_OP_RPMTEST, POLDEK_OP_KEEP_DOWNLOADS,
                               POLDEK_OP_NOFETCH, 0))
            packages_fetch_remove(pkgs, ts->cachedir);
    }
    
    return rc;
}

static void init_upgrade_s(struct upgrade_s *upg, struct poldek_ts *ts)
{
    upg->avpkgs = ts->ctx->ps->pkgs;
    upg->install_pkgs = n_array_new(128, NULL, (tn_fn_cmp)pkg_nvr_strcmp);
    upg->db_deps = db_deps_new();
    upg->uninst_set = dbpkg_set_new();
    upg->orphan_dbpkgs = pkgs_array_new_ex(128, pkg_cmp_recno);

    upg->strict = ts->getop(ts, POLDEK_OP_VRFYMERCY);
    upg->ndberrs = upg->ndep = upg->ninstall = upg->nmarked = 0;
    upg->nerr_dep = upg->nerr_cnfl = upg->nerr_dbcnfl = upg->nerr_fatal = 0;
    upg->ts = ts; 
    upg->pkg_stack = n_array_new(32, NULL, NULL);
    upg->dbpms = pkgmark_set_new(0);
    upg->unmetpms = pkgmark_set_new(0);
}


static void destroy_upgrade_s(struct upgrade_s *upg)
{
    upg->avpkgs = NULL;
    n_array_free(upg->install_pkgs);
    n_hash_free(upg->db_deps);
    dbpkg_set_free(upg->uninst_set);
    n_array_free(upg->orphan_dbpkgs);
    upg->ts = NULL;
    pkgmark_set_free(upg->dbpms);
    pkgmark_set_free(upg->unmetpms);
    memset(upg, 0, sizeof(*upg));
}


static void reset_upgrade_s(struct upgrade_s *upg)
{

    n_array_clean(upg->install_pkgs);
    
    n_hash_clean(upg->db_deps);
    dbpkg_set_free(upg->uninst_set);
    upg->uninst_set = dbpkg_set_new();
    n_array_clean(upg->orphan_dbpkgs);

    pkgmark_set_free(upg->dbpms);
    upg->dbpms = pkgmark_set_new(0);

    pkgmark_set_free(upg->unmetpms);
    upg->unmetpms = pkgmark_set_new(0);
    
    upg->ndberrs = upg->ndep = upg->ninstall = upg->nmarked = 0;
    upg->nerr_dep = upg->nerr_cnfl = upg->nerr_dbcnfl = upg->nerr_fatal = 0;
}


static 
void mapfn_mark_newer_pkg(const char *n, uint32_t e,
                          const char *v, const char *r, void *upgptr) 
{
    struct upgrade_s  *upg = upgptr;
    struct pkg        *pkg, tmpkg;
    int               i, cmprc;
    
    tmpkg.name = (char*)n;
    tmpkg.epoch = e;
    tmpkg.ver = (char*)v;
    tmpkg.rel = (char*)r;
    
    i = n_array_bsearch_idx_ex(upg->avpkgs, &tmpkg, (tn_fn_cmp)pkg_cmp_name); 
    if (i < 0) {
        msg(3, "%-32s not found in repository\n", pkg_snprintf_s(&tmpkg));
        return;
    }
    
    pkg = n_array_nth(upg->avpkgs, i);
    cmprc = pkg_cmp_evr(pkg, &tmpkg);
    if (poldek_VERBOSE) {
        if (cmprc == 0) 
            msg(3, "%-32s up to date\n", pkg_snprintf_s(&tmpkg));
        
        else if (cmprc < 0)
            msg(3, "%-32s newer than repository one\n", pkg_snprintf_s(&tmpkg));
        
        else 
            msg(2, "%-32s -> %-30s\n", pkg_snprintf_s(&tmpkg),
                pkg_snprintf_s0(pkg));
    }

    if ((pkg = n_hash_get(upg->db_pkgs, tmpkg.name))) {
        if (pkg_is_marked(upg->ts->pms, pkg)) {
            upg->nmarked--;
            logn(LOGWARN, _("%s: multiple instances installed, skipped"),
                 tmpkg.name);
            pkg_unmark(upg->ts->pms, pkg);        /* display above once */
        }

        return;
    }

    pkg = n_array_nth(upg->avpkgs, i);
    if (cmprc > 0) {
        if (pkg_is_scored(pkg, PKG_HELD)) {
            msgn(1, _("%s: skip held package"), pkg_snprintf_s(pkg));
            
        } else {
            n_hash_insert(upg->db_pkgs, tmpkg.name, pkg);
            pkg_hand_mark(upg->ts->pms, pkg);
            upg->nmarked++;
        }
    }
    
}


int do_poldek_ts_upgrade_dist(struct poldek_ts *ts) 
{
    struct upgrade_s upg;
    int nmarked;
    
    init_upgrade_s(&upg, ts);
    upg.db_pkgs = n_hash_new(103, NULL);
    
    msgn(1, _("Looking up packages for upgrade..."));
    pkgdb_map_nevr(ts->db, mapfn_mark_newer_pkg, &upg);
    n_hash_free(upg.db_pkgs);

    if (upg.ndberrs) {
        logn(LOGERR, _("There are database errors (?), give up"));
        destroy_upgrade_s(&upg);
        return 0;
    }
    
    nmarked = upg.nmarked;
    destroy_upgrade_s(&upg);

    if (sigint_reached()) 
        return 0;
    else if (nmarked == 0)
        msgn(1, _("Nothing to do"));
    else
        return do_poldek_ts_install(ts, NULL);
    return 1;
}


static void mark_namegroup(tn_array *pkgs, struct pkg *pkg, struct upgrade_s *upg) 
{
    struct pkg tmpkg;
    int n, i, len;
    char *p, prefix[512];


    n_array_sort(pkgs);
    
    len = n_snprintf(prefix, sizeof(prefix), "%s", pkg->name);
    if ((p = strchr(prefix, '-')))
        *p = '\0';
    
    tmpkg.name = prefix;

    //*p = '-';
    n = n_array_bsearch_idx_ex(pkgs, &tmpkg, (tn_fn_cmp)pkg_ncmp_name);
    
    
    //if (n < 0 && p) {
    //    n = n_array_bsearch_idx_ex(pkgs, &tmpkg, (tn_fn_cmp)pkg_cmp_name);
    // }

    if (n < 0)
        return;
    
    len = strlen(prefix);
    
    for (i = n; i < n_array_size(pkgs); i++) {
        struct pkg *p = n_array_nth(pkgs, i);
        int pkg_name_len;

        if ((pkg_name_len = strlen(pkg->name)) < len)
            break;
        
        if (strncmp(p->name, prefix, len) != 0) 
            break;

        if (!pkg_is_marked_i(upg->ts->pms, p)) 
            continue;
        
        if (pkg->pkgdir != p->pkgdir)
            continue;

        if (!pkg_is_marked(upg->ts->pms, p))
            mark_package(p, upg);
    }
}

static
int unmark_name_dups(struct pkgmark_set *pms, tn_array *pkgs) 
{
    struct pkg *pkg, *pkg2;
    int i, n, nmarked = 0;
    
    if (n_array_size(pkgs) < 2)
        return 0;
    
    n_array_sort(pkgs);

    i = n = 0;
    while (i < n_array_size(pkgs) - 1) {
        pkg = n_array_nth(pkgs, i);
        i++;
        
        if (!pkg_is_marked(pms, pkg))
            continue;
        
        nmarked++;
        DBGF("%s\n", pkg_snprintf_s(pkg));
        
        pkg2 = n_array_nth(pkgs, i);
        while (pkg_cmp_name(pkg, pkg2) == 0) {
            pkg_unmark(pms, pkg2);
            DBGF("unmark %s\n", pkg_snprintf_s(pkg2));

            i++;
            n++;
            if (i == n_array_size(pkgs))
                break;
            pkg2 = n_array_nth(pkgs, i);
        }
    }
    
    return nmarked;
}

static
int prepare_icap(struct poldek_ts *ts, const char *capname, tn_array *pkgs) 
{
    int i, found = 0;
    tn_array *dbpkgs;
    struct capreq *cap;

    capreq_new_name_a(capname, cap);
    dbpkgs = pkgdb_get_provides_dbpkgs(ts->db, cap, NULL, 0);
    if (dbpkgs == NULL) {
        struct pkg *pkg;
        if (ts->getop(ts, POLDEK_OP_FRESHEN))
            return 0;

        n_array_sort_ex(pkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev);
        pkg = n_array_nth(pkgs, 0);
        pkg_hand_mark(ts->pms, pkg);
        return 1;
    }
    
    n_array_sort_ex(pkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev);
    for (i=0; i < n_array_size(dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(dbpkgs, i);
        int n = n_array_bsearch_idx_ex(pkgs, dbpkg,
                                       (tn_fn_cmp)pkg_cmp_name);

        DBGF("%s: %s\n", capname, pkg_snprintf_s0(dbpkg));
        
        if (n < 0)
            continue;
    
        for (; n < n_array_size(pkgs); n++) {
            struct pkg *pkg = n_array_nth(pkgs, n);
            int cmprc, mark = 0;

            DBGF("%s: %s cmp %s\n", capname, pkg_snprintf_s(pkg),
                 pkg_snprintf_s0(dbpkg));
            if (pkg_cmp_name(pkg, dbpkg) != 0)
                break;
            
            cmprc = pkg_cmp_name_evr(pkg, dbpkg);
            if (cmprc > 0)
                mark = 1;
                
            else if (cmprc == 0 && poldek_ts_issetf(ts, POLDEK_TS_REINSTALL))
                mark = 1;
                
            else if (cmprc < 0 && poldek_ts_issetf(ts, POLDEK_TS_DOWNGRADE))
                mark = 1;

            if (mark) {
                found = 1;
                msgn(1, _("%s: marked as %s's provider"), pkg_snprintf_s(pkg),
                     capname);
                
                pkg_hand_mark(ts->pms, pkg);
                goto l_end;
                
            } else if (cmprc <= 0) {
                char *eqs = cmprc == 0 ? "equal" : "newer";
                msgn(1, _("%s: %s version of %s is installed (%s), skipped"),
                     capname, eqs, pkg_snprintf_s0(dbpkg),
                     pkg_snprintf_s(pkg));
                
            } else {
                n_assert(0);
                
            }
        }
    }
l_end:
    if (dbpkgs)
        n_array_free(dbpkgs);
    
    return found;
}

static
int prepare_icaps(struct poldek_ts *ts) 
{
    tn_array *keys;
    tn_hash *icaps;
    int i;

    icaps = arg_packages_get_resolved_caps(ts->aps);
    keys = n_hash_keys_cp(icaps);
    for (i=0; i < n_array_size(keys); i++) {
        const char *cap = n_array_nth(keys, i);
        tn_array *pkgs = n_hash_get(icaps, cap);
        prepare_icap(ts, cap, pkgs);
    }
    n_array_free(keys);
    n_hash_free(icaps);
    return 1;
}



int do_poldek_ts_install(struct poldek_ts *ts, struct poldek_iinf *iinf)
{
    int i, nmarked = 0, nerr = 0, n, is_particle;
    struct upgrade_s upg;
    struct pkgset *ps = ts->ctx->ps;

    
    n_assert(ts->type == POLDEK_TS_INSTALL);

    prepare_icaps(ts);
    if (unmark_name_dups(ts->pms, ps->pkgs) == 0) {
        msgn(1, _("Nothing to do"));
        return 1;
    }
    
    MEMINF("START");
    init_upgrade_s(&upg, ts);

    is_particle = ts->getop(ts, POLDEK_OP_PARTICLE);
    
    /* tests make sense on whole set only  */
    if (ts->getop_v(ts, POLDEK_OP_TEST, POLDEK_OP_RPMTEST, 0))
        ts->setop(ts, POLDEK_OP_PARTICLE, 0);

    if (ts->getop_v(ts, POLDEK_OP_JUSTPRINT, POLDEK_OP_JUSTPRINT_N, 0))
        ts->setop(ts, POLDEK_OP_PARTICLE, 0);
    
    
    for (i = 0; i < n_array_size(ps->ordered_pkgs); i++) {
        struct pkg    *pkg = n_array_nth(ps->ordered_pkgs, i);
        int           install;

        if (!pkg_is_marked(ts->pms, pkg))
            continue;

        if (sigint_reached())
            goto l_end;
        
        install = is_installable(pkg, ts, 1);
        
        pkg_unmark(ts->pms, pkg);
        if (install > 0) {
            pkg_mark_i(ts->pms, pkg);
            nmarked++;
        }
    }
    
    if (nmarked == 0) {
        msgn(1, _("Nothing to do"));
        goto l_end;
    }

    if (nmarked == 1)
        ts->setop(ts, POLDEK_OP_PARTICLE, 0);
    
    n = 1;
#if 0                           /* debug */
    for (i = 0; i < n_array_size(ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
        if (pkg_is_marked_i(pkg)) 
            printf("MARKED %s\n", pkg_snprintf_s(pkg));
    }
#endif    
    for (i = 0; i < n_array_size(ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);

        if (!pkg_is_marked_i(ts->pms, pkg)) 
            continue;

        if (sigint_reached())
            goto l_end;
        
        if (ts->getop(ts, POLDEK_OP_PARTICLE)) {
            if (n > 1) {
                if (poldek_VERBOSE > 0) {
                    poldek_term_printf_c(PRCOLOR_YELLOW,
                                         "Installing set #%d\n", n);
                    fflush(stdout);
                }
                msgn_f(0, "** Installing set #%d\n", n);
            }
            
            n++;
            pkgdb_reopen(upg.ts->db, 0);
        }
        
        mark_package(pkg, &upg);

        if (ts->getop(ts, POLDEK_OP_PARTICLE)) {
            mark_namegroup(pkg->pkgdir->pkgs, pkg, &upg);
                
            if (!do_install(ps, &upg, iinf))
                nerr++;

            pkgmark_massset(ts->pms, 0, PKGMARK_MARK | PKGMARK_DEP);
            reset_upgrade_s(&upg);
        }
    }

    if (!ts->getop(ts, POLDEK_OP_PARTICLE))
        nerr = !do_install(ps, &upg, iinf);

 l_end:
    
    destroy_upgrade_s(&upg);
    MEMINF("END");
    if (is_particle)
        ts->setop(ts, POLDEK_OP_PARTICLE, 1);
    
    return nerr == 0;
}
