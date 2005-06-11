/*
  Copyright (C) 2000 - 2005 Pawel A. Gajda <mis@pld.org.pl>

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


static void is_marked_mapfn(struct pkg *pkg, struct inf *inf) 
{
    if (pkg_is_marked(inf->pms, pkg)) {
        inf->npackages++;
        inf->bytes_used += pkg->size;
        inf->bytes_toget += pkg->fsize;
    }
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

    

int do_poldek_ts_install_dist(struct poldek_ts *ts)
{
    int               i, nerr;
    struct inf        inf;
    char              tmpdir[PATH_MAX];
    
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
    for (i=0; i < n_array_size(ts->ctx->ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ts->ctx->ps->ordered_pkgs, i);
        char *pkgpath;
        
        if (pkg_isnot_marked(ts->pms, pkg))
            continue;
        
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
    
    return nerr == 0;
}
