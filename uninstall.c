/*
  Copyright (C) 2000 - 2005 Pawel A. Gajda <mis@k2.net.pl>

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
#include "dbpkgset.h"
#include "poldek_ts.h"
#include "capreq.h"
#include "pm/pm.h"

#define DBPKG_ORPHANS_PROCESSED   (1 << 15) /* is its orphan processed ?*/
#define DBPKG_DEPS_PROCESSED      (1 << 16) /* is its deps processed? */
#define DBPKG_TOUCHED             (1 << 17)
#define DBPKG_REV_ORPHANED        (1 << 19)


#define uninst_LDFLAGS (PKG_LDNEVR | PKG_LDCAPS | PKG_LDREQS | PKG_LDFL_DEPDIRS)


static void print_summary(tn_array *pkgs, struct pkgmark_set *pms, int ndep,
                          int simple);
static void update_poldek_iinf(struct poldek_iinf *iinf, tn_array *pkgs,
                               struct pkgdb *db, int vrfy);
struct uninstall_ctx *uctx;
static int process_package(int indent, struct uninstall_ctx *uctx,
                           struct pkg *pkg);

struct uninstall_ctx {
    struct pkgdb       *db;
    struct poldek_ts   *ts;
    struct dbpkg_set   *uninst_set;
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
                               uctx->uninst_set->dbpkgs, ldflags);

    if (pkg->caps)
        for (i=0; i < n_array_size(pkg->caps); i++) {
            struct capreq *cap = n_array_nth(pkg->caps, i);
            n += pkgdb_q_what_requires(uctx->db, orphans, cap,
                                       uctx->uninst_set->dbpkgs, ldflags);
        }
    
    if (pkg->fl) {
        struct pkgfl_it it;
        const char *path;

        pkgfl_it_init(&it, pkg->fl);
        while ((path = pkgfl_it_get_next(&it, NULL))) {
            struct capreq *cap;
            capreq_new_name_a(path, cap);
            n += pkgdb_q_what_requires(uctx->db, orphans, cap, 
                                       uctx->uninst_set->dbpkgs,
                                       ldflags);
        }
    }
    
    MEMINF("END");
    
    if (n_array_size(orphans) == 0) {
        n_array_free(orphans);
        orphans = NULL;
    }

    return orphans;
}

static
int pkg_leave_orphans(struct uninstall_ctx *uctx, struct pkg *pkg)
{
    struct capreq *selfcap;
    int i;
    
    capreq_new_name_a(pkg->name, selfcap);
    if (pkgdb_q_is_required(uctx->db, selfcap, uctx->uninst_set->dbpkgs))
        return 1;

    if (pkg->caps)
        for (i=0; i < n_array_size(pkg->caps); i++) {
            struct capreq *cap = n_array_nth(pkg->caps, i);
            if (pkgdb_q_is_required(uctx->db, cap, uctx->uninst_set->dbpkgs))
                return 1;
        }
    
    if (pkg->fl) {
        struct pkgfl_it it;
        const char *path;

        pkgfl_it_init(&it, pkg->fl);
        while ((path = pkgfl_it_get_next(&it, NULL))) {
            struct capreq *cap;
            capreq_new_name_a(path, cap);
            if (pkgdb_q_is_required(uctx->db, cap, uctx->uninst_set->dbpkgs))
                return 1;
        }
    }
    
    return 0;
}


/*
  adds to uninst_set packages required by pkg and which
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
                     uctx->uninst_set->dbpkgs, uninst_LDFLAGS);

        pkgdb_search(uctx->db, &dbpkgs, PMTAG_CAP, capreq_name(req),
                     uctx->uninst_set->dbpkgs, uninst_LDFLAGS);

        if (dbpkgs == NULL)
            continue;
        
        for (j=0; j < n_array_size(dbpkgs); j++) {
            struct pkg *dbpkg = n_array_nth(dbpkgs, j);

            if (pkg_isset_mf(uctx->pms, dbpkg, DBPKG_REV_ORPHANED))
                continue;       /* was there */

            msgn_i(3, indent, "  %s requires %s", pkg_id(pkg), pkg_id(dbpkg));
            
            if (pkg_leave_orphans(uctx, dbpkg))
                continue;
            
            msgn_i(1, indent, "%s marks orphaned %s (req %s)",
                   pkg_id(pkg), pkg_id(dbpkg), capreq_snprintf_s(req));

            pkg_set_mf(uctx->pms, dbpkg, DBPKG_REV_ORPHANED);
            pkg_dep_mark(uctx->ts->pms, dbpkg);
            dbpkg_set_add(uctx->uninst_set, pkg_link(dbpkg));
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
    int i;
    
    if (sigint_reached())
        return 0;

    if (uctx->nerr_fatal)
        return 0;

    if (pkg->reqs == NULL)
        return 1;

    if (pkg_is_marked(uctx->ts->pms, pkg)) {
        DBGF("%s: obsoleted, return\n", pkg_id(pkg)); 
        //n_assert(0);
        //db_deps_remove_pkg(uctx->db_deps, pkg);
        return 1;
    }
    MEMINF("START");
    DBGF("%s\n", pkg_id(pkg));

    for (i=0; i < n_array_size(pkg->reqs); i++) {
        struct capreq *req = n_array_nth(pkg->reqs, i);
        
        if (capreq_is_rpmlib(req)) 
            continue;

        DBGF("req %s\n", capreq_snprintf_s(req));

        if (pkg_satisfies_req(pkg, req, 1)) { /* self match, should be handled
                                                 at lower level; TOFIX */
            DBGF("%s: satisfied by itself\n", capreq_snprintf_s(req));

        } else if (pkgdb_match_req(uctx->db, req, uctx->strict,
                                   uctx->uninst_set->dbpkgs)) {

            DBGF("%s: satisfied by db\n", capreq_snprintf_s(req));
            msg_i(3, indent, "%s: satisfied by db\n", capreq_snprintf_s(req));
            
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
                for (j=0; j < n_array_size(uctx->uninst_set->dbpkgs); j++) {
                    struct pkg *dbpkg = n_array_nth(uctx->uninst_set->dbpkgs, j);
                    DBGF("%s MARKS %s (req %s)?\n", pkg_id(dbpkg), pkg_id(pkg),
                         capreq_snprintf_s(req));
                    
                    if (pkg_satisfies_req(dbpkg, req, uctx->strict)) {
                        bypkg = dbpkg;
                        break;
                    }
                }
            }
            if (bypkg == NULL)  /* unsatisfied requirement */
                continue;
            
            msgn_i(1, bypkg->pri, _("%s marks %s (req %s)"), pkg_id(bypkg),
                   pkg_id(pkg), capreq_snprintf_s(req));

            uctx->ndep++;
            pkg_dep_mark(uctx->ts->pms, pkg);
            dbpkg_set_add(uctx->uninst_set, pkg_link(pkg));
            process_package(indent + 2, uctx, pkg);
        }
    }
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
    DBGF("PROCESSING [%d] %s\n", indent, pkg_id(pkg));

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
    uctx->uninst_set = dbpkg_set_new();
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
    for (i=0; i < n_array_size(uctx->uninst_set->dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(uctx->uninst_set->dbpkgs, i);
        msgn(1, "freedbset %d %s", dbpkg->_refcnt, pkg_id(dbpkg));
    }
#endif    
    dbpkg_set_free(uctx->uninst_set);
    
    pkgmark_set_free(uctx->pms);
    free(uctx);
};

static int do_process(struct uninstall_ctx *uctx)
{
    int i, n = 0;
    tn_array *tmp;

    for (i=0; i < n_array_size(uctx->uninst_set->dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(uctx->uninst_set->dbpkgs, i);
        msgn(1, "mark %s", pkg_id(dbpkg));
        pkg_hand_mark(uctx->ts->pms, dbpkg);
        n++;
    }

    if (!uctx->ts->getop(uctx->ts, POLDEK_OP_FOLLOW))
        return n;
    
    MEMINF("startdeps");
    msgn(1, _("Processing dependencies..."));
    
    tmp = n_array_dup(uctx->uninst_set->dbpkgs, (tn_fn_dup)pkg_link);
    for (i=0; i < n_array_size(tmp); i++) {
        struct pkg *dbpkg = n_array_nth(tmp, i);
        process_package(0, uctx, dbpkg);
    }
    n_array_free(tmp);
    MEMINF("enddeps");
    
    return n;
}

static int resolve_packages(struct uninstall_ctx *uctx, struct poldek_ts *ts)
{
    int               i, nerr = 0;
    tn_array          *masks;
    
    masks = poldek_ts_get_args_asmasks(ts, 1);
    
    for (i=0; i < n_array_size(masks); i++) {
        char           *mask, *p;
        tn_array       *dbpkgs;
        struct capreq  *cr, *cr_evr;
        int            nmatches = 0;

        
        mask = n_array_nth(masks, i);
#if 0 //DUPA        
        if ((pdef->tflags & PKGDEF_REGNAME) == 0) {
            logn(LOGERR, _("'%s': only exact selection is supported"),
                 pdef->virtname);
            nerr++;
            continue;
        }
#endif
        cr = NULL; cr_evr = NULL;
        if ((p = strchr(mask, '#')) == NULL) {
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

            if (poldek_util_parse_evr(p, &epoch, &ver, &rel))
                cr = cr_evr = capreq_new(NULL, tmp, epoch, ver, rel, REL_EQ, 0);
        }
        DBGF("get_provides %s\n", capreq_snprintf_s(cr));
        dbpkgs = pkgdb_get_provides_dbpkgs(ts->db, cr, NULL, uninst_LDFLAGS);
        DBGF("mask %s (%s) -> %d package(s)\n", mask, capreq_snprintf_s(cr), 
               dbpkgs ? n_array_size(dbpkgs) : 0);
        
        if (dbpkgs) {
            int j;
            
            for (j=0; j < n_array_size(dbpkgs); j++) {
                struct pkg *dbpkg = n_array_nth(dbpkgs, j);
                int matched = 0;

                DBGF("  - %s (%s?)\n", pkg_id(dbpkg),
                     capreq_snprintf_s(cr_evr ? cr_evr : cr));
                
                if (cr_evr) {
                    if (ts->getop(ts, POLDEK_OP_CAPLOOKUP)) {
                        if (pkg_xmatch_req(dbpkg, cr_evr, POLDEK_MA_PROMOTE_REQEPOCH))
                            matched = 1;
                    } else {
                        if (strcmp(dbpkg->name, capreq_name(cr_evr)) == 0) {
                            DBGF("n (%s, %s) %d\n", dbpkg->name,
                                 capreq_name(cr_evr), 
                                 pkg_evr_match_req(dbpkg, cr_evr,
                                                   POLDEK_MA_PROMOTE_REQEPOCH));
                            
                        }
                        
                        if (strcmp(dbpkg->name, capreq_name(cr_evr)) == 0 &&
                            pkg_evr_match_req(dbpkg, cr_evr,
                                              POLDEK_MA_PROMOTE_REQEPOCH))
                            matched = 1;
                    }
                    if (matched)
                        nmatches++;

                } else if (cr_evr == NULL && strcmp(mask, dbpkg->name) == 0) {
                    nmatches++;
                    matched = 1;
                }
                
                if (matched)
                    dbpkg_set_add(uctx->uninst_set, pkg_link(dbpkg));
            }
            n_array_free(dbpkgs);
        }
        
        
        if (nmatches == 0) {
            logn(LOGERR, _("%s: no such package"), mask);
            nerr++;
        }

        if (cr_evr)
            capreq_free(cr_evr);
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


int do_poldek_ts_uninstall(struct poldek_ts *ts, struct poldek_iinf *iinf)
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
    
    n_array_uniq(uctx->uninst_set->dbpkgs);
    if (nerr == 0 && n_array_size(uctx->uninst_set->dbpkgs)) {
        do_process(uctx);
        pkgs = uctx->uninst_set->dbpkgs;
    }
    pkgdb_close(ts->db); /* release db as soon as possible */
    
    if (nerr || pkgs == NULL)
        goto l_end;
    
    ordered_pkgs = reorder_packages(pkgs);
    print_summary(pkgs, ts->pms, uctx->ndep,
                  ts->getop(ts, POLDEK_OP_PARSABLETS));

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
        if (ts->getop(ts, POLDEK_OP_CONFIRM_UNINST) && ts->ask_fn)
            run_uninstall = ts->ask_fn(0, _("Proceed? [y/N]"));
    }
    
    if (run_uninstall) {
        int vrfy = 0;
            
        if (!pm_pmuninstall(ts->db, ordered_pkgs, ts)) {
            nerr++;
            vrfy = 1;
        }
            
        if (iinf)
            update_poldek_iinf(iinf, pkgs, ts->db, vrfy);
    }

 l_end:
    if (ordered_pkgs)
        n_array_free(ordered_pkgs);
    
    uninstall_ctx_free(uctx);
    return nerr == 0;
}


static
void print_summary(tn_array *pkgs, struct pkgmark_set *pms, int ndep, int simple)
{
    int n = n_array_size(pkgs);
    
#ifndef ENABLE_NLS    
    msg(0, "There are %d package%s to remove", n, n > 1 ? "s":"");
    if (ndep) 
        msg(0, _("_ (%d marked by dependencies)"), ndep);
    
#else
    msg(0, ngettext("There are %d package to remove",
                    "There are %d packages to remove", n), n);

    if (ndep) 
        msg(0, ngettext("_ (%d marked by dependencies)",
                        "_ (%d marked by dependencies)", ndep), ndep);
#endif    
    msg(0, "_:\n");
    
    packages_iinf_display(0, "R", pkgs, pms, PKGMARK_MARK, simple);
    packages_iinf_display(0, "D", pkgs, pms, PKGMARK_DEP, simple);
}


static
void update_poldek_iinf(struct poldek_iinf *iinf, tn_array *pkgs,
                        struct pkgdb *db, int vrfy)
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
            n_array_push(iinf->uninstalled_pkgs, pkg_link(pkg));
    }
    
    if (vrfy) 
        pkgdb_close(db);
}
