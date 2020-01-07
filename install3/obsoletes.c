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

#include <fnmatch.h>
#include "ictx.h"
#include "pkgcmp.h"

static void orphan_free(struct orphan *o)
{
    pkg_free(o->pkg);
    n_array_free(o->reqs);
    free(o);
}

static struct orphan *orphan_new(int indent, struct pkg *pkg, tn_array *caps)
{
    struct orphan *o;
    int i;

    o = n_malloc(sizeof(*o));
    o->pkg = pkg_link(pkg);
    o->reqs = capreq_arr_new(4);
    n_array_ctl_set_freefn(o->reqs, NULL); /* weak ref */

    for (i=0; i < n_array_size(caps); i++) {
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
    const char *arch;

    db = db;
    if (dbrec->hdr == NULL)
        return 0;

    if (!pm_dbrec_nevr(dbrec, (const char **)&dbpkg.name, &dbpkg.epoch,
                       (const char **)&dbpkg.ver, (const char **)&dbpkg.rel,
                       &arch, &dbpkg.color))
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

    if (poldek_conf_MULTILIB)
        pkgdb_set_filter(db, obs_filter, pkg);

    obsoleted = pkgs_array_new_ex(16, pkg_cmp_recno);
    pkgdb_q_obsoletedby_pkg(db, obsoleted, pkg, getflags, unpkgs, ldflags);

    if (poldek_conf_MULTILIB)
        pkgdb_set_filter(db, NULL, NULL);

    if (n_array_size(obsoleted) == 0)
        n_array_cfree(&obsoleted);

    return obsoleted;
}

/* caps not required by anyone; XXX - should be configurable */
static int is_requireable_cap(const char *cap)
{
    const char *nonreq[] = {
        "elf(buildid)*",
        NULL
    };

    int i = 0;

    while (nonreq[i]) {
        if (fnmatch(nonreq[i], cap, 0) == 0)
            return 0;

        i++;
    }

    return 1;
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

static int ver_distance(struct pkg *p1, struct pkg *p2)
{
    int n1, n2, distance = 0;
    const char **s1 = n_str_tokl_n(p1->ver, ".", &n1);
    const char **s2 = n_str_tokl_n(p2->ver, ".", &n2);

    DBGF("%s vs %s; %d, %d\n", p1->ver, p2->ver, n1, n2);

    if (n1 != n2) { /* loong distance for inconsistent versions */
        distance = (n1 + n2) * 1000;
        goto l_end;
    }

    // compare segments
    for (int i = 0; i < n1; i++) {
        const char *v1 = s1[i];
        const char *v2 = s2[i];
        long v1i = 0, v2i = 0;

        if (v1) {
            v1i = atoi(v1);
        }

        if (v2) {
            v2i = atoi(v2);
        }

        DBGF("%d, %ld cmp %ld, distance += abs(%ld) * %d\n", i, v1i, v2i, v1i-v2i, n1-i);
        distance += abs(v1i-v2i) * (n1-i);
    }

 l_end:
    n_str_tokl_free(s1);
    n_str_tokl_free(s2);

    DBGF("re %s vs %s  ==> %d\n", p1->ver, p2->ver, distance);

    return distance;
}

/* number of insalled instances */
static int obsoleted_multiinsts(struct i3pkg *i3pkg, tn_array *obsoleted) {
    int i, n = 0;

    for (i = 0; i < n_array_size(obsoleted); i++) {
        struct pkg *dbpkg = n_array_nth(obsoleted, i);

        /* every obsoleted pkg must be actual pkg instance */
        if (pkg_cmp_name(i3pkg->pkg, dbpkg) == 0) {
            n += 1;
        }
    }

    return n;
}


/* when multiple instances are installed, obsolete only one and keep the rest installed */
static
int handle_multiinsts(int indent, struct i3ctx *ictx, struct i3pkg *i3pkg, tn_array *obsoleted)
{
    int min_distance = INT_MAX;
    int i, min_i = 0, re = 1;
    int *distances = alloca(n_array_size(obsoleted) * sizeof(int));

    n_assert(n_array_size(obsoleted) > 1);

    for (i = 0; i < n_array_size(obsoleted); i++) {
        struct pkg *dbpkg = n_array_nth(obsoleted, i);

        /* every obsoleted pkg must be actual pkg instance */
        if (pkg_cmp_name(i3pkg->pkg, dbpkg) != 0) {
            return 0;
        }

        int distance = ver_distance(i3pkg->pkg, dbpkg);
        distances[i] = distance;
        if (distance < min_distance) {
            min_distance = distance;
            min_i = i;
        }

        DBGF("%s %s => %d\n", pkg_id(i3pkg->pkg), pkg_id(dbpkg), distance);
    }

    int n = 0;
    for (i = 0; i < n_array_size(obsoleted); i++) {
        if (distances[i] == min_distance)
            n++;
    }
    DBGF("min at %d %d, totalmin %d\n", min_i, min_distance, n);

    struct pkg *dbpkg_to_uninstall = pkg_link(n_array_nth(obsoleted, min_i));
    tracef(indent + 2, "upgrade %s only", pkg_id(dbpkg_to_uninstall));

    tn_array *keeps = n_array_clone(obsoleted);

    for (i = 0; i < n_array_size(obsoleted); i++) {
        struct pkg *dbpkg = n_array_nth(obsoleted, i);
        if (i != min_i) {
            n_array_push(keeps, pkg_link(dbpkg));
            tracef(indent + 2, "keep %s", pkg_id(dbpkg));
        }
    }

    /* put to obsoleted only one package */
    n_array_clean(obsoleted);
    n_assert(n_array_size(obsoleted) == 0);
    n_array_push(obsoleted, dbpkg_to_uninstall);

    n_array_unshift(keeps, dbpkg_to_uninstall); /* first will be uninstalled, see install.c */
    n_hash_insert(ictx->multi_obsoleted, pkg_id(i3pkg->pkg), keeps);

    return re;
}

static
int process_multiinsts(int indent, struct i3ctx *ictx, struct i3pkg *i3pkg, tn_array *obsoleted)
{
    int ninstances = obsoleted_multiinsts(i3pkg, obsoleted);
    if (ninstances < 2) {       /* nothing to do */
        goto l_end;
    }

    if (ninstances != n_array_size(obsoleted)) { /* other packages are obsoleted too */
        // give up here?
        goto l_end;
    }

    n_assert(ninstances == n_array_size(obsoleted));
    handle_multiinsts(indent, ictx, i3pkg, obsoleted);

 l_end:
    return n_array_size(obsoleted);
}


int i3_process_pkg_obsoletes(int indent, struct i3ctx *ictx, struct i3pkg *i3pkg)
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
    if (n > 1) {
        process_multiinsts(indent, ictx, i3pkg, obsoleted);
        n = n_array_size(obsoleted);
    }

    trace(indent + 1, "%s removes %d package(s)", pkg_id(pkg), n);

    if (n == 0)
        return 1;

    DBGF("%s n=%d\n", pkg_id(pkg), n);
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

            if (!is_requireable_cap(capreq_name(cap))) {
                trace(indent + 2, "- %s (skipped)", capreq_stra(cap));
                continue;
            }

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
    n_array_free(unsatisfied_caps); /* must be free()d after orphans array,
                                       caps in array are "weak"-referenced */
    return 1;
}
