/*
  Copyright (C) 2000 - 2004 Pawel A. Gajda <mis@pld.org.pl>

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
#include <sys/stat.h>
#include <sys/types.h>

#include <trurl/nmalloc.h>
#include <trurl/nstr.h>
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
    double    nbytes;
    double    nfbytes;
    struct pkgmark_set *pms;
};


static void is_marked_mapfn(struct pkg *pkg, struct inf *inf) 
{
    if (pkg_is_marked(inf->pms, pkg)) {
        inf->npackages++;
        inf->nbytes += pkg->size;
        inf->nfbytes += pkg->fsize;
    }
}

int do_poldek_ts_install_dist(struct poldek_ts *ts)
{
    int               i, ninstalled, nerr, is_remote = -1;
    double            ninstalled_bytes; 
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
    ninstalled = 0;
    ninstalled_bytes = 0;
    
    memset(&inf, 0, sizeof(inf));
    inf.pms = ts->pms;
    n_array_map_arg(ts->ctx->ps->pkgs, (tn_fn_map2)is_marked_mapfn, &inf);

    for (i=0; i < n_array_size(ts->ctx->ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ts->ctx->ps->ordered_pkgs, i);
        char *pkgpath;
        
        if (pkg_isnot_marked(ts->pms, pkg))
            continue;
        
        pkgpath = pkg_path_s(pkg);
        if (is_remote == -1)
            is_remote = vf_url_type(pkgpath) & VFURL_REMOTE;
        
        if (poldek_VERBOSE > 1) {
            char *p = pkg_is_hand_marked(ts->pms, pkg) ? "" : "dep";
            if (pkg_has_badreqs(pkg)) 
                msg(2, "not%sInstall %s\n", p, pkg->name);
            else
                msg(2, "%sInstall %s\n", p, pkgpath);
        }

        if (ts->getop(ts, POLDEK_OP_TEST))
            continue;
            
        if (pkgdb_install(ts->db, pkgpath, ts))
            logn(LOGNOTICE | LOGFILE, "INST-OK %s", pkg->name);
            
        else {
            logn(LOGERR | LOGFILE, "INST-ERR %s", pkg->name);
            nerr++;
        }
            
        ninstalled++;
        ninstalled_bytes += pkg->size;
        inf.nfbytes -= pkg->fsize;
        poldek_term_printf_c(PRCOLOR_YELLOW,
                             _(" %d of %d (%.2f of %.2f MB) packages done"),
                             ninstalled, inf.npackages,
                             ninstalled_bytes/(1024*1000), 
                             inf.nbytes/(1024*1000));

        if (is_remote)
            poldek_term_printf_c(PRCOLOR_YELLOW, _("; %.2f MB to download"),
                                 inf.nfbytes/(1024*1000));
        poldek_term_printf_c(PRCOLOR_YELLOW, "\n");
    }
    
    if (nerr) 
        logn(LOGERR, _("there were errors during install"));
    
    return nerr == 0;
}
