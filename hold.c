/* 
  Copyright (C) 2001 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <vfile/vfile.h>

#define  ENABLE_TRACE 0

#include "i18n.h"
#include "pkgset.h"
#include "log.h"
#include "pkg.h"



tn_array *read_holds(const char *fpath, tn_array *hold_pkgnames)
{
    char              buf[1024], path[PATH_MAX];
    struct vfile      *vf;

    if (fpath == NULL) {
        char *homedir;

        if ((homedir = getenv("HOME")) == NULL)
            return NULL;
        
        snprintf(path, sizeof(path), "%s/.poldek_hold", homedir);
        if (access(path, R_OK) != 0)
            return hold_pkgnames;

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
            DBGMSG_F("read %s\n", p);
            n_array_push(hold_pkgnames, n_strdup(p));
        }
        
    }
    
    vfile_close(vf);
    
    return hold_pkgnames;
}

static 
void mark_holds(tn_array *pkgs, tn_array *hold_pkgnames) 
{
    int i, j;

    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        for (j=0; j<n_array_size(hold_pkgnames); j++) {
            const char *mask = n_array_nth(hold_pkgnames, j);

            if (fnmatch(mask, pkg->name, 0) == 0) {
                pkg_mark_hold(pkg);
                DBGMSG_F("HOLD %s\n", pkg_snprintf_s(pkg));
                break;
            }
        }
    }
}


void pkgset_mark_holds(struct pkgset *ps, tn_array *hold_pkgnames) 
{
    int i;

    for (i=0; i<n_array_size(ps->pkgdirs); i++) {
        struct pkgdir *pkgdir = n_array_nth(ps->pkgdirs, i);
        
        if (pkgdir->pkgs)
            mark_holds(pkgdir->pkgs, hold_pkgnames);
    }
}
