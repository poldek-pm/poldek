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

#include <vfile/vfile.h>
#include "pkgdir/pkgdir.h"
#include "ictx.h"

static int verify_holds(struct install_ctx *ictx)
{
    int i, j, rc = 1;

    if (poldek_ts_issetf(ictx->ts, POLDEK_TS_UPGRADE) == 0)
        return 1;

    if (ictx->ts->hold_patterns == NULL ||
        !ictx->ts->getop(ictx->ts, POLDEK_OP_HOLD))
        return 1;

    for (i=0; i < n_array_size(ictx->uninst_set->dbpkgs); i++) {
        struct pkg *dbpkg; 
        struct pkgscore_s psc;

        dbpkg = n_array_nth(ictx->uninst_set->dbpkgs, i);
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


static int valid_arch_os(struct poldek_ts *ts, tn_array *pkgs) 
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

static void print_install_summary(struct install_ctx *ictx) 
{
    int i, n, simple;
    long int size_download = 0, size_install = 0;
    
    for (i=0; i < n_array_size(ictx->install_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ictx->install_pkgs, i);
        if (pkg->pkgdir && (vf_url_type(pkg->pkgdir->path) & VFURL_REMOTE))
            size_download += pkg->fsize;
        size_install += pkg->size;
    }
    
    n = n_array_size(ictx->install_pkgs);
#ifndef ENABLE_NLS    
    msg(1, "There are %d package%s to install", n, n > 1 ? "s":"");
    if (ictx->ndep) 
        msg(1, _("_ (%d marked by dependencies)"), ictx->ndep);
    
#else
    msg(1, ngettext("There are %d package to install",
                    "There are %d packages to install", n), n);

    if (ictx->ndep) 
        msg(1, ngettext("_ (%d marked by dependencies)",
                        "_ (%d marked by dependencies)", ictx->ndep),
            ictx->ndep);
#endif

    if (n_array_size(ictx->uninst_set->dbpkgs))
        msg(1, _("_, %d to uninstall"), n_array_size(ictx->uninst_set->dbpkgs));
    msg(1, "_:\n");
    
    simple = ictx->ts->getop(ictx->ts, POLDEK_OP_PARSABLETS);
    n_array_sort(ictx->install_pkgs);
    packages_iinf_display(1, "I", ictx->install_pkgs, ictx->ts->pms,
                          PKGMARK_MARK, simple);
    packages_iinf_display(1, "D", ictx->install_pkgs, ictx->ts->pms,
                          PKGMARK_DEP, simple);
    packages_iinf_display(1, "R", ictx->uninst_set->dbpkgs, NULL, 0, simple);
        
    if (ictx->ts->fetchdir == NULL)
        packages_fetch_summary(ictx->ts->pmctx, ictx->install_pkgs,
                               ictx->ts->cachedir, ictx->ts->fetchdir ? 1 : 0);
}

static
void update_iinf(struct install_ctx *ictx, struct poldek_iinf *iinf, int vrfy)
{
    int i, is_installed = 1;
    
    if (vrfy)
        pkgdb_reopen(ictx->ts->db, O_RDONLY);
    
    for (i=0; i<n_array_size(ictx->install_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ictx->install_pkgs, i);
        
        if (vrfy) {
            int cmprc = 0;
            
            is_installed = in_is_pkg_installed(ictx, pkg, &cmprc);
            if (is_installed && cmprc != 0) 
                is_installed = 0;
        }
        
        if (is_installed)
            n_array_push(iinf->installed_pkgs, pkg_link(pkg));
    }
    
    if (vrfy == 0)
        is_installed = 0;
    
    for (i=0; i < n_array_size(ictx->uninst_set->dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(ictx->uninst_set->dbpkgs, i);
        struct pkg *pkg = dbpkg;


        if (vrfy) {
            int cmprc = 0;
            
            is_installed = in_is_pkg_installed(ictx, pkg, &cmprc);
            if (is_installed && cmprc != 0) 
                is_installed = 0;
        }

        if (is_installed == 0)
            n_array_push(iinf->uninstalled_pkgs, pkg_link(pkg));
    }

    if (vrfy) 
        pkgdb_close(ictx->ts->db);
}

static
int show_errors(struct install_ctx *ictx) 
{
    struct poldek_ts *ts = ictx->ts;
    int n = 0, nerr = 0;
    char errmsg[256];
    
    n_assert(ictx->nerr_dep || ictx->nerr_cnfl);
    
    if (ictx->nerr_dep) {
#ifndef ENABLE_NLS
        n += n_snprintf(&errmsg[n], sizeof(errmsg) - n,
                        "%d unresolved dependencies", ictx->nerr_dep);
#else
        n += n_snprintf(&errmsg[n], sizeof(errmsg) - n,
                        ngettext("%d unresolved dependency",
                                 "%d unresolved dependencies", ictx->nerr_dep),
                        ictx->nerr_dep);
#endif    
        
        if (ts->getop_v(ts, POLDEK_OP_NODEPS, POLDEK_OP_RPMTEST, 0))
            ictx->nerr_dep = 0;
        else
            nerr++;
    }
        
    if (ictx->nerr_cnfl) {
        n += n_snprintf(&errmsg[n], sizeof(errmsg) - n,
                        "%s%d conflicts", n ? ", ":"", ictx->nerr_cnfl);
        if (ts->getop_v(ts, POLDEK_OP_FORCE, POLDEK_OP_RPMTEST, 0))
            ictx->nerr_cnfl = 0;
        else
            nerr++;
    }
    
    logn(LOGERR, "%s", errmsg);
    return nerr > 0;
}
    

static
int do_install(struct install_ctx *ictx, struct poldek_iinf *iinf)
{
    int rc = 1, nerr = 0, any_err = 0;
    tn_array *pkgs = NULL;
    struct poldek_ts *ts;
    int i;

    ts = ictx->ts;

    msgn(1, _("Processing dependencies..."));
    for (i = n_array_size(ictx->ps->ordered_pkgs) - 1; i > -1; i--) {
        struct pkg *pkg = n_array_nth(ictx->ps->ordered_pkgs, i);
        if (pkg_is_hand_marked(ictx->ts->pms, pkg)) 
            in_process_package(-2, ictx, pkg, PROCESS_AS_NEW);
        
        if (sigint_reached())
            break;
    }

    if (ictx->nerr_fatal || sigint_reached())
        return 0;

    n_array_sort(ictx->install_pkgs);
    print_install_summary(ictx);
    pkgdb_close(ts->db); /* release db as soon as possible */

    if (ictx->nerr_dep || ictx->nerr_cnfl) {
        any_err++;
        if (show_errors(ictx))
            nerr++;
    }

    rc = (any_err == 0);
    if (nerr)
        goto l_end;

    pkgs = ts__packages_in_install_order(ictx->ts);
    n_assert(n_array_size(pkgs) == n_array_size(ictx->install_pkgs));

    if (ts->getop_v(ts, POLDEK_OP_JUSTPRINT, POLDEK_OP_JUSTPRINT_N, 0)) {
        rc = packages_dump(pkgs, ts->dumpfile,
                           ts->getop(ts, POLDEK_OP_JUSTPRINT_N) == 0);
        goto l_end;
    }

    if ((ts->getop_v(ts, POLDEK_OP_JUSTPRINT, POLDEK_OP_JUSTPRINT_N,
                     POLDEK_OP_JUSTFETCH, 0)) == 0) {
        if (!valid_arch_os(ictx->ts, ictx->install_pkgs)) {
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

    } else if (!ts->getop(ts, POLDEK_OP_HOLD) || (rc = verify_holds(ictx))) {
        int is_test = ts->getop(ts, POLDEK_OP_RPMTEST);

        if (!is_test && ts->getop(ts, POLDEK_OP_CONFIRM_INST) && ts->ask_fn) {
            if (!ts->ask_fn(1, _("Proceed? [Y/n]"))) {
                rc = 1;
                goto l_end;
            }
        }
        
        if (!ts->getop(ts, POLDEK_OP_NOFETCH))
            if (!packages_fetch(ts->pmctx, pkgs, ts->cachedir, 0)) {
                rc = 0;
                goto l_end;
            }
        
        rc = pm_pminstall(ts->db, pkgs, ictx->uninst_set->dbpkgs, ts);
        
        if (!is_test && iinf)
            update_iinf(ictx, iinf, rc <= 0);

        if (rc && !ts->getop_v(ts, POLDEK_OP_RPMTEST, POLDEK_OP_KEEP_DOWNLOADS,
                               POLDEK_OP_NOFETCH, 0))
            packages_fetch_remove(pkgs, ts->cachedir);
    }

l_end:
    if (pkgs)
        n_array_free(pkgs);
    
    return rc;
}

static int package_is_duplicate(const struct pkg *pkg, const struct pkg *pkg2)
{
    if (pkg_cmp_name(pkg, pkg2) != 0)
        return 0;

    if (poldek_conf_MULTILIB && pkg_cmp_arch(pkg, pkg2) != 0)
        return 0;

    return 1;
}

static
int unmark_name_duplicates(struct pkgmark_set *pms, tn_array *pkgs) 
{
    struct pkg *pkg, *pkg2;
    int i, n, nmarked = 0;

    if (n_array_size(pkgs) < 2)
        return n_array_size(pkgs);
    
    n_array_sort(pkgs);

    i = n = 0;
    while (i < n_array_size(pkgs)) {
        pkg = n_array_nth(pkgs, i);
        i++;
        
        if (!pkg_is_marked(pms, pkg))
            continue;
        
        nmarked++;
        DBGF("%s\n", pkg_id(pkg));
        
        if (i == n_array_size(pkgs))
            break;

        pkg2 = n_array_nth(pkgs, i);
        while (package_is_duplicate(pkg, pkg2)) {
            pkg_unmark(pms, pkg2);
            DBGF("  unmark %s\n", pkg_id(pkg2));
            n++;
            i++;
            if (i == n_array_size(pkgs))
                break;
            pkg2 = n_array_nth(pkgs, i);
        }
    }
    
    return nmarked;
}

int in_do_poldek_ts_install(struct poldek_ts *ts, struct poldek_iinf *iinf)
{
    int i, nmarked = 0, nerr = 0, n, is_particle;
    struct install_ctx ictx;
    n_assert(ts->type == POLDEK_TS_INSTALL);

    if (in_prepare_icaps(ts) < 0) /* user aborts, no error */
        return 1;
    
    if (unmark_name_duplicates(ts->pms, ts->ctx->ps->pkgs) == 0) {
        msgn(1, _("Nothing to do"));
        return 1;
    }
    
    MEMINF("START");
    install_ctx_init(&ictx, ts);

    /* preserve option value */
    is_particle = ts->getop(ts, POLDEK_OP_PARTICLE);
    
    /* tests make sense on whole set only  */
    if (ts->getop_v(ts, POLDEK_OP_TEST, POLDEK_OP_RPMTEST, 0))
        ts->setop(ts, POLDEK_OP_PARTICLE, 0);

    /* so JUSTPRINTs */
    if (ts->getop_v(ts, POLDEK_OP_JUSTPRINT, POLDEK_OP_JUSTPRINT_N, 0))
        ts->setop(ts, POLDEK_OP_PARTICLE, 0);
    
    
    for (i = 0; i < n_array_size(ictx.ps->ordered_pkgs); i++) {
        struct pkg    *pkg = n_array_nth(ictx.ps->ordered_pkgs, i);
        int           install;

        if (!pkg_is_marked(ts->pms, pkg))
            continue;

        if (sigint_reached())
            goto l_end;
        
        install = in_is_pkg_installable(&ictx, pkg, 1);
        
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
#if DEVEL                        /* debug */
    for (i = 0; i < n_array_size(ictx.ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ictx.ps->ordered_pkgs, i);
        if (pkg_is_marked_i(ts->pms, pkg)) 
            printf("MARKED %s\n", pkg_id(pkg));
    }
#endif    
    for (i = 0; i < n_array_size(ictx.ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ictx.ps->ordered_pkgs, i);

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
            pkgdb_reopen(ts->db, 0);
        }
        
        in_mark_package(&ictx, pkg);

        if (ts->getop(ts, POLDEK_OP_PARTICLE)) {
            in_mark_namegroup(&ictx, pkg, pkg->pkgdir->pkgs);
                
            if (!do_install(&ictx, iinf))
                nerr++;

            pkgmark_massset(ts->pms, 0, PKGMARK_MARK | PKGMARK_DEP);
            install_ctx_reset(&ictx);
        }
    }

    if (!ts->getop(ts, POLDEK_OP_PARTICLE))
        nerr = !do_install(&ictx, iinf);

 l_end:
    
    install_ctx_destroy(&ictx);
    MEMINF("END");
    if (is_particle)
        ts->setop(ts, POLDEK_OP_PARTICLE, 1);
    
    return nerr == 0;
}
