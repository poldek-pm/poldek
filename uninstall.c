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
#include "usrset.h"
#include "misc.h"
#include "pkg.h"
#include "dbpkgset.h"

#define uninst_LDFLAGS (PKG_LDNEVR | PKG_LDCAPS | PKG_LDFL_DEPDIRS)

static int do_uninstall(tn_array *pkgs, struct inst_s *inst);


static
int visit_pkg(int indent, struct pkg *pkg, struct pkgdb *db,
              struct dbpkg_set *uninst_set) 
{
    unsigned ldflags = uninst_LDFLAGS;
    int i, k, n = 0;
    
    if (pkg_is_color(pkg, PKG_COLOR_BLACK))
        return 0;

    pkg_set_color(pkg, PKG_COLOR_BLACK);
    indent += 2;
    
    n += rpm_get_pkgs_requires_capn(db->dbh, uninst_set->dbpkgs, pkg->name,
                                    NULL, ldflags);
    
    if (pkg->caps)
        for (i=0; i < n_array_size(pkg->caps); i++) {
            struct capreq *cap = n_array_nth(pkg->caps, i);

            n += rpm_get_pkgs_requires_capn(db->dbh, uninst_set->dbpkgs,
                                            capreq_name(cap), NULL, ldflags);
        }
    
    if (pkg->fl)
        for (i=0; i < n_array_size(pkg->fl); i++) {
            struct pkgfl_ent *flent = n_array_nth(pkg->fl, i);
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

                n += rpm_get_pkgs_requires_capn(db->dbh, uninst_set->dbpkgs, path,
                                                NULL, ldflags);
            }
        }
    
    if (n) {
        n_assert(pkg->revreqpkgs == NULL);
        pkg->revreqpkgs = n_array_new(4, (tn_fn_free)pkg_free, NULL);
                
        for (i=0; i < n_array_size(uninst_set->dbpkgs); i++) {
            struct dbpkg *dbpkg = n_array_nth(uninst_set->dbpkgs, i);
            
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
                n += visit_pkg(indent, revreq_pkg, db, uninst_set);
            }
        }
    }
    
    return n;
}

static 
int mark_to_uninstall(struct dbpkg_set *set, struct pkgdb *db, struct inst_s *inst)
{
    int i, n = 0;

    for (i=0; i < n_array_size(set->dbpkgs); i++) {
        struct dbpkg *dbpkg = n_array_nth(set->dbpkgs, i);
        dbpkg->flags |= DBPKG_TOUCHED;
        pkg_hand_mark(dbpkg->pkg);
    }
    
    if (inst->flags & INSTS_FOLLOW)
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



int packages_uninstall(tn_array *pkgs, struct inst_s *inst,
                       struct install_info *iinf)
{
    struct usrpkgset *ups;
    int rc, i;
    
    ups = usrpkgset_new();
    for (i=0; i<n_array_size(pkgs); i++)
        usrpkgset_add_pkg(ups, n_array_nth(pkgs, i));

    rc = uninstall_usrset(ups, inst, iinf);
    usrpkgset_free(ups);
    return rc;
}


int uninstall_usrset(struct usrpkgset *ups, struct inst_s *inst,
                     struct install_info *iinf)
{
    int               i, nerr = 0, ndep_marked = 0, doit = 1;
    struct dbpkg_set  *uninst_set;
    struct pkgdb      *db;
    tn_array          *pkgs = NULL;
    void              *pkgflmod_mark = NULL;
    
    
    if (inst->rootdir == NULL)
        inst->rootdir = "/";
    
    db = pkgdb_open(inst->rootdir, NULL, O_RDONLY);
    if (db == NULL) 
        return 0;

    uninst_set = dbpkg_set_new();
    pkgflmod_mark = pkgflmodule_allocator_push_mark();
    
    for (i=0; i < n_array_size(ups->pkgdefs); i++) {
        struct pkgdef *pdef;
        tn_array *dbpkgs;
        int nmatches = 0;

        
        pdef = n_array_nth(ups->pkgdefs, i);
        if ((pdef->tflags & PKGDEF_REGNAME) == 0) {
            logn(LOGERR, _("'%s': only exact selection is supported"),
                 pdef->virtname);
            nerr++;
            continue;
        }
        
                
        dbpkgs = rpm_get_packages(db->dbh, pdef->pkg, uninst_LDFLAGS);
        if (dbpkgs) {
            int n, j;

            for (j=0; j < n_array_size(dbpkgs); j++) {
                struct dbpkg *dbpkg = n_array_nth(dbpkgs, j);

                if (pkgdef_match_pkg(pdef, dbpkg->pkg)) {
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
                    dbpkg_set_add(uninst_set, dbpkg);
                else
                    dbpkg_free(dbpkg);
            }
            

            n_array_free(dbpkgs);
        }
        
        
        if (nmatches == 0) {
            logn(LOGERR, _("%s: no such package"), pdef->pkg->name);
            nerr++;
        }
    }

    n_array_uniq(uninst_set->dbpkgs);
    if (nerr == 0 && n_array_size(uninst_set->dbpkgs)) {
        ndep_marked = mark_to_uninstall(uninst_set, db, inst);
        pkgs = dbpkgs_to_pkgs(uninst_set->dbpkgs);
    }

    dbpkg_set_free(uninst_set);
    pkgdb_closedb(db);
    pkgflmodule_allocator_pop_mark(pkgflmod_mark);

    if (nerr)
        doit = 0;
    
    if ((inst->flags & INSTS_TEST) && (inst->flags & INSTS_RPMTEST) == 0)
        doit = 0;
    
    if (pkgs && doit) {
        int is_test = inst->flags & INSTS_RPMTEST;
        
        print_uninstall_summary(pkgs, ndep_marked);
        if (!is_test && (inst->flags & INSTS_CONFIRM_UNINST) && inst->ask_fn)
            doit = inst->ask_fn(0, _("Proceed? [y/N]"));
        
        if (doit) {
            int vrfy = 0;
            
            if (!do_uninstall(pkgs, inst)) {
                nerr++;
                vrfy = 1;
            }
            
            if (iinf)
                update_install_info(iinf, pkgs, db, vrfy);
        }
    }

    if (pkgs)
        n_array_free(pkgs);
    
    pkgdb_free(db);
    return nerr == 0;
}


static
int do_uninstall(tn_array *pkgs, struct inst_s *inst)
{
    char **argv;
    char *cmd;
    int i, n, nopts = 0;

    n = 128 + n_array_size(pkgs);
    
    argv = alloca((n + 1) * sizeof(*argv));
    argv[n] = NULL;
    
    n = 0;
    
    if (inst->flags & INSTS_RPMTEST) {
        cmd = "/bin/rpm";
        argv[n++] = "rpm";
        
    } else if (inst->flags & INSTS_USESUDO) {
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

    if (inst->flags & INSTS_RPMTEST)
        argv[n++] = "--test";
    
    if (inst->flags & INSTS_FORCE)
        argv[n++] = "--force";
    
    if (inst->flags & INSTS_NODEPS)
        argv[n++] = "--nodeps";

    if (inst->rootdir) {
    	argv[n++] = "--root";
	argv[n++] = (char*)inst->rootdir;
    }

    if (inst->rpmopts) 
        for (i=0; i<n_array_size(inst->rpmopts); i++)
            argv[n++] = n_array_nth(inst->rpmopts, i);
    
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

