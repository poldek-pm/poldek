/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@k2.net.pl>

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

#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <trurl/nassert.h>
#include <vfile/vfile.h>

#include "pkgset.h"
#include "source.h"
#include "log.h"
#include "misc.h"
#include "i18n.h"
#include "depdirs.h"

int pkgset_load(struct pkgset *ps, int ldflags, tn_array *sources)
{
    int i, j, iserr = 0;
    struct pkgdir *pkgdir = NULL;

    n_array_isort_ex(sources, (tn_fn_cmp)source_cmp_pri);
    
    for (i=0; i < n_array_size(sources); i++) {
        struct source *src = n_array_nth(sources, i);

        if (src->flags & PKGSOURCE_NOAUTO)
            continue;

        if (src->type == PKGSRCT_NIL) 
            src->type = PKGSRCT_IDX;
        
        switch (src->type) {
            case PKGSRCT_IDX:
                pkgdir = pkgdir_new(src->name, src->path,
                                    src->pkg_prefix, PKGDIR_NEW_VERIFY);
                if (pkgdir != NULL) 
                    break;
                
                if (is_dir(src->path)) 
                    src->type = PKGSRCT_DIR; /* no break */
                else
                    break;
                
            case PKGSRCT_DIR: {
                const char *path = src->path;
                if (src->pkg_prefix)
                    path = src->pkg_prefix;
                msg(1, _("Loading %s..."), vf_url_slim_s(path, 0));
                pkgdir = pkgdir_load_dir(src->name, path);
            }
                break;

            case PKGSRCT_HDL:
                msgn(1, _("Loading %s..."), vf_url_slim_s(src->path, 0));
                pkgdir = pkgdir_load_hdl(src->name, src->path, src->pkg_prefix);
                break;

            default:
                n_assert(0);
                break;
        }

        if (pkgdir == NULL) {
            if (n_array_size(sources) > 1)
                logn(LOGWARN, _("%s: load failed, skipped"), vf_url_slim_s(src->path, 0));
            continue;
        }

        if (src->flags & (PKGSOURCE_VRFY_GPG | PKGSOURCE_VRFY_SIGN))
            pkgdir->flags |= PKGDIR_VRFY_GPG;

        if (src->flags & PKGSOURCE_VRFY_PGP)
            pkgdir->flags |= PKGDIR_VRFY_PGP;

        pkgdir->pri = src->pri;
        n_array_push(ps->pkgdirs, pkgdir);
    }


    /* merge pkgdis depdirs into ps->depdirs */
    for (i=0; i<n_array_size(ps->pkgdirs); i++) {
        pkgdir = n_array_nth(ps->pkgdirs, i);
        
        if (pkgdir->depdirs) {
            for (j=0; j<n_array_size(pkgdir->depdirs); j++)
                n_array_push(ps->depdirs, n_array_nth(pkgdir->depdirs, j));
        }
    }

    n_array_sort(ps->depdirs);
    n_array_uniq(ps->depdirs);

    
    for (i=0; i<n_array_size(ps->pkgdirs); i++) {
        pkgdir = n_array_nth(ps->pkgdirs, i);

        if (pkgdir->flags & PKGDIR_LDFROM_IDX) {
            msgn(1, _("Loading %s..."), vf_url_slim_s(pkgdir->idxpath, 0));
            if (!pkgdir_load(pkgdir, ps->depdirs, ldflags)) {
                logn(LOGERR, _("%s: load failed"), pkgdir->idxpath);
                iserr = 1;
            }
        }
    }
    
    if (!iserr) {
        /* merge pkgdirs packages into ps->pkgs */
        for (i=0; i<n_array_size(ps->pkgdirs); i++) {
            pkgdir = n_array_nth(ps->pkgdirs, i);
            for (j=0; j < n_array_size(pkgdir->pkgs); j++)
                n_array_push(ps->pkgs, pkg_link(n_array_nth(pkgdir->pkgs, j)));
        }

        init_depdirs(ps->depdirs);
    }
    
    if (n_array_size(ps->pkgs)) {
        int n = n_array_size(ps->pkgs);
        msgn(1, ngettext("%d package read",
                         "%d packages read", n), n);
    }
    
    return n_array_size(ps->pkgs);
}
