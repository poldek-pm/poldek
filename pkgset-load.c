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

#include <trurl/nassert.h>
#include <trurl/nstr.h>

#include "pkgset.h"
#include "pkgset-def.h"
#include "pkgset-load.h"
#include "misc.h"
#include "log.h"

static char *setup_pkgprefix(const char *path) 
{
    char *dn = NULL, *bn, *buf, *rpath = NULL;
    int len;

    len = strlen(path);
    buf = alloca(len + 1);
    memcpy(buf, path, len);
    buf[len] = '\0';
    
    n_basedirnam(buf, &dn, &bn);
    if (dn)
        rpath = strdup(dn);
    else
        rpath = strdup(".");

    return rpath;
}

    

int pkgset_load(struct pkgset *ps, int ldmethod, void *path,
                const char *prefix) 
{
    int n = 0;
    
    n_assert(ps->path == NULL);

    msg(1, "Reading package info from %s...\n", path);
    
    switch (ldmethod) {
        case PKGSET_LD_DIR:
            n = pkgset_load_dir(ps, path);
            if (n) {
                int len;
                ps->path = strdup((char*)path);
                len = strlen(path) - 1;
                if (ps->path[len] == '/')
                    ps->path[len] = '\0';
            }
            
            break;
            
        case PKGSET_LD_HDRFILE:
            n = pkgset_load_rpmidx(ps, path);
            if (n)
                ps->path = setup_pkgprefix(path);
            
            break;

        case PKGSET_LD_TXTFILE:
            n = pkgset_load_txtidx(ps, path);
            if (n)
                ps->path = setup_pkgprefix(path);
            break;
            
        default:
            n_assert(0);
    }

    if (n > 0) {
        msg(1, "%d packages read\n", n);
        n_assert(n_array_size(ps->pkgs) == n);
    }

    if (prefix) {
        if (ps->path)
            free(ps->path);
        ps->path = strdup(prefix);
    }
    
    return n > 0 ? n : 0;
}
