/*
  Copyright (C) 2002 Pawel A. Gajda <mis@k2.net.pl>

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
#include <sys/types.h>
#include <sys/stat.h> 
#include <unistd.h>

#include <trurl/narray.h>
#include <trurl/nassert.h>
#include <trurl/n_snprintf.h>
#include <trurl/nmalloc.h>
#include <trurl/nstr.h>

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

#define UNINST_NOTFOLLOW    (1 << 18) /* see uninstall.c */
#define UNINST_MATCHED      (1 << 19) /* see uninstall.c */

#define uninst_LDFLAGS (PKG_LDNEVR | PKG_LDCAPS | PKG_LDFL_DEPDIRS)

static
int visit_pkg(int indent, struct pkg *pkg, struct pkgdb *db,
              struct dbpkg_set *unpoldek_tset, struct poldek_ts *ts) 
{
    unsigned ldflags = uninst_LDFLAGS;
    int i, k, n = 0;
    
    if (pkg_is_color(pkg, PKG_COLOR_BLACK))
        return 0;

    pkg_set_color(pkg, PKG_COLOR_BLACK);
    indent += 2;
    
    n += pkgdb_get_pkgs_requires_capn(db, unpoldek_tset->dbpkgs, pkg->name,
                                      NULL, ldflags);
    
    if (pkg->caps)
        for (i=0; i < n_array_size(pkg->caps); i++) {
            struct capreq *cap = n_array_nth(pkg->caps, i);

            n += pkgdb_get_pkgs_requires_capn(db, unpoldek_tset->dbpkgs,
                                              capreq_name(cap), NULL, ldflags);
        }
    
    if (pkg->fl)
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

                n += pkgdb_get_pkgs_requires_capn(db, unpoldek_tset->dbpkgs, path,
                                                  NULL, ldflags);
            }
        }
    
    if (n) {
        n_assert(pkg->revreqpkgs == NULL);
        pkg->revreqpkgs = n_array_new(4, (tn_fn_free)pkg_free, NULL);
                
        for (i=0; i < n_array_size(unpoldek_tset->dbpkgs); i++) {
            struct pkg *dbpkg = n_array_nth(unpoldek_tset->dbpkgs, i);
            
            if (pkg_is_color(dbpkg, PKG_COLOR_WHITE)) {
                n_array_push(pkg->revreqpkgs, pkg_link(dbpkg));
                pkg_set_color(dbpkg, PKG_COLOR_GRAY);
            }
        }
        
        for (i=0; i < n_array_size(pkg->revreqpkgs); i++) {
            struct pkg *revreq_pkg = n_array_nth(pkg->revreqpkgs, i);
            if (!pkg_is_marked(ts->pms, revreq_pkg)) {
                pkg_dep_mark(ts->pms, revreq_pkg);
                msg_i(1, indent, "%s marks %s\n", pkg_snprintf_s(pkg),
                      pkg_snprintf_s0(revreq_pkg));
                n += visit_pkg(indent, revreq_pkg, db, unpoldek_tset, ts);
            }
        }
    }
    
    return n;
}

static 
int mark_to_uninstall(struct dbpkg_set *set, struct pkgdb *db,
                      struct poldek_ts *ts)
{
    int i, n = 0;

    for (i=0; i < n_array_size(set->dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(set->dbpkgs, i);
        printf("mark %s\n", pkg_snprintf_s(dbpkg));
        pkg_hand_mark(ts->pms, dbpkg);
    }
    
    if (ts->getop(ts, POLDEK_OP_FOLLOW))
        for (i=0; i < n_array_size(set->dbpkgs); i++) {
            struct pkg *dbpkg = n_array_nth(set->dbpkgs, i);
            if (!pkgmark_isset(ts->pms, dbpkg, UNINST_NOTFOLLOW))
                n += visit_pkg(-2, dbpkg, db, set, ts);
        }
    
    return n;
}

static
void print_uninstall_summary(tn_array *pkgs, struct pkgmark_set *pms, int ndep)
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
    
    packages_iinf_display(0, "R", pkgs, pms, PKGMARK_MARK);
    packages_iinf_display(0, "D", pkgs, pms, PKGMARK_DEP);
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


int do_poldek_ts_uninstall(struct poldek_ts *ts, struct poldek_iinf *iinf)
{
    int               i, nerr = 0, ndep_marked = 0, doit = 1;
    struct dbpkg_set  *unpoldek_tset;
    tn_array          *pkgs = NULL;
    const tn_array    *pkgmasks;
    

    unpoldek_tset = dbpkg_set_new();
    pkgmasks = poldek_ts_get_arg_pkgmasks(ts);
    
    for (i=0; i < n_array_size(pkgmasks); i++) {
        char           *mask, *p;
        tn_array       *dbpkgs;
        struct capreq  *cr, *cr_evr;
        int            nmatches = 0;

        
        mask = n_array_nth(pkgmasks, i);
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

            if (parse_evr(p, &epoch, &ver, &rel))
                cr = cr_evr = capreq_new(NULL, tmp, epoch, ver, rel, REL_EQ, 0);
        }
        
        dbpkgs = pkgdb_get_provides_dbpkgs(ts->db, cr, NULL, uninst_LDFLAGS);
        DBGF("mask %s (%s) -> %d packages\n", mask, capreq_snprintf_s(cr), 
             dbpkgs ? n_array_size(dbpkgs) : 0);
        
        if (dbpkgs) {
            int n, j;

            for (j=0; j < n_array_size(dbpkgs); j++) {
                struct pkg *dbpkg = n_array_nth(dbpkgs, j);

                if (cr_evr && pkg_match_req(dbpkg, cr_evr, 1)) {
                    pkgmark_set(ts->pms, dbpkg, 1, UNINST_MATCHED);
                    nmatches++;
                    
                } else if (cr_evr == NULL && strcmp(mask, dbpkg->name) == 0) {
                    pkgmark_set(ts->pms, dbpkg, 1, UNINST_MATCHED);
                    nmatches++;
                }
            }
            
            n = n_array_size(dbpkgs);
            while (n_array_size(dbpkgs) > 0) {
                struct pkg *dbpkg = n_array_pop(dbpkgs);
                    
                if (nmatches != n)
                    pkgmark_set(ts->pms, dbpkg, 1, UNINST_NOTFOLLOW);

                if (pkgmark_isset(ts->pms, dbpkg, UNINST_MATCHED))
                    dbpkg_set_add(unpoldek_tset, dbpkg);
                else
                    pkg_free(dbpkg);
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

    n_array_uniq(unpoldek_tset->dbpkgs);
    if (nerr == 0 && n_array_size(unpoldek_tset->dbpkgs)) {
        ndep_marked = mark_to_uninstall(unpoldek_tset, ts->db, ts);
        pkgs = unpoldek_tset->dbpkgs;
    }

    if (nerr)
        doit = 0;

    if (ts->getop(ts, POLDEK_OP_TEST) && !ts->getop(ts, POLDEK_OP_RPMTEST))
        doit = 0;
    
    if (pkgs && doit) {
        int is_test = ts->getop(ts, POLDEK_OP_RPMTEST);
        
        print_uninstall_summary(pkgs, ts->pms, ndep_marked);
        if (!is_test && ts->getop(ts, POLDEK_OP_CONFIRM_UNINST) && ts->ask_fn)
            doit = ts->ask_fn(0, _("Proceed? [y/N]"));
        
        if (doit) {
            int vrfy = 0;
            
            if (!pm_pmuninstall(ts->db, pkgs, ts)) {
                nerr++;
                vrfy = 1;
            }
            
            if (iinf)
                update_poldek_iinf(iinf, pkgs, ts->db, vrfy);
        }
    }

    dbpkg_set_free(unpoldek_tset);
    return nerr == 0;
}


