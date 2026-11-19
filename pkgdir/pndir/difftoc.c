/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

/* the list of diffs of a pndir index (packages.i/<index>.diff.toc) */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nstream.h>
#include <trurl/n_snprintf.h>

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pkgdir.h"
#include "pndir.h"
#include "tags.h"

/* the lines of the list of diffs, none for a missing list; NULL when the
   list cannot be read whole: the caller starts a new list rather than
   rewrite one from a part */
static tn_array *difftoc_read(const char *path)
{
    tn_array   *lines;
    tn_stream  *st;
    char       *buf = NULL;
    size_t     size = 0;
    int        n;

    lines = n_array_new(32, free, NULL);
    if ((st = n_stream_open(path, "r", TN_STREAM_UNKNOWN)) == NULL) {
        if (errno == ENOENT)
            return lines;

        logn(LOGERR, "%s: %m", path);
        n_array_free(lines);
        return NULL;
    }

    while ((n = n_stream_getline(st, &buf, &size)) > 0)
        n_array_push(lines, n_strdupl(buf, n));
    free(buf);

    if (n_stream_close(st) != 0) {
        logn(LOGERR, _("%s: unable to read the list of diffs: %m"), path);
        n_array_free(lines);
        return NULL;
    }

    return lines;
}

/* the diffs are listed from the newest back until they add up to 90% of
   the whole index: a client behind that takes the index instead.  The
   lines of the missing ones are emptied; *from is the first line to keep */
static int difftoc_vaccum(tn_array *lines, const char *idxpath,
                          const char *tocpath, int *from)
{
    char         tmp[PATH_MAX], *dn, *bn;
    struct stat  st_idx, st;
    off_t        diffs_size = 0;
    int          i, cut = -1;

    *from = 0;
    if (stat(idxpath, &st_idx) != 0) {
        logn(LOGERR, "vaccum: stat %s: %m", idxpath);
        return 0;
    }

    n_snprintf(tmp, sizeof(tmp), "%s", tocpath);
    n_basedirnam(tmp, &dn, &bn);

    for (i = n_array_size(lines) - 1; i >= 0; i--) {
        char *p, *l, path[PATH_MAX];

        l = n_array_nth(lines, i);
        if ((p = strchr(l, ' ')) == NULL) {
            logn(LOGERR, _("vaccum: %s: format error"), tocpath);
            *l = '\0';
            continue;
        }

        snprintf(path, sizeof(path), "%s/%.*s", dn, (int)(p - l), l);

        if (stat(path, &st) != 0) {
            if (errno != ENOENT)
                logn(LOGERR, "vaccum diff: stat %s: %m", path);
            *l = '\0';
            continue;
        }
        DBGF("path = %s %lld, %lld, %lld\n", path, (long long)st.st_size,
             (long long)diffs_size, (long long)st_idx.st_size);

        if (cut >= 0)
            continue;

        if (diffs_size + st.st_size > (st_idx.st_size * 0.9)) {
            cut = i;

        } else {
            diffs_size += st.st_size;
        }
    }

    if (cut >= 0)
        *from = cut;

    return 1;
}

/* each diff starts from the index the one before it led to.  A list whose
   last diff does not lead to the index this one starts from lost a diff,
   one a failed --mkidx did not list: its diffs lead no client to the
   current index */
int pndir_difftoc_leads_to(tn_array *lines, time_t orig_ts)
{
    unsigned long ts;
    int i;

    for (i = n_array_size(lines) - 1; i >= 0; i--) {
        const char *l = n_array_nth(lines, i);

        if (*l == '\0' || *l == '\n')
            continue;

        if (sscanf(l, "%*s %lu", &ts) != 1)
            return 0;

        return ts == (unsigned long)orig_ts;
    }

    return 1;                   /* an empty list */
}

/* the timestamp (pndir_tsstr()) in the name of a file of a diff, as in
   packages.ndir.2026.11.18-16.04.00.gz or packages.ndir.dscr.<tss>.gz */
#define TSS_LEN 19

const char *pndir_difftoc_find_tss(const char *name)
{
    static const char form[] = "dddd.dd.dd-dd.dd.dd";
    const char *p;
    int i;

    for (p = strchr(name, '.'); p && strlen(p + 1) >= TSS_LEN; p = strchr(p + 1, '.')) {
        const char *t = p + 1;

        for (i = 0; i < TSS_LEN; i++) {
            if (form[i] == 'd' ? !isdigit((unsigned char)t[i]) : t[i] != form[i])
                break;
        }

        if (i == TSS_LEN && (t[TSS_LEN] == '.' || t[TSS_LEN] == '\0'))
            return t;
    }

    return NULL;
}

/* the files of the diffs the list no longer names: the trimmed ones, with
   their descriptions, and those of a diff a failed --mkidx did not list */
static void rm_unlisted_diffs(const char *idxpath, tn_array *lines, int from)
{
    char           tmp[PATH_MAX], dir[PATH_MAX], path[PATH_MAX], *dn, *stem;
    tn_array       *listed;
    struct dirent  *ent;
    struct stat    st;
    size_t         stemlen;
    DIR            *d;
    int            i;

    n_snprintf(tmp, sizeof(tmp), "%s", idxpath);
    if (!pndir_index_stem(tmp, &dn, &stem))
        return;
    stemlen = strlen(stem);

    listed = n_array_new(16, free, (tn_fn_cmp)strcmp);
    for (i = from; i < n_array_size(lines); i++) {
        const char *l = n_array_nth(lines, i), *t;
        char name[PATH_MAX];

        n_snprintf(name, sizeof(name), "%.*s", (int)strcspn(l, " \n"), l);
        if ((t = pndir_difftoc_find_tss(name)))
            n_array_push(listed, n_strdupl(t, TSS_LEN));
    }

    n_array_sort(listed);

    n_snprintf(dir, sizeof(dir), "%s/%s", dn, pndir_packages_incdir);
    if ((d = opendir(dir)) == NULL) {
        n_array_free(listed);
        return;
    }

    while ((ent = readdir(d)) != NULL) {
        const char *t;
        char tss[TSS_LEN + 1];

        if (strncmp(ent->d_name, stem, stemlen) != 0)
            continue;

        if (ent->d_name[stemlen] != '.')
            continue;

        if ((t = pndir_difftoc_find_tss(ent->d_name + stemlen)) == NULL)
            continue;           /* the list itself */

        n_snprintf(tss, sizeof(tss), "%.*s", TSS_LEN, t);
        if (n_array_bsearch(listed, tss))
            continue;

        n_snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;

        msgn(1, _("Removing outdated %s"), ent->d_name);
        if (unlink(path) != 0)
            logn(LOGWARN, "unlink %s: %m", path);
    }

    closedir(d);
    n_array_free(listed);
}

/* the list is written whole, with the new diff added: a client finds the
   old list or the new one, never a cut entry, which would abort its read
   of the list.  The diffs it no longer lists are removed after it */
int pndir_difftoc_update(const struct pkgdir *pkgdir, const char *idxpath,
                         const char *diffpath, const char *tocpath)
{
    tn_array       *lines;
    tn_stream      *st;
    struct pndir   *idx;
    char           entry[PATH_MAX + 256];
    int            i, n, from = 0, ok = 1, trim;

    idx = pkgdir->mod_data;
    n_assert(idx && idx->md_orig);

    if ((lines = difftoc_read(tocpath)) == NULL) {
        /* the list is derived, and a client it does not lead to the index
           takes the whole one instead: a list that cannot be read is
           replaced rather than failing every --mkidx that changes the repo */
        logn(LOGWARN, _("%s: unreadable list of diffs, starting a new one"),
             tocpath);
        lines = n_array_new(32, free, NULL);
    }

    if (!pndir_difftoc_leads_to(lines, pkgdir->orig_ts)) {
        logn(LOGWARN, _("%s: a diff is missing from the list, the older diffs "
                        "are dropped"), tocpath);
        n_array_clean(lines);
    }

    n = n_snprintf(entry, sizeof(entry), "%s %lu %s %lu\n",
                   n_basenam(diffpath), (unsigned long)pkgdir->ts,
                   idx->md_orig, (unsigned long)pkgdir->orig_ts);
    n_array_push(lines, n_strdupl(entry, n));

    /* only a diff that adds packages trims the list */
    trim = pkgdir->pkgs && n_array_size(pkgdir->pkgs);
    if (trim && !poldek__is_in_testing_mode()) {
        if (!difftoc_vaccum(lines, idxpath, tocpath, &from))
            ok = 0;             /* the list is not trimmed */
    }

    st = n_stream_open(tocpath, "w",
                       TN_STREAM_UNKNOWN | TN_STREAM_ATOMIC | TN_STREAM_SYNC);
    if (st == NULL) {
        logn(LOGERR, "%s: %m", tocpath);
        ok = 0;
        goto l_end;
    }

    for (i = from; i < n_array_size(lines); i++) {
        const char *l = n_array_nth(lines, i);
        int len = strlen(l);

        if (len && n_stream_write(st, l, len) != len) {
            n_stream_abort(st);
            st = NULL;
            break;
        }
    }

    if (st == NULL || n_stream_close(st) != 0) {
        logn(LOGERR, _("%s: unable to write the list of diffs: %m"),
             tocpath);
        ok = 0;
        goto l_end;             /* the old list, and its diffs, stay */
    }

    /* the tests keep every diff */
    if (!poldek__is_in_testing_mode())
        rm_unlisted_diffs(idxpath, lines, from);

 l_end:
    n_array_free(lines);
    return ok;
}
