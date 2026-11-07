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

#include <vfile/vfile.h>
#include "pkgdir/pkgdir.h"
#include "ictx.h"
#include "iset.h"
#include "pm_hooks.h"

static int verify_held_packages(struct i3ctx *ictx)
{
    int i, j, rc = 1;
    const tn_array *unpkgs;

    if (poldek_ts_issetf(ictx->ts, POLDEK_TS_UPGRADE) == 0)
        return 1;

    if (ictx->ts->hold_patterns == NULL ||
        !ictx->ts->getop(ictx->ts, POLDEK_OP_HOLD))
        return 1;

    unpkgs = iset_packages(ictx->unset);

    for (i=0; i < n_array_size(unpkgs); i++) {
        struct pkg *dbpkg;
        struct pkgscore_s psc;

        dbpkg = n_array_nth(unpkgs, i);
        pkgscore_match_init(&psc, dbpkg);

        for (j=0; j < n_array_size(ictx->ts->hold_patterns); j++) {
            const char *mask = n_array_nth(ictx->ts->hold_patterns, j);
            if (pkgscore_match(&psc, mask)) {
                logn(LOGERR, _("%s: refusing to uninstall held package"),
                     pkg_id(dbpkg));
                rc = 0;
                break;
            }
        }
    }

    return rc;
}

static int valid_arch_os(struct poldek_ts *ts, const tn_array *pkgs)
{
    int i, nerr = 0;

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        if (!poldek_conf_MULTILIB &&
            !ts->getop(ts, POLDEK_OP_IGNOREARCH) && pkg->_arch &&
            !pm_machine_score(ts->pmctx, PMMSTAG_ARCH, pkg_arch(pkg)))
         {
             logn(LOGERR, _("%s: package is for a different architecture (%s)"),
                  pkg_id(pkg), pkg_arch(pkg));
             nerr++;
         }

        if (!ts->getop(ts, POLDEK_OP_IGNOREOS) && pkg->_os &&
            !pm_machine_score(ts->pmctx, PMMSTAG_OS, pkg_os(pkg)))
         {
             logn(LOGERR, _("%s: package is for a different operating "
                            "system (%s)"), pkg_id(pkg), pkg_os(pkg));
             nerr++;
         }
    }

    return nerr == 0;
}

static void install_summary(struct i3ctx *ictx)
{
    const struct pkgmark_set *pms = iset_pms(ictx->inset); /* for short */
    const tn_array *ipkgs = iset_packages(ictx->inset); /* for short */

    poldek__ts_update_summary(ictx->ts, "I", ipkgs, PKGMARK_MARK, pms);
    poldek__ts_update_summary(ictx->ts, "D", ipkgs, PKGMARK_DEP, pms);
    poldek__ts_update_summary(ictx->ts, "R", iset_packages(ictx->unset),
                              0, NULL);

    poldek__ts_display_summary(ictx->ts);

    if (ictx->ts->fetchdir == NULL)
        packages_fetch_summary(ictx->ts->pmctx, ipkgs,
                               ictx->ts->cachedir, ictx->ts->fetchdir ? 1 : 0);
}


static void update_iinf(struct i3ctx *ictx, int vrfy)
{
    int i, is_installed = 1;
    const tn_array *unpkgs = iset_packages(ictx->unset);
    const tn_array *inpkgs = iset_packages(ictx->inset);

    if (vrfy)
        pkgdb_reopen(ictx->ts->db, O_RDONLY);

    for (i=0; i < n_array_size(inpkgs); i++) {
        struct pkg *pkg = n_array_nth(inpkgs, i);

        if (vrfy) {
            int cmprc = 0;

            is_installed = i3_is_pkg_installed(ictx->ts, pkg, &cmprc);
            if (is_installed && cmprc != 0)
                is_installed = 0;
        }

        if (is_installed)
            n_array_push(ictx->ts->pkgs_installed, pkg_link(pkg));
    }

    if (vrfy == 0)
        is_installed = 0;

    for (i=0; i < n_array_size(unpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(unpkgs, i);
        struct pkg *pkg = dbpkg;


        if (vrfy) {
            int cmprc = 0;

            is_installed = i3_is_pkg_installed(ictx->ts, pkg, &cmprc);
            if (is_installed && cmprc != 0)
                is_installed = 0;
        }

        if (is_installed == 0)
            n_array_push(ictx->ts->pkgs_removed, pkg_link(pkg));
    }

    if (vrfy)
        pkgdb_close(ictx->ts->db);
}

static void print_dependency_errors(int nunmet_deps, int nconflicts)
{
    int n = 0;
    char errmsg[256];

    if (nunmet_deps) {
#ifndef ENABLE_NLS
        n += n_snprintf(&errmsg[n], sizeof(errmsg) - n,
                        "%d unresolved dependencies", nunmet_deps);
#else
        n += n_snprintf(&errmsg[n], sizeof(errmsg) - n,
                        ngettext("%d unresolved dependency",
                                 "%d unresolved dependencies", nunmet_deps),
                        nunmet_deps);
#endif
    }

    if (nconflicts) {
        n += n_snprintf(&errmsg[n], sizeof(errmsg) - n,
                        "%s%d conflicts", n ? ", ":"", nconflicts);
    }

    logn(LOGERR, "%s", errmsg);
}

static int do_install(struct i3ctx *ictx, tn_array *pkgs)
{
    struct poldek_ts *ts = ictx->ts;
    uint32_t ts_flags = ts->_flags;
    int ts_op_force = ts->getop(ts, POLDEK_OP_FORCE);
    struct pkg *multi_upgrade_pkg = NULL;
    int rc = 0;

    if (n_array_size(pkgs) == 1) {
        struct pkg *pkg = n_array_nth(pkgs, 0);
        tn_array *obsoleted;

        if ((obsoleted = n_hash_get(ictx->multi_obsoleted, pkg_id(pkg)))) {
            //pkgs_array_dump(obsoleted, "multi_obsoleted");
            multi_upgrade_pkg = n_array_nth(obsoleted, 0);
        }
    }

    if (multi_upgrade_pkg == NULL) { /* ordinary installation */
        return pm_pminstall(ts->db, pkgs, iset_packages(ictx->unset), ts);
    }

    /*
      if multiple instances of pkg are installed, we have to install new version first
      and then uninstall old one (keeping other versions untouch) as rpm does not
      support such scenario
    */

    /* force if reinstall is set and same evr  */
    if (poldek_ts_issetf(ts, POLDEK_TS_REINSTALL) &&
        pkg_cmp_name_evr(n_array_nth(pkgs, 0), multi_upgrade_pkg) == 0) {
        ts->setop(ts, POLDEK_OP_FORCE, 1);
    }

    poldek_ts_clrf(ts, POLDEK_TS_UPGRADE | POLDEK_TS_DOWNGRADE | POLDEK_TS_REINSTALL);

    //pkgs_array_dump(pkgs, "inst");
    DBGF("MULTI_UPGRADE %s\n", multi_upgrade_pkg ? pkg_id(multi_upgrade_pkg) : NULL);

    const tn_array *unpkgs = iset_packages(ictx->unset);
    rc = pm_pminstall(ts->db, pkgs, unpkgs, ts);

    if (rc && multi_upgrade_pkg && pkg_cmp_name_evr(n_array_nth(pkgs, 0), multi_upgrade_pkg) != 0) {
        n_assert(n_array_size(unpkgs) == 1);
        n_assert(pkg_cmp_name_evr(n_array_nth(unpkgs, 0), multi_upgrade_pkg) == 0);

        ts->setop(ts, POLDEK_OP_FORCE, 0);
        if (!pm_pmuninstall(ts->db, unpkgs, ts))
            rc = 0;
    }

    /* restore ts flags&force op */
    ts->setop(ts, POLDEK_OP_FORCE, ts_op_force);
    ts->_flags = ts_flags;

    return rc;
}

static tn_array *iset_packages_in_install_order(struct i3ctx *ictx)
{
    const tn_array *inpkgs = iset_packages(ictx->inset);
    tn_array *pkgs = NULL;

    pkgset_order(ictx->ps, inpkgs, &pkgs, PKGORDER_INSTALL);
    n_assert(pkgs);
    n_assert(n_array_size(pkgs) == n_array_size(inpkgs));

    return pkgs;
}

int poldek_log__get_last_error_distance();
int poldek_term_get_height(void);

#define CHOOSE_AGAIN (-1)       /* install_packages(): r at Proceed? */

/* the --caplookup choices whose package the set took */
static tn_array *set_icaps(struct i3ctx *ictx)
{
    tn_array *icaps = n_array_new(2, NULL, NULL);
    int i;

    for (i = 0; i < n_array_size(ictx->icaps); i++) {
        struct i3icap *icap = n_array_nth(ictx->icaps, i);

        if (icap->chosen &&
            iset_ismarkedf(ictx->inset, icap->chosen, PKGMARK_INTERNAL))
            n_array_push(icaps, icap);
    }

    return icaps;
}

static int icap_chosen_on_terminal(struct i3ctx *ictx)
{
    tn_array *icaps = set_icaps(ictx);
    int i, yes = 0;

    for (i = 0; i < n_array_size(icaps); i++)
        if (((struct i3icap *)n_array_nth(icaps, i))->on_terminal)
            yes = 1;

    n_array_free(icaps);
    return yes;
}

static int install_packages(struct i3ctx *ictx)
{
    tn_array *toinstall;
    tn_array *pkgs = NULL;
    struct poldek_ts *ts = ictx->ts;
    int i, rc = 1;

    toinstall = n_array_dup(iset_packages(ictx->inset), (tn_fn_dup)pkg_link);
    msgn(1, _("Processing dependencies..."));
    //pkgs_array_dump(toinstall, "inset");

    for (i = 0; i < n_array_size(toinstall); i++) {
        struct pkg *pkg = n_array_nth(toinstall, i);

        DBGF("%s\n", pkg_id(pkg));
        DBGF("%s %d\n", pkg_id(pkg), i3_is_hand_marked(ictx, pkg));
        i3_install_package(ictx, pkg);

        if (sigint_reached())
            break;
    }
    n_array_free(toinstall);

    if (ictx->skipped)
        return 1;

    i3_return_zero_if_stoppped(ictx);

    i3_report_db_conflicts(ictx);
    install_summary(ictx);
    pkgdb_close(ts->db); /* release db as soon as possible */

    if (i3_get_nerrors(ictx, I3ERR_CLASS_DEP|I3ERR_CLASS_CNFL)) {
        int nunmet_deps = i3_get_nerrors(ictx, I3ERR_CLASS_DEP);
        int nconflicts = i3_get_nerrors(ictx, I3ERR_CLASS_CNFL);

        if (nunmet_deps || nconflicts) {
            int term_height = poldek_term_get_height() - nconflicts - nunmet_deps;
            int log_errors = poldek_log__get_last_error_distance() > term_height;

            print_dependency_errors(nunmet_deps, nconflicts);

            if (log_errors)  /* log errors again as they are scrolled out */
                i3_log_errors(ictx);

            rc = 0;
        }

        if (nunmet_deps) {
            if (!ts->getop_v(ts, POLDEK_OP_NODEPS, POLDEK_OP_RPMTEST, 0))
                goto l_end;
        }

        if (nconflicts) {
            if (!ts->getop_v(ts, POLDEK_OP_FORCE, POLDEK_OP_RPMTEST, 0))
                goto l_end;
        }
    }

    pkgs = iset_packages_in_install_order(ictx);
    //pkgs_array_dump(pkgs, "inset.ordered");

    if (ts->getop_v(ts, POLDEK_OP_JUSTPRINT, POLDEK_OP_JUSTPRINT_N, 0)) {
        rc = packages_dump(pkgs, ts->dumpfile,
                           ts->getop(ts, POLDEK_OP_JUSTPRINT_N) == 0);
        goto l_end;
    }

    if ((ts->getop_v(ts, POLDEK_OP_JUSTPRINT, POLDEK_OP_JUSTPRINT_N,
                     POLDEK_OP_JUSTFETCH, 0)) == 0) {
        if (!valid_arch_os(ictx->ts, iset_packages(ictx->inset))) {
            rc = 0;
            goto l_end;
        }
    }

    /* poldek's test only  */
    if (ts->getop(ts, POLDEK_OP_TEST) && !ts->getop(ts, POLDEK_OP_RPMTEST))
        goto l_end;

    if (ts->getop(ts, POLDEK_OP_JUSTFETCH)) {
        const char *destdir = ts->fetchdir;
        if (destdir == NULL)
            destdir = ts->cachedir;

        rc = packages_fetch(ts->pmctx, pkgs, destdir, ts->fetchdir ? 1 : 0);

    } else if (!ts->getop(ts, POLDEK_OP_HOLD) || (rc = verify_held_packages(ictx))) {
        int is_test = ts->getop(ts, POLDEK_OP_RPMTEST);

        if (!is_test) {
            if (icap_chosen_on_terminal(ictx))
                ts->_iflags |= TS_CHOSEN;

            switch (poldek__ts_confirm(ts)) {
                case 1:
                    break;
                case 0:         /* the set is skipped */
                    rc = 1;
                    goto l_end;
                case POLDEK_ANSWER_REDO:
                    rc = CHOOSE_AGAIN;
                    goto l_end;
                default:        /* aborted */
                    rc = 0;
                    goto l_end;
            }
        }

        if (!ts->getop(ts, POLDEK_OP_NOFETCH))
            if (!packages_fetch(ts->pmctx, pkgs, ts->cachedir, 0)) {
                rc = 0;
                goto l_end;
            }

        rc = do_install(ictx, pkgs);

        if (!is_test && poldek_ts_issetf(ictx->ts, POLDEK_TS_TRACK))
            update_iinf(ictx, rc <= 0);

        if (rc && !ts->getop_v(ts, POLDEK_OP_RPMTEST, POLDEK_OP_KEEP_DOWNLOADS,
                               POLDEK_OP_NOFETCH, 0))
            packages_fetch_remove(pkgs, ts->cachedir);
    }

l_end:
    n_array_cfree(&pkgs);

    return rc;
}


static void ts_reset(struct poldek_ts *ts)
{
    n_hash_clean(ts->ts_summary);
}

extern int i3_pre_ts_install(struct i3ctx *ictx, tn_array **pkgs);

/* Undoes the set processed, for r at its Proceed?: the requested packages
   it took are marked again and its --caplookup choices, put into icaps,
   asked again. Returns 0 after q. */
static int choose_again(struct i3ctx *ictx, tn_array *icaps)
{
    struct poldek_ts *ts = ictx->ts;
    const tn_array *inpkgs = iset_packages(ictx->inset);
    tn_array *set = set_icaps(ictx);
    int i, rc = 1;

    for (i = 0; i < n_array_size(inpkgs); i++) {
        struct pkg *pkg = n_array_nth(inpkgs, i);

        if (iset_ismarkedf(ictx->inset, pkg, PKGMARK_INTERNAL))
            pkg_mark_i(ts->pms, pkg);
    }

    ts_reset(ts);
    i3ctx_reset(ictx);
    ts->_iflags &= ~TS_CHOSEN;
    pkgdb_reopen(ts->db, 0);

    /* all asked, then the answers marked: a provider chosen for two
       capabilities stays while one keeps it (a package named on the
       command line is no candidate, see verify_package_caps()) */
    for (i = 0; i < n_array_size(set); i++) {
        struct i3icap *icap = n_array_nth(set, i);
        struct capreq *cap;
        struct pkg *pkg;

        capreq_new_name_a(icap->cap, cap);
        pkg = i3_choose_equiv(ts, NULL, cap, icap->candidates, icap->chosen);
        if (pkg == NULL && poldek__ts_cancelled(ts)) { /* q */
            rc = 0;
            break;
        }

        if (pkg == NULL)
            msgn(0, _("\"%s\" left out"), icap->cap);

        pkg_unmark_i(ts->pms, icap->chosen);
        icap->chosen = pkg;
        n_array_push(icaps, icap);
    }

    for (i = 0; rc && i < n_array_size(icaps); i++) {
        struct i3icap *icap = n_array_nth(icaps, i);

        if (icap->chosen == NULL || pkg_is_marked_i(ts->pms, icap->chosen))
            continue;

        if (i3_is_pkg_installable(ts, icap->chosen, 1) > 0)
            pkg_mark_i(ts->pms, icap->chosen);
        else
            icap->chosen = NULL;
    }

    n_array_free(set);
    return rc;
}

/* marks the set again after choose_again(): head and its name group with
   particle install, else every package requested; and the providers of
   the set's --caplookup choices */
static void mark_set_again(struct i3ctx *ictx, struct pkg *head,
                           tn_array *pkgs, tn_array *icaps)
{
    struct poldek_ts *ts = ictx->ts;
    int i;

    if (ts->getop(ts, POLDEK_OP_PARTICLE)) {
        if (pkg_is_marked_i(ts->pms, head))
            i3_mark_package(ictx, head, PKGMARK_MARK);
        i3_mark_namegroup(ictx, head, ts->ctx->ps->pkgs);

    } else {
        for (i = 0; i < n_array_size(pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgs, i);

            if (pkg_is_marked_i(ts->pms, pkg) && !i3_is_marked(ictx, pkg))
                i3_mark_package(ictx, pkg, PKGMARK_MARK);
        }
    }

    for (i = 0; i < n_array_size(icaps); i++) {
        struct pkg *pkg = ((struct i3icap *)n_array_nth(icaps, i))->chosen;

        if (pkg && pkg_is_marked_i(ts->pms, pkg) && !i3_is_marked(ictx, pkg))
            i3_mark_package(ictx, pkg, PKGMARK_MARK);
    }
}

/* installs the set marked, which head begins, again for every r */
static int install_set(struct i3ctx *ictx, struct pkg *head, tn_array *pkgs)
{
    tn_array *icaps = n_array_new(2, NULL, NULL);
    int rc;

    while ((rc = install_packages(ictx)) == CHOOSE_AGAIN) {
        n_array_clean(icaps);
        if (!choose_again(ictx, icaps)) {
            rc = 0;
            break;
        }

        mark_set_again(ictx, head, pkgs, icaps);
        if (n_array_size(iset_packages(ictx->inset)) == 0) {
            rc = 1;             /* all left out */
            break;
        }
    }

    n_array_free(icaps);
    return rc;
}

int i3_do_poldek_ts_install(struct poldek_ts *ts)
{
    int i, nerr = 0, n, is_particle;
    struct i3ctx ictx;
    tn_array *pkgs = NULL;

    n_assert(ts->type == POLDEK_TS_INSTALL);

    i3ctx_init(&ictx, ts);      /* keeps --caplookup's choices too */

    if ((n = i3_pre_ts_install(&ictx, &pkgs)) <= 0) {
        n_assert(pkgs == NULL);
        i3ctx_destroy(&ictx);
        return n == 0 ? 1 : 0;    /* report success(1) if 'nothing to do' */
    }
    n_assert(pkgs);
    n_assert(n_array_size(pkgs) > 0);

    is_particle = ts->getop(ts, POLDEK_OP_PARTICLE); /* preserve option value */

    if (n_array_size(pkgs) == 1) {
        ts->setop(ts, POLDEK_OP_PARTICLE, 0);

    /* tests make sense on whole set only  */
    } else if (ts->getop_v(ts, POLDEK_OP_TEST, POLDEK_OP_RPMTEST, 0)) {
        ts->setop(ts, POLDEK_OP_PARTICLE, 0);

    /* so JUSTPRINTs */
    } else if (ts->getop_v(ts, POLDEK_OP_JUSTPRINT, POLDEK_OP_JUSTPRINT_N, 0)) {
        ts->setop(ts, POLDEK_OP_PARTICLE, 0);
    }

    if (poldek__is_in_testing_mode())
        ts->setop(ts, POLDEK_OP_PARTICLE, 1);

    for (i = 0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        if (!pkg_is_marked_i(ts->pms, pkg))
            continue;

        if (sigint_reached()) {
            nerr++;
            goto l_end;
        }

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
            pkgdb_reopen(ts->db, 0);
            ts->_iflags &= ~TS_CHOSEN;
            ts->_set_head = pkg;
        }
        DBGF("mark %s\n", pkg_id(pkg));

        i3_mark_package(&ictx, pkg, PKGMARK_MARK);

        if (ts->getop(ts, POLDEK_OP_PARTICLE)) {
            i3_mark_namegroup(&ictx, pkg, ts->ctx->ps->pkgs);

            if (!install_set(&ictx, pkg, pkgs))
                nerr++;

            if (poldek__ts_cancelled(ts) || poldek__ts_hooks_failed(ts))
                break;

            ts_reset(ictx.ts);
            i3ctx_reset(&ictx);
        }
    }

    if (!ts->getop(ts, POLDEK_OP_PARTICLE)) {
        ts->_iflags &= ~TS_CHOSEN;
        nerr = !install_set(&ictx, NULL, pkgs);
    }

 l_end:
    ts->_set_head = NULL;
    i3ctx_destroy(&ictx);
    MEMINF("END");
    if (is_particle)
        ts->setop(ts, POLDEK_OP_PARTICLE, 1);

    n_array_cfree(&pkgs);

    return nerr == 0;
}
