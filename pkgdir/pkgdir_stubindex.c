/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

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

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>

#include <tndb/tndb.h>
#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>
#include <trurl/nmalloc.h>
#include <trurl/ntuple.h>

#include <vfile/vfile.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "pkgdir.h"
#include "pkgdir_intern.h"
#include "pkg.h"
#include "pkgmisc.h"
#include "capreq.h"
#include "pkgu.h"
#include "pkgroup.h"
#include "pkgfl.h"
#include "misc.h"
#include "pndir/pndir.h"        /* for pndir_make_pkgkey() */
//#include "pkgdir_stubindex.h"

const char *pkgdir_stubindex_basename = "stubindex";

static int stubindex_path(char *path, int size, const struct pkgdir *pkgdir)
{
    char tmp[PATH_MAX];
    char *ofpath;
    int n;

    n = n_snprintf(tmp, sizeof(tmp), "%s", pkgdir_localidxpath(pkgdir));
    n_assert(n > 0);

    ofpath = tmp;
    if (ofpath[n - 1] == '/') {    /* directory */
        ofpath[n - 1] = '\0';

    } else if (!util__isdir(ofpath)) { /* not directory? */
        char *dn = n_dirname(ofpath);
        ofpath = dn;
    }

#if 0                           /* debugging  */
    struct source *src = pkgdir->src;
    if (src != NULL) {
        char tmp2[PATH_MAX], tmp3[PATH_MAX];
        pkgdir__make_idxpath(tmp2, sizeof(tmp2), src->path, src->type, src->compr);
        vf_cachepath(tmp3, sizeof(tmp3), n_dirname(tmp2));
        DBGF("src %s => %s => %s\n", src->path, tmp2, tmp3);
    }
#endif

    DBGF("local path = %s\n", ofpath);
    n = vf_cachepath(path, size, ofpath);
    DBGF("cache path = %s\n", path);

    n_assert(n > 0);
    n += n_snprintf(&path[n], size - n, "/%s.%s.zst", pkgdir_stubindex_basename,
                    pkgdir->type);
    DBGF("result = %s\n", path);
    n_assert(n > 0);

    return n;
}

static int stubindex_create(const struct pkgdir *pkgdir, const char *path)
{
    struct vflock *lock;
    tn_stream     *st;
    char *dir, *tmp;

    msgn_i(2, 2, "Creating stub index of %s...", pkgdir_idstr(pkgdir));

    n_strdupap(path, &tmp);
    dir = n_dirname(tmp);

    DBGF("mkdir %s %s\n", path, dir);

    if ((lock = vf_lock_mkdir(dir)) == NULL)
        return 0;

    st = n_stream_open(path, "w", TN_STREAM_UNKNOWN);
    if (st == NULL) {
        logn(LOGERR, "%s: open failed (%m)\n", path);
        vf_lock_release(lock);
        return 0;
    }

    for (int i=0; i < n_array_size(pkgdir->pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
        char key[PATH_MAX];

        int n = pndir_make_pkgkey(key, sizeof(key), pkg);
        n_snprintf(&key[n], sizeof(key) - n, "^%u:%u:%u", pkg->color, pkg->size, pkg->btime);
        n_stream_printf(st, "%s\n", key);
    }

    n_stream_close(st);
    vf_lock_release(lock);

    struct utimbuf ut;
    ut.actime = ut.modtime = pkgdir_mtime(pkgdir);
    utime(path, &ut);

    return 1;
}

static
tn_array *load_stubindex(const char *path)
{
    tn_stream *st;
    tn_array *pkgs;
    char *buf;
    int n;

    st = n_stream_open(path, "r", TN_STREAM_UNKNOWN);
    if (st == NULL)
        return NULL;

    pkgs = pkgs_array_new(512);
    buf = n_malloc(128);

    while ((n = n_stream_getline(st, &buf, 128)) > 0) {
        char *b = n_str_strip_ws(buf);
        char *csb = strrchr(b, '^');
        if (csb != NULL) {
            *csb = '\0';
            csb++;
        }

        struct pkg *pkg = pndir_parse_pkgkey(b, n, NULL);
        if (pkg) {
            sscanf(csb, "%u:%u:%u", &pkg->color, &pkg->size, &pkg->btime);
            n_array_push(pkgs, pkg);
        }
    }
    n_stream_close(st);
    n_free(buf);

    return pkgs;
}

tn_array *source_stubload(struct source *src)
{
    char path[PATH_MAX], tmp[PATH_MAX];
    tn_array *pkgs;
    int n;



    pkgdir__make_idxpath(tmp, sizeof(tmp), src->path, src->type, src->compr);
    n = vf_cachepath(path, sizeof(path), n_dirname(tmp));

    n += n_snprintf(&path[n], sizeof(path) - n, "/%s.%s.zst",
                    pkgdir_stubindex_basename, src->type);

    msgn_i(2, 2, "Loading stub index of %s...", source_idstr(src));
    pkgs = load_stubindex(path);

    if (pkgs == NULL)
        return NULL;


    if (src->ign_patterns) {
        const struct pkgdir_module *mod = pkgdir_mod_find(src->type);
        /* module does not handle "ignore" itself  */
        if (mod && (mod->cap_flags & PKGDIR_CAP_HANDLEIGNORE) == 0)
            packages_score_ignore(pkgs, src->ign_patterns, 1);
    }

    return pkgs;
}

void pkgdir__stubindex_update(struct pkgdir *pkgdir)
{
    time_t idx_mtime, mtime;
    char path[1024];

    stubindex_path(path, sizeof(path), pkgdir);

    idx_mtime = pkgdir_mtime(pkgdir);
    mtime = poldek_util_mtime(path);

    if (mtime == idx_mtime) {
        return;
    }

    msgn_i(2, 2, "updating stub index of %s...", pkgdir_idstr(pkgdir));
    int verbosity = poldek_set_verbose(0);

    if ((pkgdir->flags & PKGDIR_LOADED) == 0) {
        pkgdir_load(pkgdir, 0, 0);
    }

    stubindex_create(pkgdir, path);
    poldek_set_verbose(verbosity);
}
