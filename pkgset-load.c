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

#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <trurl/nassert.h>
#include <vfile/vfile.h>

#include "compiler.h"
#include "pkgset.h"
#include "pkgdir/pkgdir.h"
#include "pkgdir/pkgdir_intern.h"
#include "log.h"
#include "misc.h"
#include "i18n.h"
#include "depdirs.h"
#include "thread.h"

static int load_pkgdirs_seq(const tn_array *pkgdirs, const tn_array *depdirs, int ldflags)
{
    int re = 1;

    printf("seqload\n");

    for (int i=0; i < n_array_size(pkgdirs); i++) {
        struct pkgdir *pkgdir = n_array_nth(pkgdirs, i);

        if ((pkgdir->flags & PKGDIR_LOADED) != 0)
            continue;

        if (!pkgdir_load(pkgdir, depdirs, ldflags)) {
            logn(LOGERR, _("%s: load failed"), pkgdir->idxpath);
            re = 0;
        }
    }

    return re;
}

#ifndef ENABLE_THREADS /* not available */
static int load_pkgdirs(const tn_array *pkgdirs, const tn_array *depdirs, int ldflags)
{
    return load_pkgdirs_seq(pkgdirs, depdirs, ldflags);
}
#else  /* ENABLE_THREADS */
struct thread_info {
    pthread_t tid;
    struct pkgdir *pkgdir;
    const tn_array *depdirs;
    int ldflags;
};

static void *thread_load(void *thread_info) {
    struct thread_info *a = thread_info;

    if (!pkgdir_load(a->pkgdir, a->depdirs, a->ldflags)) {
        logn(LOGERR, _("%s: load failed"), a->pkgdir->idxpath);
    }

    return NULL;
}

static struct thread_info *load_pkgdir(struct thread_info *ti) {
    pthread_t tid;

    msgn(3, "running load thread for %s", vf_url_slim_s(ti->pkgdir->idxpath, 50));
    pthread_create(&tid, NULL, &thread_load, ti);
    //pthread_setname_np(tid, pkgdir->name);
    ti->tid = tid;

    return ti;
}

static int threadable_loads(const tn_array *pkgdirs) {
    tn_array *threadable = n_array_new(n_array_size(pkgdirs), NULL, NULL);

    for (int i=0; i < n_array_size(pkgdirs); i++) {
        struct pkgdir *pkgdir = n_array_nth(pkgdirs, i);

        if ((pkgdir->flags & PKGDIR_LOADED) != 0)
            continue;

        if (n_str_ne(pkgdir->type, "pndir"))
            continue;

        n_array_push(threadable, pkgdir);
    }

    int re = n_array_size(threadable);
    n_array_free(threadable);

    return re;
}

static int load_pkgdirs(const tn_array *pkgdirs, const tn_array *depdirs, int ldflags)
{
    struct thread_info *ti = NULL;
    int nti = 0;

    if (!poldek_enabled_threads()) {
        return load_pkgdirs_seq(pkgdirs, depdirs, ldflags);
    }

    int nt = threadable_loads(pkgdirs);

    msgn(3, "%d threadable loads", nt);

    if (nt > 1) {
        ti = malloc(n_array_size(pkgdirs) * sizeof(*ti));
        poldek_threading_toggle(true);
    }

    for (int i=0; i < n_array_size(pkgdirs); i++) {
        struct pkgdir *pkgdir = n_array_nth(pkgdirs, i);

        if ((pkgdir->flags & PKGDIR_LOADED) != 0)
            continue;

        bool th_load = nt > 1 && n_str_eq(pkgdir->type, "pndir");

        if (!th_load) {
            if (!pkgdir_load(pkgdir, depdirs, ldflags)) {
                logn(LOGERR, _("%s: load failed"), pkgdir->idxpath);
            }
        } else {
            n_assert(ti);
            struct thread_info *a = &ti[nti++];
            a->pkgdir = pkgdir;
            a->depdirs = depdirs;
            a->ldflags = ldflags;
            load_pkgdir(a);
        }
        //MEMINF("after load %s", pkgdir_idstr(pkgdir));
    }

    for (int i = 0; i < nti; i++) {
        pthread_join(ti[i].tid, NULL);
    }
    free(ti);
    poldek_threading_toggle(false);

    return 1;
}
#endif

int pkgset_load(struct pkgset *ps, int ldflags, tn_array *sources)
{
    int i, j;
    unsigned openflags = 0;
    void *t = timethis_begin();

    n_array_isort_ex(sources, (tn_fn_cmp)source_cmp_pri);

    if (ldflags & PKGDIR_LD_ALLDESC)
	openflags |= PKGDIR_OPEN_ALLDESC;

    for (i=0; i < n_array_size(sources); i++) {
        struct source *src = n_array_nth(sources, i);
        struct pkgdir *pkgdir = NULL;


        if (src->flags & PKGSOURCE_NOAUTO)
            continue;

        if (src->type == NULL)
            source_set_type(src, poldek_conf_PKGDIR_DEFAULT_TYPE);

        pkgdir = pkgdir_srcopen(src, openflags);

        /* trying dir */
        if (pkgdir == NULL && !source_is_type(src, "dir") && util__isdir(src->path)) {
            logn(LOGNOTICE, _("trying to scan directory %s..."), src->path);

            source_set_type(src, "dir");
            pkgdir = pkgdir_srcopen(src, openflags);
        }

        if (pkgdir == NULL) {
            if (n_array_size(sources) > 1)
                logn(LOGWARN, _("%s: load failed, skipped"),
                     vf_url_slim_s(src->path, 0));
            continue;
        }

        n_array_push(ps->pkgdirs, pkgdir);
        MEMINF("after open %s", pkgdir_idstr(pkgdir));
    }

    /* merge pkgdis depdirs into ps->depdirs */
    for (i=0; i < n_array_size(ps->pkgdirs); i++) {
        struct pkgdir *pkgdir = n_array_nth(ps->pkgdirs, i);
        if (pkgdir->depdirs)
            n_array_concat_ex(ps->depdirs, pkgdir->depdirs, (tn_fn_dup)strdup);
    }

    n_array_sort(ps->depdirs);
    n_array_uniq(ps->depdirs);
    n_array_freeze(ps->depdirs);

    load_pkgdirs(ps->pkgdirs, ps->depdirs, ldflags);

    /* merge pkgdirs packages into ps->pkgs */
    for (i=0; i < n_array_size(ps->pkgdirs); i++) {
	struct pkgdir *pkgdir = n_array_nth(ps->pkgdirs, i);

        for (j=0; j < n_array_size(pkgdir->pkgs); j++) {
            struct pkg *pkg = n_array_nth(pkgdir->pkgs, j);

            //pkg->recno = ps->_recno++; TOFIX another field is needed
            if (pkg_is_scored(pkg, PKG_IGNORED))
                continue;
            n_array_push(ps->pkgs, pkg_link(pkg));
        }
    }

    init_depdirs(ps->depdirs);

    if (n_array_size(ps->pkgs)) {
        int n = n_array_size(ps->pkgs);
        msgn(1, ngettext("%d package read",
                         "%d packages read", n), n);
    }

    timethis_end(4, t, "ps.load");

    return n_array_size(ps->pkgs);
}

int pkgset_add_pkgdir(struct pkgset *ps, struct pkgdir *pkgdir)
{
    int i;

    if (pkgdir->depdirs) {
        for (i=0; i < n_array_size(pkgdir->depdirs); i++)
            n_array_push(ps->depdirs, n_strdup(n_array_nth(pkgdir->depdirs, i)));
    }

    n_array_sort(ps->depdirs);
    n_array_uniq(ps->depdirs);

    n_array_concat_ex(ps->pkgs, pkgdir->pkgs, (tn_fn_dup)pkg_link);
    /*
    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        n_array_concat_ex(ps->pkgs, pkgdir->pkgs, (tn_fn_dup)pkg_link);
        struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
        //pkg->recno = ps->_recno++; TOFIX see comment in pkgset_load()
        n_array_push(ps->pkgs, pkg_link(pkg));
    }
    */
    n_array_push(ps->pkgdirs, pkgdir);

    return 1;
}
