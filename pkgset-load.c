/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <string.h>
#include <sys/types.h>
#include <unistd.h>


#include <trurl/nassert.h>
#include <trurl/nstr.h>

#include <vfile/vfile.h>

#include "pkgset.h"
#include "pkgset-load.h"
#include "misc.h"
#include "log.h"


struct source *source_new(const char *path, const char *pkg_prefix)
{
    struct source *src;

    src = malloc(sizeof(*src));
    src->source_path = strdup(path);
    if (pkg_prefix)
        src->pkg_prefix = strdup(pkg_prefix);
    src->ldmethod = 0;
    
    return src;
}

void source_free(struct source *src)
{
    
    free(src->source_path);
    if (src->pkg_prefix)
        free(src->pkg_prefix);
    free(src);
}


int source_cmp(struct source *s1, struct source *s2)
{
    return strcmp(s1->source_path, s2->source_path);
}

int source_update(struct source *src)
{
    return update_pkgdir_idx(src->source_path);
}


static int select_ldmethod(const char *path) 
{
    struct stat st;
    int ldmethod = 0;
    
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        ldmethod = PKGSET_LD_DIR;
    else 
        ldmethod = PKGSET_LD_IDX;
    
        
    return ldmethod;
}



int pkgset_load(struct pkgset *ps, int ldflags, tn_array *sources)
{
    int i, j;
    struct pkgdir *pkgdir = NULL;


    for (i=0; i<n_array_size(sources); i++) {
        struct source *src = n_array_nth(sources, i);

        if (src->ldmethod == 0)
            src->ldmethod = select_ldmethod(src->source_path);
        
        if (src->ldmethod == 0) {
            log(LOGERR, "%s: cannot determine load method\n", src->source_path);
            continue;
        }
    
        switch (src->ldmethod) {
            case PKGSET_LD_DIR:
                msg(1, "Loading %s...\n", src->source_path);
                pkgdir = pkgdir_load_dir(src->source_path);
                break;
                
            case PKGSET_LD_IDX:
                pkgdir = pkgdir_new(src->source_path, src->pkg_prefix);
                break;

            default:
                n_assert(0);
        }

        if (pkgdir == NULL) {
            log(LOGERR, "%s: load failed, skiped\n", src->source_path);
            continue;
        }
        
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
            msg(1, "Loading %s...\n", pkgdir->idxpath);
            pkgdir_load(pkgdir, ps->depdirs, ldflags);
        }
    }


    /* merge pkgdirs packages into ps->pkgs */
    for (i=0; i<n_array_size(ps->pkgdirs); i++) {
        pkgdir = n_array_nth(ps->pkgdirs, i);
        for (j=0; j<n_array_size(pkgdir->pkgs); j++)
            n_array_push(ps->pkgs, pkg_link(n_array_nth(pkgdir->pkgs, j)));
    }

    if (n_array_size(ps->pkgs))
        msg(1, "%d packages read\n", n_array_size(ps->pkgs));

    return n_array_size(ps->pkgs);
}
