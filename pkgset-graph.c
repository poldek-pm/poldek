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

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <trurl/trurl.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"
#include "pkgmisc.h"
#include "capreq.h"
#include "pkgset.h"
#include "fileindex.h"

#if 0                           /* probably nobody need this */
/* GraphViz */
static int dot_graph(tn_array *pkgs, struct pkgset *ps, const char *outfile)
{
    int i, j, n_unmet = 0;
    tn_buf *nbuf;
    tn_array *errs;
    FILE *stream;

    nbuf = n_buf_new(1024 * 8);

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        if ((errs = get_unsatisfied_reqs(ps, pkg))) {
            for (j=0; j < n_array_size(errs); j++) {
                struct pkg_unreq *unreq = n_array_nth(errs, j);
                n_buf_printf(nbuf, "\"%s\" -> \"UNMET\" [ label = \"%s\" ];\n",
                             pkg_id(pkg), unreq->req);
                n_unmet++;
            }
        }

        tn_array *reqpkgs = pkgset_get_required_packages(0, ps, pkg);
        if (reqpkgs == NULL || n_array_size(reqpkgs) == 0) {
            n_buf_printf(nbuf, "\"%s\";\n", pkg_id(pkg));
            continue;
        }

        for (j=0; j < n_array_size(reqpkgs); j++) {
            struct reqpkg *rp = n_array_nth(reqpkgs, j);

            n_buf_printf(nbuf, "\"%s\" -> \"%s\" [ label = \"%s\" ];\n",
                         pkg_id(pkg), pkg_id(rp->pkg), capreq_snprintf_s(rp->req));

            if (rp->flags & REQPKG_MULTI) {
                int n = 0;
                while (rp->adds[n]) {
                    n_buf_printf(nbuf, "\"%s\" -> \"%s\" [ label = \"%s\" ];\n",
                                 pkg_id(pkg), pkg_id(rp->adds[n]->pkg),
                                 capreq_snprintf_s(rp->req));
                    n++;
                }
            }
        }

        n_array_cfree(&reqpkgs);
    }

    stream = stdout;

    if (outfile != NULL)
        if ((stream = fopen(outfile, "w")) == NULL) {
            logn(LOGERR, _("%s: open failed: %m"), outfile);
            n_buf_free(nbuf);
            return 0;
        }

    fprintf(stream, "digraph repo {\n"
            "rankdir=LR;\n"
            "ordering=out\n"
            "mclimit=2.0\n"
            "charset=\"utf-8\"\n"
            "graph [fontsize=10];\n"
            "edge  [fontsize=8,color=\"gray\"]\n"
            "node  [fontsize=10];\n"
            "node [shape = ellipse];\n");

    if (n_unmet > 0)
        fprintf(stream, "node [shape = box] UNMET;\nnode [shape = ellipse];\n");

    fprintf(stream, "%s", (char*)n_buf_ptr(nbuf));
    fprintf(stream, "\n}\n");
    if (stream != stdout)
        fclose(stream);
    n_buf_free(nbuf);

    if (outfile)
        msgn(0, _("Graph saved as %s"), outfile);

    return 1;
}


/* See http://xavier.informatics.indiana.edu/lanet-vi/ */
static int lanvi_graph(tn_array *pkgs, struct pkgset *ps, const char *outfile)
{
    int i, j;
    tn_buf *nbuf;
    FILE *stream;

    ps = ps;                    /* unused */
    nbuf = n_buf_new(1024 * 8);

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        pkg->recno = i + 1;
    }

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        tn_array *reqpkgs = pkgset_get_required_packages(0, ps, pkg);
        if (reqpkgs == NULL || n_array_size(reqpkgs) == 0)
            continue;

        for (j=0; j < n_array_size(reqpkgs); j++) {
            struct reqpkg *rp = n_array_nth(reqpkgs, j);

            n_buf_printf(nbuf, "%d %d\n", pkg->recno, rp->pkg->recno);

            if (rp->flags & REQPKG_MULTI) {
                int n = 0;
                while (rp->adds[n]) {
                    n_buf_printf(nbuf, "%d %d\n", pkg->recno, rp->adds[n]->pkg->recno);
                    n++;
                }
            }
        }

        n_array_cfree(&reqpkgs);
    }

    stream = stdout;
    if (outfile != NULL)
        if ((stream = fopen(outfile, "w")) == NULL) {
            logn(LOGERR, _("%s: open failed: %m"), outfile);
            n_buf_free(nbuf);
            return 0;
        }


    fprintf(stream, "%s", (char*)n_buf_ptr(nbuf));
    if (stream != stdout)
        fclose(stream);
    n_buf_free(nbuf);

    if (outfile)
        msgn(0, _("LanVi graph saved as %s"), outfile);

    return 1;
}


int packages_generate_depgraph(tn_array *pkgs, struct pkgset *ps,
                               const char *graphspec)
{

    const char **tl = NULL, *type, *path = NULL;

    if (strchr(graphspec, ':') == NULL)
        type = graphspec;
    else {
        tl = n_str_tokl(graphspec, ":");
        type = *tl;
        path = *(tl + 1);
    }
    msgn(3, "g %s\n", path);

    if (n_str_eq(type, "lanvi"))
        lanvi_graph(pkgs, ps, path);

    else if (n_str_eq(type, "dot"))
        dot_graph(pkgs, ps, path);

    else
        logn(LOGERR, "%s: unknown graph type", type);

    if (tl)
        n_str_tokl_free(tl);

    return 1;
}
#endif  /* graphs */
