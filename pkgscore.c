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
#include "log.h"
#include "pkg.h"


static
tn_array *read_patterns(const char *fpath, tn_array *patterns, unsigned type)
{
    char              buf[1024], path[PATH_MAX];
    struct vfile      *vf;

    if (fpath == NULL) {
        char *homedir;

        if ((homedir = getenv("HOME")) == NULL)
            return NULL;

        switch (type) {
            case PKG_HELD:
                snprintf(path, sizeof(path), "%s/.poldek-hold", homedir);
                if (access(path, R_OK) != 0) /* backward compat */
                    snprintf(path, sizeof(path), "%s/.poldek_hold", homedir);
        	
                break;

            case PKG_IGNORED:
                snprintf(path, sizeof(path), "%s/.poldek-ignore", homedir);
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
            DBGMSG_F("read %s\n", p);
            n_array_push(patterns, n_strdup(p));
        }
        
    }
    
    vfile_close(vf);
    return patterns;
}

static int pkg_cmp_ignored_(struct pkg *pkg, void *dummy) 
{
    dummy = dummy;
    if (pkg->flags & PKG_IGNORED)
        return 0;
    
    return 1;
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
        struct pkg *pkg = n_array_nth(pkgs, i);

        for (j=0; j<n_array_size(patterns); j++) {
            const char *mask = n_array_nth(patterns, j);
            
            if (fnmatch(mask, pkg->name, 0) == 0) {
                switch (scoreflag) {
                    case PKG_HELD:
                        msgn(3, "held %s", pkg_snprintf_s(pkg));
                        DBGMSG_F("HELD %s\n", pkg_snprintf_s(pkg));
                        pkg_score(pkg, PKG_HELD);
                        break;

                    case PKG_IGNORED:
                        msgn(3, "ignored %s", pkg_snprintf_s(pkg));
                        DBGMSG_F("IGNORED %s\n", pkg_snprintf_s(pkg));
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

    if (scoreflag == PKG_IGNORED)
        n_array_remove_ex(pkgs, NULL, (tn_fn_cmp)pkg_cmp_ignored_);
    
}