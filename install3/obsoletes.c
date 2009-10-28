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

#include <fnmatch.h>
#include "ictx.h"

static void orphan_free(struct orphan *o)
{
    pkg_free(o->pkg);
    n_array_free(o->reqs);
    free(o);
}

static struct orphan *orphan_new(int indent, struct pkg *pkg, tn_array *caps) 
{
    struct orphan *o;

    o = n_malloc(sizeof(*o));
    o->pkg = pkg_link(pkg);
    o->reqs = capreq_arr_new(4);
    n_array_ctl_set_freefn(o->reqs, NULL); /* weak ref */
    
    for (int i=0; i < n_array_size(caps); i++) {
        const struct capreq *req;
        struct capreq *cap = n_array_nth(caps, i);

        req = pkg_requires_cap(pkg, cap);
        tracef(indent, "%s requires %s? %s", pkg_id(pkg), capreq_stra(cap), req ? "yes" : "no");
        if (req)
            n_array_push(o->reqs, (struct capreq *)req);
    }
    
    if (n_array_size(o->reqs) == 0) { /* not an orphan */
        orphan_free(o);
        o = NULL;
    }
    
    return o;
}

/* is to-install set provides cap? */
static int toin_provides(int indent, struct i3ctx *ictx,
                         const struct pkg *pkg, const struct capreq *cap)
{
    struct pkg *tomark = NULL;
    
    if (i3_find_req(indent, ictx, pkg, cap, &tomark, NULL) && tomark == NULL)
        return 1;

    return 0;
}

/* get packages required by pkg */
static int get_orphaned(int indent, struct i3ctx *ictx,
                        tn_array *orphaned, const tn_array *unsatisfied_caps)
{
    struct pkgdb *db = ictx->ts->db;
    unsigned ldflags = PKG_LDNEVR | PKG_LDREQS;
    int i, norphaned = 0;
    
    if (sigint_reached())
        return 0;
    
    MEMINF("process_pkg_orphans:");
    
    for (i=0; i < n_array_size(unsatisfied_caps); i++) {
        struct capreq *cap = n_array_nth(unsatisfied_caps, i);
        unsigned ma_flags = POLDEK_MA_PROMOTE_CAPEPOCH;
        int n = 0;

#if 0                           /* XXX - disabled, needs testing */
        /* skip orphaned by cap name only if cap name is provided by inset */
        if (capreq_versioned(cap)) { 
            struct capreq *c;
            
            capreq_new_name_a(capreq_name(cap), c);
            if (toin_provides(indent, ictx, NULL, c))
                ma_flags = 0;
        }
#endif
        n = pkgdb_q_what_requires(db, orphaned, cap,
                                  iset_packages_by_recno(ictx->unset),
                                  ldflags, ma_flags);
        norphaned += n;
        trace(indent + 1, "%s: %d package(s) orphaned", capreq_stra(cap), n);
    }
    return norphaned;
}

/* filter out obsoletes packages not coloured like new one  */
static
int obs_filter(struct pkgdb *db, const struct pm_dbrec *dbrec, void *apkg)
{
    struct pkg dbpkg, *pkg = apkg;
    char *arch;

    db = db;
    if (dbrec->hdr == NULL)
        return 0;
    
    if (!pm_dbrec_nevr(dbrec, &dbpkg.name, &dbpkg.epoch,
                       &dbpkg.ver, &dbpkg.rel, &arch, &dbpkg.color))
        return 0;

    if (arch) 
        pkg_set_arch(&dbpkg, arch);

    if (!poldek_conf_MULTILIB)
        return 1;

    tracef(4, "%s.%s (c=%d) colored like %s (c=%d) => %s\n",
           pkg_evr_snprintf_s(&dbpkg), arch, dbpkg.color,
           pkg_id(pkg), pkg->color,
           pkg_is_colored_like(&dbpkg, pkg) ? "yes" : "no");

    if (pkg_is_colored_like(&dbpkg, pkg))
        return 1;

    /* any uncolored -> rpm allows upgrade */
    if (dbpkg.color == 0 || pkg->color == 0)
        return 1;
    
    return 0;
}

static tn_array *get_obsoletedby_pkg(struct pkgdb *db, const tn_array *unpkgs,
                                     struct pkg *pkg, unsigned getflags,
                                     unsigned ldflags)
{
    tn_array *obsoleted;
    int n;
    
    if (poldek_conf_MULTILIB)
        pkgdb_set_filter(db, obs_filter, pkg);

    obsoleted = pkgs_array_new_ex(16, pkg_cmp_recno);
    n = pkgdb_q_obsoletedby_pkg(db, obsoleted, pkg, getflags,
                                unpkgs, ldflags);
    
    if (poldek_conf_MULTILIB)
        pkgdb_set_filter(db, NULL, NULL);

    if (n_array_size(obsoleted) == 0)
        n_array_cfree(&obsoleted);

    return obsoleted;
}

/* files surely not required by anyone; XXX - should be configurable */
int is_requireable_path(const char *path) 
{
    const char *nonreq[] = {
        "/usr/share/doc/*/*",
        "/usr/share/doc/*",
        "/usr/share/man/*.[0-9]",
        "/usr/src/examples/*",
        "*.desktop",
        "*.mo",
        "*.gz",
        "*.bz2",
        "*.pdf", 
        "*.txt",
        "*.png",
        "*.gif",
        "*.jpg",
        "*.c",
        "*.h",
        "*.pc",
        "*.pm",
        "*.py",
        "*.rb",
        "*.txt",
        NULL,
    };

    int i = 0;
    
    while (nonreq[i]) {
        if (fnmatch(nonreq[i], path, 0) == 0)
            return 0;
        i++;
    }
    return 1;
}

int i3_process_pkg_obsoletes(int indent, struct i3ctx *ictx,
                             struct i3pkg *i3pkg)
{
    struct pkg       *pkg = i3pkg->pkg;
    struct pkgdb     *db = ictx->ts->db;
    struct iset      *unset = ictx->unset;
    unsigned         getflags = PKGDB_GETF_OBSOLETEDBY_NEVR;
    tn_array         *obsoleted = NULL, *orphaned = NULL, *orphans = NULL;
    tn_array         *unsatisfied_caps = NULL;
    int              n, i;
    
    if (!poldek_ts_issetf(ictx->ts, POLDEK_TS_UPGRADE))
        return 1;

    if (sigint_reached())
        return 0;
    
    tracef(indent, "%s", pkg_id(pkg));
    
    if (ictx->ts->getop(ictx->ts, POLDEK_OP_OBSOLETES))
        getflags |= PKGDB_GETF_OBSOLETEDBY_OBSL;

    if (poldek_ts_issetf(ictx->ts, POLDEK_TS_DOWNGRADE))
        getflags |= PKGDB_GETF_OBSOLETEDBY_REV;
    
    
    obsoleted = get_obsoletedby_pkg(db, iset_packages_by_recno(unset), pkg,
                                    getflags, PKG_LDWHOLE_FLDEPDIRS);
    
    n = obsoleted ? n_array_size(obsoleted) : 0;
    trace(indent + 1, "%s removes %d package(s)", pkg_id(pkg), n);
    if (n == 0)
        return 1;

    for (i=0; i < n_array_size(obsoleted); i++) {
        struct pkg *dbpkg = n_array_nth(obsoleted, i);
        struct pkg_cap_iter *it;
        const struct capreq *cap;
        
        if (iset_has_pkg(unset, dbpkg)) { /* rpmdb with duplicate? */
            logn(LOGERR | LOGDIE, "%s: installed twice? Give up.", pkg_id(dbpkg));
        }
            
        n_array_push(i3pkg->obsoletedby, pkg_link(dbpkg));

        msgn_i(1, indent, _("%s obsoleted by %s"), pkg_id(dbpkg), pkg_id(pkg));
        iset_add(unset, dbpkg, PKGMARK_DEP);
        
        trace(indent + 1, "verifying uninstalled %s's caps", pkg_id(dbpkg));

        it = pkg_cap_iter_new(dbpkg);
        while ((cap = pkg_cap_iter_get(it))) {
            int is_satisfied = 0;

            if (capreq_is_file(cap) && !is_requireable_path(capreq_name(cap))) {
                trace(indent + 2, "- %s (skipped)", capreq_stra(cap));
                continue;
            }
            
            if (pkg_satisfies_req(pkg, cap, 1))
                is_satisfied = 1;
            
            else if (toin_provides(indent + 4, ictx, pkg, cap)) 
                is_satisfied = 2;
            
            if (is_satisfied) {
                trace(indent + 2, "- %s (satisfied %s)", capreq_stra(cap),
                      is_satisfied == 1 ? "by successor" : "by inset");
                
            } else {
                if (unsatisfied_caps == NULL)
                    unsatisfied_caps = capreq_arr_new(24);
                
                trace(indent + 2, "- %s (to verify)", capreq_stra(cap));
                n_array_push(unsatisfied_caps, capreq_clone(NULL, cap));
            }
        }
        
        pkg_cap_iter_free(it);
    }
    
    if (unsatisfied_caps == NULL)
        return 0;
    n_assert(n_array_size(unsatisfied_caps));

    n_array_uniq(unsatisfied_caps);
    orphaned = pkgs_array_new_ex(16, pkg_cmp_recno);
    
    /*
      determine what exactly requirements are missed, i.e no more
      "foreign" dependencies skipping needed
    */
    if (!get_orphaned(indent + 1, ictx, orphaned, unsatisfied_caps)) {
        n_assert(n_array_size(orphaned) == 0);
        
    } else {
        n_assert(n_array_size(orphaned));
        n_assert(n_array_size(unsatisfied_caps));

        orphans = n_array_new(16, (tn_fn_free)orphan_free, NULL);
        
        for (i=0; i < n_array_size(orphaned); i++) {
            struct orphan *o = orphan_new(indent + 3, n_array_nth(orphaned, i),
                                          unsatisfied_caps);
            if (o)
		n_array_push(orphans, o);
        }

	if (n_array_size(orphans) == 0)
	    n_array_cfree(&orphans);
    }
    n_array_free(orphaned);

    if (orphans == NULL) {
        trace(indent + 2, "- no orphaned packages");
        
    } else {
        for (i=0; i < n_array_size(orphans); i++) {
            struct orphan *o = n_array_nth(orphans, i);
            trace(indent + 2, "- %s (nreqs=%d)", pkg_id(o->pkg), n_array_size(o->reqs));
        }
    
        for (i=0; i < n_array_size(orphans); i++) {
            struct orphan *o = n_array_nth(orphans, i);
            i3_process_orphan(indent, ictx, o);
        }
    }
    
    n_array_cfree(&orphans);
    n_array_free(unsatisfied_caps); /* XXX: must free()d after orphans array,
                                       caps in array are "weak"-referenced */
    return 1;
}
