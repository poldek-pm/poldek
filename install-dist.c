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

#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/stat.h>
#include <sys/types.h>

#include <trurl/nmalloc.h>
#include <trurl/nstr.h>

#include "sigint/sigint.h"
#include "vfile/vfile.h"
#include "pkgdir/source.h"
#include "pm/pm.h"
#include "poldek.h"
#include "poldek_intern.h"
#include "poldek_ts.h"
#include "poldek_term.h"
#include "pkgset.h"
#include "pkgmisc.h"
#include "pkgset-req.h"
#include "misc.h"
#include "log.h"
#include "i18n.h"

/*
 * Dist Instalation
 */ 
struct inf {
    int       npackages;
    int       ninstalled;
    double    bytes_used;
    double    bytes_toget;
    struct pkgmark_set *pms;
};


/* --fetch, --dump, packages in install order */
static int ts_fetch_or_dump_packages(struct poldek_ts *ts) 
{
    tn_array *pkgs;
    int rc = 0;
    
    pkgs = poldek__ts_install_ordered_packages(ts);
    
    if (ts->getop_v(ts, POLDEK_OP_JUSTPRINT, POLDEK_OP_JUSTPRINT_N, 0)) {
        rc = packages_dump(pkgs, ts->dumpfile,
                           ts->getop(ts, POLDEK_OP_JUSTPRINT_N) == 0);
        
    } else if (ts->getop(ts, POLDEK_OP_JUSTFETCH)) {
        const char *destdir = ts->fetchdir;
        if (destdir == NULL)
            destdir = ts->cachedir;
        
        rc = packages_fetch(ts->pmctx, pkgs, destdir, ts->fetchdir ? 1 : 0);
    }
    
    n_array_free(pkgs);
    return rc;
}

static void is_marked_mapfn(struct pkg *pkg, struct inf *inf) 
{
    if (pkg_is_marked(inf->pms, pkg)) {
        inf->npackages++;
        inf->bytes_used += pkg->size;
        inf->bytes_toget += pkg->fsize;
    }
}

static int mkdbdir(struct poldek_ts *ts) 
{
    char dbpath[PATH_MAX], *dbpathp;
    dbpathp = pm_dbpath(ts->pmctx, dbpath, sizeof(dbpath));
    n_assert(dbpathp);
    return util__mkdir_p(ts->rootdir, dbpath);
}

void display_iinf_start(struct inf *inf)
{
    if (inf->bytes_toget) {
        char buf[64];
        
        snprintf_size(buf, sizeof(buf), inf->bytes_toget, 0, 1);
        msg(1, _("Need to get about %s of archives."), buf);
        if (inf->bytes_used) {
            char buf[64];
            snprintf_size(buf, sizeof(buf), inf->bytes_used, 0, 1);
            msg(1, _(" After unpacking about %s will be used."), buf);
        }
        msg(1, "_\n");
    }
    
    
}

void display_iinf_progress(struct inf *inf)
{
    char buf[64];
    
    snprintf_size(buf, sizeof(buf), inf->bytes_toget, 1, 0);
    poldek_term_printf_c(PRCOLOR_YELLOW,
                         _("Installing #%d package of total %d (%s left to get)\n"),
                         inf->ninstalled + 1, inf->npackages, buf);
}

    
static int do_install_dist(struct poldek_ts *ts)
{
    int               i, nerr;
    struct inf        inf;
    char              tmpdir[PATH_MAX];
    tn_array          *pkgs = NULL;
    
    n_assert(ts->db->rootdir);
    
    if (!poldek_util_is_rwxdir(ts->db->rootdir)) {
        logn(LOGERR, "access %s: %m", ts->db->rootdir);
        return 0;
    }
    
    unsetenv("TMPDIR");
    unsetenv("TMP");

    snprintf(tmpdir, sizeof(tmpdir), "%s/tmp", ts->db->rootdir);
    mkdir(tmpdir, 0755);
    pm_configure(ts->pmctx, "%_tmpdir", "/tmp");
    pm_configure(ts->pmctx, "%_tmppath", "/tmp");
    pm_configure(ts->pmctx, "%tmppath", "/tmp");
    pm_configure(ts->pmctx, "%tmpdir", "/tmp");
    nerr = 0;
    
    memset(&inf, 0, sizeof(inf));
    inf.pms = ts->pms;
    n_array_map_arg(ts->ctx->ps->pkgs, (tn_fn_map2)is_marked_mapfn, &inf);
    
    display_iinf_start(&inf);
    pkgs = poldek__ts_install_ordered_packages(ts);
    
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        char *pkgpath;
        
        n_assert(pkg_is_marked(ts->pms, pkg));
        pkgpath = pkg_path_s(pkg);
        
        if (poldek_VERBOSE > 1) {
            char *p = pkg_is_hand_marked(ts->pms, pkg) ? "" : "dep";
            msg(2, "%sInstall %s\n", p, pkgpath);
        }

        if (ts->getop(ts, POLDEK_OP_TEST))
            continue;

        if (sigint_reached()) {
            logn(LOGNOTICE, _("Interrupted"));
            nerr++;
            break;
        }
        
        if (inf.ninstalled < inf.npackages)
            display_iinf_progress(&inf);
        
        ts->setop(ts, POLDEK_OP_NODEPS, 1); /* install dist is performed one
                                               by one, and we trust ourselves
                                            */
        
        if (pkgdb_install(ts->db, pkgpath, ts)) /* message for external scripts */
            logn(LOGNOTICE | LOGFILE, "INST-OK %s", pkg->name);
            
        else {
            logn(LOGERR | LOGFILE, "INST-ERR %s", pkg->name);
            nerr++;
        }
            
        inf.ninstalled++;
        inf.bytes_toget -= pkg->fsize;
    }

    poldek_term_printf_c(PRCOLOR_YELLOW,
                         _("Done, %d packages were installed.\n"),
                         inf.ninstalled);
    if (nerr) 
        logn(LOGERR, _("There were errors during install"));

    n_array_cfree(&pkgs);
    return nerr == 0;
}

static void install_dist_summary(struct poldek_ts *ts)
{
    int n = 0, ndep = 0;
    tn_array *pkgs, *depkgs;
    
    pkgs = pkgmark_get_packages(ts->pms, PKGMARK_MARK);
    n_assert(pkgs); /* function should not be called if no marked packages */
    
    n = n_array_size(pkgs);
    
    depkgs = pkgmark_get_packages(ts->pms, PKGMARK_DEP);
    if (depkgs)
        ndep = n_array_size(depkgs);

    poldek__ts_update_summary(ts, "I", pkgs, 0, NULL);
    n_array_free(pkgs);
    
    if (depkgs) {
        poldek__ts_update_summary(ts, "D", depkgs, 0, NULL);
        n_array_free(depkgs);
    }

    poldek__ts_display_summary(ts);
}

int do_poldek_ts_install_dist(struct poldek_ts *ts) 
{
    int rc, nerr = 0, ignorer;
    tn_array *pkgs = NULL;

    rc = 1;

    pkgs = pkgmark_get_packages(ts->pms, PKGMARK_MARK | PKGMARK_DEP);

    ignorer = ts->getop(ts, POLDEK_OP_NODEPS);
    if (!packages_verify_dependecies(pkgs, ts->ctx->ps) && !ignorer)
        nerr++;
    
    n_array_free(pkgs);
    pkgs = NULL;

    ignorer = ts->getop(ts, POLDEK_OP_FORCE);
    if (!pkgmark_verify_package_conflicts(ts->pms) && !ignorer)
        nerr++;
    
    if (nerr) {
        logn(LOGERR, _("Buggy package set"));
        rc = 0;
        goto l_end;
    }

    install_dist_summary(ts);
        
    if (ts->getop(ts, POLDEK_OP_TEST))
        goto l_end;

    if (ts->getop_v(ts, POLDEK_OP_JUSTPRINT, POLDEK_OP_JUSTPRINT_N,
                    POLDEK_OP_JUSTFETCH, 0)) {
        
        rc = ts_fetch_or_dump_packages(ts);
        goto l_end;
    }
    
    if (ts->getop(ts, POLDEK_OP_MKDBDIR)) {
        if (!mkdbdir(ts)) {
            rc = 0;
            goto l_end;
        }
    }
    
    if (ts->getop(ts, POLDEK_OP_RPMTEST))
        ts->db = poldek_ts_dbopen(ts, O_RDONLY);
    else
        ts->db = poldek_ts_dbopen(ts, O_RDWR | O_CREAT | O_EXCL);
    
    if (ts->db == NULL) {
        rc = 0;
        goto l_end;
    }

    rc = do_install_dist(ts);
    
    if (!ts->getop(ts, POLDEK_OP_RPMTEST))
        pkgdb_tx_commit(ts->db);
    pkgdb_free(ts->db);
    ts->db = NULL;
    
 l_end:

    if (pkgs)
        n_array_free(pkgs);
    return rc;
}
