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
#include "arg_packages.h"
#include "misc.h"
#include "pkg.h"
#include "dbpkgset.h"
#include "poldek_ts.h"
#include "rpm/rpm.h"
#include "pkgdb/pkgdb.h"

#define uninst_LDFLAGS (PKG_LDNEVR | PKG_LDCAPS | PKG_LDFL_DEPDIRS)

static int do_uninstall(tn_array *pkgs, struct poldek_ts *ts);

static
int visit_pkg(int indent, struct pkg *pkg, struct pkgdb *db,
              struct dbpkg_set *unpoldek_tset) 
{
    unsigned ldflags = uninst_LDFLAGS;
    int i, k, n = 0;
    
    if (pkg_is_color(pkg, PKG_COLOR_BLACK))
        return 0;

    pkg_set_color(pkg, PKG_COLOR_BLACK);
    indent += 2;
    
    n += rpm_get_pkgs_requires_capn(db->dbh, unpoldek_tset->dbpkgs, pkg->name,
                                    NULL, ldflags);
    
    if (pkg->caps)
        for (i=0; i < n_array_size(pkg->caps); i++) {
            struct capreq *cap = n_array_nth(pkg->caps, i);

            n += rpm_get_pkgs_requires_capn(db->dbh, unpoldek_tset->dbpkgs,
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

                n += rpm_get_pkgs_requires_capn(db->dbh, unpoldek_tset->dbpkgs, path,
                                                NULL, ldflags);
            }
        }
    
    if (n) {
        n_assert(pkg->revreqpkgs == NULL);
        pkg->revreqpkgs = n_array_new(4, (tn_fn_free)pkg_free, NULL);
                
        for (i=0; i < n_array_size(unpoldek_tset->dbpkgs); i++) {
            struct dbpkg *dbpkg = n_array_nth(unpoldek_tset->dbpkgs, i);
            
            if (pkg_is_color(dbpkg->pkg, PKG_COLOR_WHITE)) {
                n_array_push(pkg->revreqpkgs, pkg_link(dbpkg->pkg));
                pkg_set_color(dbpkg->pkg, PKG_COLOR_GRAY);
            }
        }
        
        for (i=0; i < n_array_size(pkg->revreqpkgs); i++) {
            struct pkg *revreq_pkg = n_array_nth(pkg->revreqpkgs, i);
            if (!pkg_is_marked(revreq_pkg)) {
                pkg_dep_mark(revreq_pkg);
                msg_i(1, indent, "%s marks %s\n", pkg_snprintf_s(pkg),
                      pkg_snprintf_s0(revreq_pkg));
                n += visit_pkg(indent, revreq_pkg, db, unpoldek_tset);
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
        struct dbpkg *dbpkg = n_array_nth(set->dbpkgs, i);
        dbpkg->flags |= DBPKG_TOUCHED;
        pkg_hand_mark(dbpkg->pkg);
    }
    
    if (ts->getop(ts, POLDEK_OP_FOLLOW))
        for (i=0; i < n_array_size(set->dbpkgs); i++) {
            struct dbpkg *dbpkg = n_array_nth(set->dbpkgs, i);
            if ((dbpkg->flags & DBPKG_UNIST_NOTFOLLOW) == 0) 
                n += visit_pkg(-2, dbpkg->pkg, db, set);
        }
    
    return n;
}

static void print_uninstall_summary(tn_array *pkgs, int ndep) 
{
    int n = n_array_size(pkgs);
    
#ifndef ENABLE_NLS    
    msg(0, "There are %d package%s to remove", n, n > 1 ? "s":"");
    if (ndep) 
        msg(0, _("_ (%d marked by dependencies)"), upg->ndep);
    
#else
    msg(0, ngettext("There are %d package to remove",
                    "There are %d packages to remove", n), n);

    if (ndep) 
        msg(0, ngettext("_ (%d marked by dependencies)",
                        "_ (%d marked by dependencies)", ndep), ndep);
#endif    
    msg(0, "_:\n");

    display_pkg_list(0, "R", pkgs, PKG_DIRMARK);
    display_pkg_list(0, "D", pkgs, PKG_INDIRMARK);
    
}


static
void update_install_info(struct install_info *iinf, tn_array *pkgs,
                         struct pkgdb *db, int vrfy)
{
    int i, is_installed = 0;
    
    if (vrfy) {
        pkgdb_reopendb(db);
        is_installed = 1;
    }

    
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        
        if (vrfy)
            is_installed = rpm_is_pkg_installed(db->dbh, pkg,
                                                NULL, NULL);

        if (!is_installed)
            n_array_push(iinf->uninstalled_pkgs, pkg_link(pkg));
    }
    
    if (vrfy) 
        pkgdb_closedb(db);
}


int do_poldek_ts_uninstall(struct poldek_ts *ts, struct install_info *iinf)
{
    int               i, nerr = 0, ndep_marked = 0, doit = 1;
    struct dbpkg_set  *unpoldek_tset;
    tn_array          *pkgs = NULL;
    const tn_array    *pkgmasks;
    

    unpoldek_tset = dbpkg_set_new();
    pkgmasks = poldek_ts_get_arg_pkgmasks(ts);
    
    for (i=0; i < n_array_size(pkgmasks); i++) {
        char           *mask;
        tn_array       *dbpkgs;
        struct capreq  *cr;
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
        cr = NULL;
        capreq_new_name_a(mask, cr);
        dbpkgs = rpm_get_provides_dbpkgs(ts->db->dbh, cr, NULL, uninst_LDFLAGS);
        if (dbpkgs) {
            int n, j;

            for (j=0; j < n_array_size(dbpkgs); j++) {
                struct dbpkg *dbpkg = n_array_nth(dbpkgs, j);

                if (strcmp(mask, dbpkg->pkg->name) == 0 ||
                    fnmatch(mask, dbpkg->pkg->nvr, 0) == 0) {
                    dbpkg->flags |= DBPKG_UNIST_MATCHED;
                    nmatches++;
                }
            }
            
            n = n_array_size(dbpkgs);
            while (n_array_size(dbpkgs) > 0) {
                struct dbpkg *dbpkg = n_array_pop(dbpkgs);
                    
                if (nmatches != n)
                    dbpkg->flags |= DBPKG_UNIST_NOTFOLLOW;

                if (dbpkg->flags & DBPKG_UNIST_MATCHED)
                    dbpkg_set_add(unpoldek_tset, dbpkg);
                else
                    dbpkg_free(dbpkg);
            }
            

            n_array_free(dbpkgs);
        }
        
        
        if (nmatches == 0) {
            logn(LOGERR, _("%s: no such package"), mask);
            nerr++;
        }
    }

    n_array_uniq(unpoldek_tset->dbpkgs);
    if (nerr == 0 && n_array_size(unpoldek_tset->dbpkgs)) {
        ndep_marked = mark_to_uninstall(unpoldek_tset, ts->db, ts);
        pkgs = dbpkgs_to_pkgs(unpoldek_tset->dbpkgs);
    }

    dbpkg_set_free(unpoldek_tset);

    if (nerr)
        doit = 0;

    if (ts->getop(ts, POLDEK_OP_TEST) && !ts->getop(ts, POLDEK_OP_RPMTEST))
        doit = 0;
    
    if (pkgs && doit) {
        int is_test = ts->getop(ts, POLDEK_OP_RPMTEST);
        
        print_uninstall_summary(pkgs, ndep_marked);
        if (!is_test && ts->getop(ts, POLDEK_OP_CONFIRM_UNINST) && ts->ask_fn)
            doit = ts->ask_fn(0, _("Proceed? [y/N]"));
        
        if (doit) {
            int vrfy = 0;
            
            if (!do_uninstall(pkgs, ts)) {
                nerr++;
                vrfy = 1;
            }
            
            if (iinf)
                update_install_info(iinf, pkgs, ts->db, vrfy);
        }
    }

    if (pkgs)
        n_array_free(pkgs);
    
    return nerr == 0;
}


static
int do_uninstall(tn_array *pkgs, struct poldek_ts *ts)
{
    char **argv;
    char *cmd;
    int i, n, nopts = 0;

    n = 128 + n_array_size(pkgs);
    
    argv = alloca((n + 1) * sizeof(*argv));
    argv[n] = NULL;
    
    n = 0;
    
    if (ts->getop(ts, POLDEK_OP_RPMTEST)) {
        cmd = "/bin/rpm";
        argv[n++] = "rpm";
        
    } else if (ts->getop(ts, POLDEK_OP_USESUDO)) {
        cmd = "/usr/bin/sudo";
        argv[n++] = "sudo";
        argv[n++] = "/bin/rpm";
        
    } else {
        cmd = "/bin/rpm";
        argv[n++] = "rpm";
    }
    
    argv[n++] = "--erase";

    for (i=1; i<verbose; i++)
        argv[n++] = "-v";

    if (ts->getop(ts, POLDEK_OP_RPMTEST))
        argv[n++] = "--test";
    
    if (ts->getop(ts, POLDEK_OP_FORCE))
        argv[n++] = "--force";
    
    if (ts->getop(ts, POLDEK_OP_NODEPS))
        argv[n++] = "--nodeps";

    if (ts->rootdir) {
    	argv[n++] = "--root";
        argv[n++] = (char*)ts->rootdir;
    }

    if (ts->rpmopts) 
        for (i=0; i<n_array_size(ts->rpmopts); i++)
            argv[n++] = n_array_nth(ts->rpmopts, i);
    
    nopts = n;
    for (i=0; i<n_array_size(pkgs); i++) {
        char nevr[256];
        int len;
        
        len = pkg_snprintf(nevr, sizeof(nevr), n_array_nth(pkgs, i));
        argv[n] = alloca(len + 1);
        memcpy(argv[n], nevr, len + 1);
        n++;
    }
    
    n_assert(n > nopts); 
    argv[n++] = NULL;
    
    if (verbose > 0) {
        char buf[1024], *p;
        p = buf;
        
        for (i=0; i<nopts; i++) 
            p += n_snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", argv[i]);
        *p = '\0';
        msgn(1, _("Running%s..."), buf);
        
    }

    return rpmr_exec(cmd, argv, 0, 0) == 0;
}

