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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <fnmatch.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <trurl/trurl.h>

#include "sigint/sigint.h"
#define ENABLE_TRACE 0
#include "i18n.h"
#include "log.h"
#include "pkgset.h"
#include "misc.h"
#include "pkg.h"
#include "pkgmisc.h"
#include "poldek_ts.h"
#include "poldek_intern.h"
#include "capreq.h"
#include "pm/pm.h"
#include "pkgfl.h"

#define DBPKG_ORPHANS_PROCESSED   (1 << 15) /* is its orphan processed ?*/
#define DBPKG_DEPS_PROCESSED      (1 << 16) /* is its deps processed? */
#define DBPKG_TOUCHED             (1 << 17)
#define DBPKG_REV_ORPHANED        (1 << 19)


#define uninst_LDFLAGS (PKG_LDNEVR | PKG_LDCAPS | PKG_LDREQS | PKG_LDFL_WHOLE)

static void uninstall_summary(struct poldek_ts *ts, tn_array *pkgs, int ndep);

static void update_iinf(struct poldek_ts *ts, tn_array *pkgs,
                               struct pkgdb *db, int vrfy);
struct uninstall_ctx *uctx;
static int process_package(int indent, struct uninstall_ctx *uctx,
                           struct pkg *pkg);

struct uninstall_ctx {
    struct pkgdb       *db;
    struct poldek_ts   *ts;
    tn_array           *unpkgs;
    struct pkgmark_set *pms;
    int                strict;
    int                rev_orphans_deep;
    int                ndep;
    int                nerr_fatal;
    int                nerr_dep;
};

static
tn_array *get_orphanedby_pkg(struct uninstall_ctx *uctx, struct pkg *pkg)
{
    tn_array *orphans;
    struct capreq *selfcap;
    unsigned ldflags = uninst_LDFLAGS;
    int i, n = 0;
    
    if (sigint_reached())
        return 0;
    MEMINF("START");
    DBGF("%s\n", pkg_id(pkg));

    orphans = pkgs_array_new_ex(128, pkg_cmp_recno);

    capreq_new_name_a(pkg->name, selfcap);
    n += pkgdb_q_what_requires(uctx->db, orphans, selfcap,
                               uctx->unpkgs, ldflags, 0);

    if (pkg->caps)
        for (i=0; i < n_array_size(pkg->caps); i++) {
            struct capreq *cap = n_array_nth(pkg->caps, i);
            n += pkgdb_q_what_requires(uctx->db, orphans, cap,
                                       uctx->unpkgs, ldflags, 0);
        }
    
    if (pkg->fl) {
        struct pkgfl_it it;
        const char *path;

        pkgfl_it_init(&it, pkg->fl);
        while ((path = pkgfl_it_get(&it, NULL))) {
            struct capreq *cap;
            capreq_new_name_a(path, cap);
            tracef(0, "%s of %s", pkg_id(pkg), path);
            
            n += pkgdb_q_what_requires(uctx->db, orphans, cap, 
                                       uctx->unpkgs, ldflags, 0);
        }
    }
    
    MEMINF("END");
    
    if (n_array_size(orphans) == 0) {
        n_array_free(orphans);
        orphans = NULL;
    }

    return orphans;
}

static int pkg_leave_orphans(struct uninstall_ctx *uctx, struct pkg *pkg)
{
    struct capreq *selfcap;
    tn_array *exclude;
    int i;
    
    exclude = n_array_dup(uctx->unpkgs, (tn_fn_dup)pkg_link);
    /* yep, there are packages which requires themselves */
    n_array_push(exclude, pkg_link(pkg)); 
    
    capreq_new_name_a(pkg->name, selfcap);
    if (pkgdb_q_is_required(uctx->db, selfcap, exclude))
        goto l_yes;

    if (pkg->caps)
        for (i=0; i < n_array_size(pkg->caps); i++) {
            struct capreq *cap = n_array_nth(pkg->caps, i);
            if (pkgdb_q_is_required(uctx->db, cap, exclude))
                goto l_yes;
        }
    
    if (pkg->fl) {
        struct pkgfl_it it;
        const char *path;

        pkgfl_it_init(&it, pkg->fl);
        while ((path = pkgfl_it_get(&it, NULL))) {
            struct capreq *cap;
            capreq_new_name_a(path, cap);
            if (pkgdb_q_is_required(uctx->db, cap, exclude))
                goto l_yes;
        }
    }

    
    n_array_free(exclude);
    return 0;
    
l_yes:
    n_array_free(exclude);
    return 1;
}
        


/*
  adds to unpkgs packages required by pkg and which
  is not required by any other packages -> could be removed so. 
*/
static
int process_pkg_rev_orphans(int indent, struct uninstall_ctx *uctx,
                            struct pkg *pkg, int deep)
{
    int i, j;
    tn_array *dbpkgs = NULL;
    
    if (pkg->reqs == NULL)
        return 1;
    
    for (i=0; i < n_array_size(pkg->reqs); i++) {
        struct capreq *req = n_array_nth(pkg->reqs, i);
        pkgdb_search(uctx->db, &dbpkgs, PMTAG_NAME, capreq_name(req),
                     uctx->unpkgs, uninst_LDFLAGS);

        pkgdb_search(uctx->db, &dbpkgs, PMTAG_CAP, capreq_name(req),
                     uctx->unpkgs, uninst_LDFLAGS);

        if (dbpkgs == NULL)
            continue;
        
        for (j=0; j < n_array_size(dbpkgs); j++) {
            struct pkg *dbpkg = n_array_nth(dbpkgs, j);

            if (pkg_isset_mf(uctx->pms, dbpkg, DBPKG_REV_ORPHANED))
                continue;       /* was there */

            msgn_i(3, indent, _("  %s requires %s"), pkg_id(pkg), pkg_id(dbpkg));
            
            if (pkg_leave_orphans(uctx, dbpkg))
                continue;
            
            msgn_i(1, indent, _("%s marks orphaned %s (req %s)"),
                   pkg_id(pkg), pkg_id(dbpkg), capreq_snprintf_s(req));

            pkg_set_mf(uctx->pms, dbpkg, DBPKG_REV_ORPHANED);
            pkg_dep_mark(uctx->ts->pms, dbpkg);
            n_array_push(uctx->unpkgs, pkg_link(dbpkg));
            uctx->ndep++;
            
            if (uctx->rev_orphans_deep > deep)
                process_pkg_rev_orphans(indent + 2, uctx, dbpkg, deep + 1);
        }
    }
    
    if (dbpkgs)
        n_array_free(dbpkgs);
    return 1;
}

static
int process_pkg_reqs(int indent, struct uninstall_ctx *uctx, struct pkg *pkg,
                     struct pkg *requirer) 
{
    struct pkg_req_iter *it;
    const struct capreq *req;
    unsigned itflags = PKG_ITER_REQIN;
    
    if (sigint_reached() || uctx->nerr_fatal)
        return 0;

    if (pkg_is_marked(uctx->ts->pms, pkg)) {
        DBGF("%s: obsoleted, return\n", pkg_id(pkg)); 
        //n_assert(0);
        //db_deps_remove_pkg(uctx->db_deps, pkg);
        return 1;
    }
    MEMINF("START");
    tracef(indent, "%s (requirer=%s)", pkg_id(pkg), pkg_id(requirer));

    msg_i(3, indent, "%s\n", pkg_id(pkg));

    if (uctx->ts->getop(uctx->ts, POLDEK_OP_AUTODIRDEP))
        itflags |= PKG_ITER_REQDIR;
    

    it = pkg_req_iter_new(pkg, itflags);
    while ((req = pkg_req_iter_get(it))) {
        
        if (capreq_is_rpmlib(req)) 
            continue;

        trace(indent + 1, "req %s", capreq_snprintf_s(req));

        if (pkg_satisfies_req(pkg, req, 1)) { /* XXX: self match, should be handled
                                                 at lower level; TOFIX */
            trace(indent + 2, "- satisfied by itself");
            
        } else if (pkgdb_match_req(uctx->db, req, uctx->strict, uctx->unpkgs)) {
            trace(indent + 2, "- satisfied by db");
            msg_i(3, indent, "  %s: satisfied by db\n", capreq_snprintf_s(req));
            
        } else if (!uctx->ts->getop(uctx->ts, POLDEK_OP_FOLLOW)) {
            logn(LOGERR, _("%s (cap %s) is required by %s"),
                 pkg_id(requirer), capreq_snprintf_s(req), pkg_id(pkg));
            uctx->nerr_dep++;
            
        } else if (!pkg_is_marked(uctx->ts->pms, pkg)) {
            struct pkg *bypkg = requirer;
            
            DBGF("%s MARKS %s (req %s)?\n", pkg_id(requirer), pkg_id(pkg),
                 capreq_snprintf_s(req));
            
            /* find the requirer */
            if (!pkg_satisfies_req(requirer, req, uctx->strict)) {
                int j;
                
                bypkg = NULL;
                for (j=0; j < n_array_size(uctx->unpkgs); j++) {
                    struct pkg *dbpkg = n_array_nth(uctx->unpkgs, j);
                    DBGF("%s MARKS %s (req %s)?\n", pkg_id(dbpkg), pkg_id(pkg),
                         capreq_snprintf_s(req));
                    
                    if (pkg_satisfies_req(dbpkg, req, uctx->strict)) {
                        bypkg = dbpkg;
                        break;
                    }
                }
            }
            
            /* installed with unsatisfied requirement - ignored */
            if (bypkg == NULL)  
                continue;
            
            msgn_i(1, bypkg->pri, _("%s marks %s (req %s)"), pkg_id(bypkg),
                   pkg_id(pkg), capreq_snprintf_s(req));

            uctx->ndep++;
            pkg_dep_mark(uctx->ts->pms, pkg);
            n_array_push(uctx->unpkgs, pkg_link(pkg));
            process_package(indent + 2, uctx, pkg);
        }
    }
    tracef(indent, "END %s (requirer=%s)", pkg_id(pkg), pkg_id(requirer));
    MEMINF("END");
    return 1;
}

static
int process_package(int indent, struct uninstall_ctx *uctx, struct pkg *pkg)
{
    tn_array *orphans, *pkgorphans;
    int i, n = 0;
    
    if (pkg_isset_mf(uctx->pms, pkg, PKGMARK_GRAY)) /* was there */
        return 0;

    MEMINF("START");
    tracef(indent, "%s", pkg_id(pkg));

    pkg_set_mf(uctx->pms, pkg, PKGMARK_GRAY); /* is there */
    
    if (uctx->ts->getop(uctx->ts, POLDEK_OP_GREEDY))
        process_pkg_rev_orphans(indent, uctx, pkg, 1);
        
    pkgorphans = get_orphanedby_pkg(uctx, pkg);
    if (pkgorphans == NULL)
        return 0;
    
    orphans = pkgs_array_new(n_array_size(pkgorphans));
    for (i=0; i<n_array_size(pkgorphans); i++) {
        struct pkg *dbpkg = n_array_nth(pkgorphans, i);
        if (!pkg_is_marked(uctx->ts->pms, dbpkg)) {
            DBGF("%s ORPHANEDBY %s\n", pkg_id(dbpkg), pkg_id(pkg));
            n_array_push(orphans, pkg_link(dbpkg));
        }
    }
    n_array_free(pkgorphans);
    
    if (n_array_size(orphans)) {
        pkg->pri = indent;      /* pri is used as indent, looks messy but
                                   pkg is local to this module and pri
                                   never be used in other context
                                 */
        for (i=0; i<n_array_size(orphans); i++) {
            struct pkg *dbpkg = n_array_nth(orphans, i);
            DBGF("%s ORPHANED by %s\n", pkg_id(dbpkg), pkg_id(pkg));
            process_pkg_reqs(indent, uctx, dbpkg, pkg);
        }
    }
    
    n = n_array_size(orphans);
    n_array_free(orphans);
    DBGF("END PROCESSING [%d] %s\n", indent, pkg_id(pkg));
    MEMINF("END");

    pkg_set_mf(uctx->pms, pkg, PKGMARK_BLACK); /* done */
    return n;
}

static
struct uninstall_ctx *uninstall_ctx_new(struct poldek_ts *ts) 
{
    struct uninstall_ctx *uctx = n_malloc(sizeof(*uctx));
    memset(uctx, 0, sizeof(*uctx));

    uctx->db = ts->db;
    uctx->ts = ts;
    uctx->unpkgs = pkgs_array_new_ex(128, pkg_cmp_recno);
    uctx->pms = pkgmark_set_new(0, 0);
    uctx->strict = 1;
    /* how deeply cause removes too much packages */
    uctx->rev_orphans_deep = ts->uninstall_greedy_deep;
    return uctx;
};

static void uninstall_ctx_free(struct uninstall_ctx *uctx) 
{
#if ENABLE_TRACE
    int i;
    for (i=0; i < n_array_size(uctx->unpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(uctx->unpkgs, i);
        msgn(1, _("freedbset %d %s"), dbpkg->_refcnt, pkg_id(dbpkg));
    }
#endif    
    n_array_free(uctx->unpkgs);
    pkgmark_set_free(uctx->pms);
    free(uctx);
};

static int do_process(struct uninstall_ctx *uctx)
{
    int i, n = 0;
    tn_array *tmp;

    for (i=0; i < n_array_size(uctx->unpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(uctx->unpkgs, i);
        msgn(1, _("mark %s"), pkg_id(dbpkg));
        pkg_hand_mark(uctx->ts->pms, dbpkg);
        n++;
    }

    if (!uctx->ts->getop(uctx->ts, POLDEK_OP_FOLLOW))
        return n;
    
    MEMINF("startdeps");
    msgn(1, _("Processing dependencies..."));
    
    tmp = n_array_dup(uctx->unpkgs, (tn_fn_dup)pkg_link);
    for (i=0; i < n_array_size(tmp); i++) {
        struct pkg *dbpkg = n_array_nth(tmp, i);
        process_package(0, uctx, dbpkg);
    }
    n_array_free(tmp);
    MEMINF("enddeps");
    
    return n;
}

static
int do_resolve_package(struct uninstall_ctx *uctx, struct poldek_ts *ts,
                       const char *mask, const struct capreq *cr,
                       const char *arch)
{
    tn_array *dbpkgs = NULL;
    int i, nmatches = 0;

    n_assert(cr);
    DBGF("get_provides %s\n", capreq_snprintf_s(cr));
    
    pkgdb_search(ts->db, &dbpkgs, PMTAG_CAP, capreq_name(cr), NULL, uninst_LDFLAGS);
    
    mask = mask;
    DBGF("mask %s (%s) -> %d package(s)\n", mask, capreq_snprintf_s(cr), 
         dbpkgs ? n_array_size(dbpkgs) : 0);
        
    if (dbpkgs == NULL)
        return 0;
    
    for (i=0; i < n_array_size(dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(dbpkgs, i);
        int matched = 0;

        DBGF("  - %s (%s?)\n", pkg_id(dbpkg), capreq_snprintf_s(cr));
                
        if (!capreq_versioned(cr)) {
            if (strcmp(dbpkg->name, capreq_name(cr)) == 0)
                matched = 1;
            
        } else {                /* with version */
            if (ts->getop(ts, POLDEK_OP_CAPLOOKUP)) {
                if (pkg_xmatch_req(dbpkg, cr, POLDEK_MA_PROMOTE_REQEPOCH))
                    matched = 1;
                
            } else {
                if (strcmp(dbpkg->name, capreq_name(cr)) == 0) {
                    DBGF("n (%s, %s) %d\n", dbpkg->name,
                         capreq_name(cr), 
                         pkg_evr_match_req(dbpkg, cr,
                                           POLDEK_MA_PROMOTE_REQEPOCH));
                    
                }
                        
                if (strcmp(dbpkg->name, capreq_name(cr)) == 0 &&
                    pkg_evr_match_req(dbpkg, cr, POLDEK_MA_PROMOTE_REQEPOCH))
                    matched = 1;
            }
            
            if (matched && arch) {
                const char *dbarch = pkg_arch(dbpkg);
                matched = n_str_eq(arch, dbarch ? dbarch : "none");
            }
            
        }

        if (matched) {
            nmatches++;
            n_array_push(uctx->unpkgs, pkg_link(dbpkg));
        }
    }
    
    n_array_free(dbpkgs);
    return nmatches;
}

static int resolve_package(struct uninstall_ctx *uctx, struct poldek_ts *ts,
                           const char *mask, const char *arch)
{
    char           *p;
    struct capreq  *cr, *cr_evr;
    int            resolved = 0;
    
    cr = NULL; cr_evr = NULL;

    DBGF("mask=%s\n", mask); 
    /* No EVR mask or empty EVR (last char '#') */
    if ((p = strchr(mask, '#')) == NULL || *(p + 1) == '\0') {
        if (p)
            *p = '\0';
        capreq_new_name_a(mask, cr);
            
    } else {
        const char *ver, *rel;
        char *tmp;
        uint32_t epoch;
        
        n_strdupap(mask, &tmp);
        p = strchr(tmp, '#');
        n_assert(p);
        *p = '\0';
        p++;

        if (poldek_util_parse_evr(p, &epoch, &ver, &rel)) {
            cr = cr_evr = capreq_new(NULL, tmp, epoch, ver, rel, REL_EQ, 0);
            DBGF("cap=%s\n", capreq_snprintf_s(cr)); 
        }
    }
    
    if (do_resolve_package(uctx, ts, mask, cr, arch))
        resolved = 1;

    if (cr_evr)
        capreq_free(cr_evr);
    
    return resolved;
}

static int resolve_mask(struct uninstall_ctx *uctx, struct poldek_ts *ts,
                        const char *mask)
{
    char *p, *tmp;
    const char *n, *v, *r;
    char nmask[256];
    int32_t e = 0;
    int matched = 0;
    
    msgn(2, _("Trying %s\n"), mask);
    if (resolve_package(uctx, ts, mask, NULL))
        return 1;
            
    if ((p = strchr(mask, '-')) == NULL) /* try N-[E:]V */
        return 0;

    /* try N-[E:]V-R */
    n_strdupap(mask, &tmp);
    p = strrchr(tmp, '-');
    *p = '#';
        
    msgn(2, _("  Trying %s\n"), tmp);
                
    if (resolve_package(uctx, ts, tmp, NULL))
        return 1;
    
    n_strdupap(mask, &tmp);
    if (poldek_util_parse_nevr(tmp, &n, &e, &v, &r)) {
        if (e)
            n_snprintf(nmask, sizeof(nmask), "%s#%d:%s-%s", n, e, v, r);
        else
            n_snprintf(nmask, sizeof(nmask), "%s#%s-%s", n, v, r);

        msgn(2, _("    Trying %s\n"), nmask);
        DBGF("try %s => %s (%s, %s, %s)\n", mask, nmask, n, v, r);
        matched = resolve_package(uctx, ts, nmask, NULL);
        
        if (!matched && (p = strrchr(r, '.'))) { /* try N-[E:]-V-R.ARCH */
            *p = '\0';
            p++;
            
            if (e)
                n_snprintf(nmask, sizeof(nmask), "%s#%d:%s-%s", n, e, v, r);
            else
                n_snprintf(nmask, sizeof(nmask), "%s#%s-%s", n, v, r);
            msgn(2, _("      Trying %s (arch=%s)\n"), nmask, p);
            matched = resolve_package(uctx, ts, nmask, p);
        }
    }

    return matched;
}

static int resolve_packages(struct uninstall_ctx *uctx, struct poldek_ts *ts)
{
    int               i, nerr = 0;
    tn_array          *masks;
    
    masks = poldek_ts_get_args_asmasks(ts, 1);
    
    for (i=0; i < n_array_size(masks); i++) {
        char *mask = n_array_nth(masks, i);

        if (!resolve_mask(uctx, ts, mask)) {
            logn(LOGERR, _("%s: no such package"), mask);
            nerr++;
        }
    }
    

    n_array_free(masks);
    return nerr == 0;
}


static tn_array *reorder_packages(tn_array *pkgs)
{
    struct pkgset *ps;
    tn_array *ordered_pkgs = NULL;
    
    int i;

    ps = pkgset_new(0);
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        pkgset_add_package(ps, pkg);
    }

    pkgset_setup(ps, PSET_NOORDER);
    packages_order(ps->pkgs, &ordered_pkgs, PKGORDER_UNINSTALL);

    ordered_pkgs = n_array_reverse(ordered_pkgs);
#if ENABLE_TRACE    
    for (i=0; i < n_array_size(ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ordered_pkgs, i);
        DBGF("%d. %s\n", i, pkg_id(pkg));
    }
#endif    
    pkgset_free(ps);
    
    return ordered_pkgs;
}


int do_poldek_ts_uninstall(struct poldek_ts *ts)
{
    int               nerr = 0, run_uninstall = 0;
    tn_array          *pkgs = NULL, *ordered_pkgs = NULL;
    struct uninstall_ctx *uctx;

    MEMINF("START");
    uctx = uninstall_ctx_new(ts);
    if (!resolve_packages(uctx, ts)) {
        nerr++;
        goto l_end;
    }
    
    n_array_uniq(uctx->unpkgs);
    if (nerr == 0 && n_array_size(uctx->unpkgs)) {
        do_process(uctx);
        pkgs = uctx->unpkgs;
    }
    pkgdb_close(ts->db); /* release db as soon as possible */
    
    if (nerr || pkgs == NULL)
        goto l_end;
    
    ordered_pkgs = reorder_packages(pkgs);
    uninstall_summary(ts, pkgs, uctx->ndep);

    if (uctx->nerr_dep) {
        char errmsg[256];
        int n = 0;
        
#ifndef ENABLE_NLS
        n += n_snprintf(&errmsg[n], sizeof(errmsg) - n,
                        "%d unresolved dependencies", uctx->nerr_dep);
#else
        n += n_snprintf(&errmsg[n], sizeof(errmsg) - n,
                        ngettext("%d unresolved dependency",
                                 "%d unresolved dependencies", uctx->nerr_dep),
                        uctx->nerr_dep);
#endif    
        logn(LOGERR, "%s", errmsg);
        
        if (ts->getop_v(ts, POLDEK_OP_NODEPS, POLDEK_OP_RPMTEST, 0))
            uctx->nerr_dep = 0;
        else
            nerr++;
    }

    if (ts->getop(ts, POLDEK_OP_TEST) && !ts->getop(ts, POLDEK_OP_RPMTEST))
        goto l_end;
    
    run_uninstall = 1;
    if (!ts->getop(ts, POLDEK_OP_RPMTEST)) {
        run_uninstall = poldek__ts_confirm(ts);
    }
    
    if (run_uninstall) {
        int vrfy = 0;
            
        if (!pm_pmuninstall(ts->db, ordered_pkgs, ts)) {
            nerr++;
            vrfy = 1;
        }
            
        if (poldek_ts_issetf(ts, POLDEK_TS_TRACK))
            update_iinf(ts, pkgs, ts->db, vrfy);
    }

 l_end:
    if (ordered_pkgs)
        n_array_free(ordered_pkgs);
    
    uninstall_ctx_free(uctx);
    return nerr == 0;
}


static
void uninstall_summary(struct poldek_ts *ts, tn_array *pkgs, int ndep)
{
    poldek__ts_update_summary(ts, "R", pkgs, PKGMARK_MARK, ts->pms);
    if (ndep)
        poldek__ts_update_summary(ts, "D", pkgs, PKGMARK_DEP, ts->pms);
    poldek__ts_display_summary(ts);
}


static
void update_iinf(struct poldek_ts *ts, tn_array *pkgs, struct pkgdb *db,
                 int vrfy)
{
    int i, is_installed = 0;
    
    if (vrfy) {
        pkgdb_reopen(db, O_RDONLY);
        is_installed = 1;
    }

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        if (vrfy)
            is_installed = pkgdb_is_pkg_installed(db, pkg, NULL);
        
        if (!is_installed)
            n_array_push(ts->pkgs_removed, pkg_link(pkg));
    }
    
    if (vrfy) 
        pkgdb_close(db);
}
