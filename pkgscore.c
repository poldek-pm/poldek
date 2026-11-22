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

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>
#include <sys/param.h>          /* for PATH_MAX */

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nmalloc.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>

#include <vfile/vfile.h>

#undef  ENABLE_TRACE
#define  ENABLE_TRACE 0

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgdir/pkgdir.h"
#include "pkgmisc.h"
#include "misc.h"
#include "poldek_intern.h"

static
tn_array *read_patterns(const char *fpath, tn_array *patterns, unsigned type)
{
    char              buf[1024], path[PATH_MAX];
    struct vfile      *vf;

    if (fpath == NULL) {
        char home[512];
        if (get_homedir(home, sizeof(home)) == NULL)
            return NULL;

        switch (type) {
            case PKG_HELD:
                snprintf(path, sizeof(path), "%s/.poldek-hold", home);
                if (access(path, R_OK) != 0) /* backward compat */
                    snprintf(path, sizeof(path), "%s/.poldek_hold", home);

                break;

            case PKG_IGNORED:
                snprintf(path, sizeof(path), "%s/.poldek-ignore", home);
                break;

            default:
                n_assert(0);
                break;
        }

        if (access(path, R_OK) != 0)
            return patterns;

        fpath = path;
    }

    if ((vf = vfile_open(fpath, VFT_STDIO, VFM_RO)) == NULL)
        return NULL;

    while (fgets(buf, sizeof(buf), vf->vf_stream)) {
        char *p;
        int  len;


        p = buf;
        while (isspace(*p))
            p++;

        if (*p == '#')
            continue;

        len = strlen(buf);
        len--;
        while (isspace(buf[len]))
            buf[len--] = '\0';

        if (*p) {
            DBGF("read %s\n", p);
            n_array_push(patterns, n_strdup(p));
        }

    }

    vfile_close(vf);
    return patterns;
}


/* a package may have none */
static const char *arch_of(const struct pkg *pkg)
{
    const char *arch = pkg_arch(pkg);

    return arch ? arch : "(null)";
}

/* "source:name-version-release.arch" in buf, which fits it; the length of
   "source:": 0 for a package of no source */
static size_t snprintf_forms(char *buf, size_t size, const struct pkg *pkg)
{
    size_t n = 0;

    /* a source given as a bare path (-s PATH) has no name */
    if (pkg->pkgdir)
        n = n_snprintf(buf, size, "%s:",
                       pkg->pkgdir->name ? pkg->pkgdir->name : "");

    n_snprintf(&buf[n], size - n, "%s-%s-%s.%s", pkg->name, pkg->ver,
               pkg->rel, arch_of(pkg));
    return n;
}

static size_t forms_size(const struct pkg *pkg)
{
    size_t n = strlen(pkg->name) + strlen(pkg->ver) + strlen(pkg->rel) +
        strlen(arch_of(pkg)) + 4;

    if (pkg->pkgdir)
        n += (pkg->pkgdir->name ? strlen(pkg->pkgdir->name) : 0) + 1;

    return n;
}

void pkgscore_match_init(struct pkgscore_s *psc, struct pkg *pkg)
{
    psc->pkg = pkg;

    /* pkgname_off: the length of "source:", or -1 with pkgbuf too small
       for the forms, which pkgscore_match() then makes itself */
    if (forms_size(pkg) > sizeof(psc->pkgbuf)) {
        psc->pkgbuf[0] = '\0';
        psc->pkgname_off = -1;
        return;
    }

    psc->pkgname_off = snprintf_forms(psc->pkgbuf, sizeof(psc->pkgbuf), pkg);
}

/* fnmatch() against buf from start, cut after end for the call */
static int match_cut(char *buf, const char *mask, size_t start, size_t end)
{
    char c;
    int rc;

    c = buf[end];
    buf[end] = '\0';
    rc = fnmatch(mask, &buf[start], 0) == 0;
    buf[end] = c;

    return rc;
}

/* a mask matches a package's name, name-version-release or
   name-version-release.arch, each also after "source:"; the forms are cut
   from the one string, so struct pkgscore_s keeps its size */
static int match_forms(char *buf, size_t off, const struct pkg *pkg,
                       const char *mask)
{
    size_t name = off + strlen(pkg->name);
    size_t nvr = name + strlen(pkg->ver) + strlen(pkg->rel) + 2;

    if (match_cut(buf, mask, off, name) || match_cut(buf, mask, off, nvr) ||
        fnmatch(mask, &buf[off], 0) == 0)
        return 1;

    return off && (match_cut(buf, mask, 0, name) ||
                   match_cut(buf, mask, 0, nvr) ||
                   fnmatch(mask, buf, 0) == 0);
}

// return 0 if not match
int pkgscore_match(struct pkgscore_s *psc, const char *mask)
{
    size_t size, off;
    char *buf;
    int rc;

    if (psc->pkgname_off >= 0)
        return match_forms(psc->pkgbuf, psc->pkgname_off, psc->pkg, mask);

    size = forms_size(psc->pkg);
    buf = n_malloc(size);
    off = snprintf_forms(buf, size, psc->pkg);
    rc = match_forms(buf, off, psc->pkg, mask);
    free(buf);

    return rc;
}


/* hold = MASK in source NAME's section stands for NAME:MASK in [ global ],
   with the glob characters of NAME escaped */
void poldek__add_source_holds(tn_array *holds, const char *source,
                              const tn_array *masks)
{
    char *name = alloca(2 * strlen(source) + 1), *p = name;
    int i;

    for (; *source; source++) {
        if (strchr("*?[\\", *source))
            *p++ = '\\';
        *p++ = *source;
    }
    *p = '\0';

    /* the user's ~/.poldek-hold stands in for no hold in [ global ] */
    if (n_array_size(holds) == 0)
        read_patterns(NULL, holds, PKG_HELD);

    for (i=0; i < n_array_size(masks); i++)
        n_array_push(holds, n_str_concat(name, ":", n_array_nth(masks, i),
                                         NULL));

    n_array_sort(holds);
    n_array_uniq(holds);
}

void packages_score(tn_array *pkgs, tn_array *patterns, unsigned scoreflag)
{
    int i, j;


    n_assert(patterns);
    if (n_array_size(patterns) == 0)
        read_patterns(NULL, patterns, scoreflag);

    if (n_array_size(patterns) == 0)
        return;

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkgscore_s  psc;
        struct pkg         *pkg;

        pkg = n_array_nth(pkgs, i);
        pkgscore_match_init(&psc, pkg);

        for (j=0; j < n_array_size(patterns); j++) {
            const char *mask = n_array_nth(patterns, j);

            pkg_clr_score(pkg, scoreflag);

            if (pkgscore_match(&psc, mask)) {
                switch (scoreflag) {
                    case PKG_HELD:
                        msgn(3, "held %s", pkg_id(pkg));
                        DBGF("HELD %s\n", pkg_snprintf_s(pkg));
                        pkg_score(pkg, PKG_HELD);
                        break;

                    case PKG_IGNORED:
                        msgn(3, "ignored %s", pkg_id(pkg));
                        DBGF("IGNORED %s\n", pkg_snprintf_s(pkg));
                        pkg_score(pkg, PKG_IGNORED);
                        break;

                    default:
                        n_assert(0);
                        break;
                }

                break;
            }
        }
    }
}

static int cmp_isignored(struct pkg *pkg, void *dummy)
{
    dummy = dummy;
    if (pkg_is_scored(pkg, PKG_IGNORED))
        return 0;

    return 1;
}

int packages_score_ignore(tn_array *pkgs, tn_array *patterns, int remove)
{
    int n = n_array_size(pkgs);

    packages_score(pkgs, patterns, PKG_IGNORED);
    if (remove)
        n_array_remove_ex(pkgs, NULL, (tn_fn_cmp)cmp_isignored);

    return n - n_array_size(pkgs);
}
