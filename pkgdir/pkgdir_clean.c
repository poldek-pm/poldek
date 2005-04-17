/*
  Copyright (C) 2000 - 2005 Pawel A. Gajda <mis@k2.net.pl>

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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>

#include <vfile/vfile.h>

#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pkgdir.h"
#include "pkgdir_intern.h"

static int do_unlink(const char *path) 
{
    msgn(1, _(" Removing %s"), n_basenam(path));
    //return vf_localunlink(path);
    return 1;
}


int pkgdir__rmf(const char *dirpath, const char *mask) 
{
    struct dirent  *ent;
    DIR            *dir;
    struct stat    st;
    char           *sepchr = "/";
    int            msg_displayed = 0, rc = 1;
    struct vflock  *vflock = NULL;

    msgn(3, "rm -f %s/%s", dirpath, mask ? mask : "*");

    if (mask && n_str_eq(mask, "*"))
        mask = NULL;
    
    if (stat(dirpath, &st) != 0)
        return 0;

    if ((vflock = vf_lockdir(dirpath)) == NULL) 
        return 0;
    
    if (S_ISREG(st.st_mode) && mask == NULL) {
        char *tmp, *dn;
        struct vflock *tmp_vflock;
            
        n_strdupap(dirpath, &tmp);
        dn = n_dirname(tmp);
        rc = 0;
        
        if ((tmp_vflock = vf_lockdir(dn))) {
            msgn(1, _("Cleaning up %s..."), dn);
            rc = do_unlink(dirpath);
            vf_lock_release(tmp_vflock);
        }
        return rc;
    }
    
    if ((dir = opendir(dirpath)) == NULL) {
        if (poldek_VERBOSE > 2)
            logn(LOGWARN, "opendir %s: %m", dirpath);
        return 1;
    }
    
    if (dirpath[strlen(dirpath) - 1] == '/')
        sepchr = "";
    
    while ((ent = readdir(dir))) {
        char path[PATH_MAX];
        struct stat st;
    
        if (*ent->d_name == '.') {
            if (ent->d_name[1] == '\0')
                continue;
            
            if (ent->d_name[1] == '.' && ent->d_name[2] == '\0')
                continue;
        }

        if (mask && fnmatch(mask, ent->d_name, 0) != 0)
            continue;

        /* do not remove locks */
        if (strcmp(ent->d_name, n_basenam(vflock->path)) == 0) {
            DBGF("skip %s\n", vflock->path);
            continue;
        }

        if (msg_displayed == 0) {
            msgn(1, _("Cleaning up %s..."), dirpath);
            msg_displayed = 1;
        }

        snprintf(path, sizeof(path), "%s%s%s", dirpath, sepchr, ent->d_name);
        if (stat(path, &st) == 0) {
            if (S_ISREG(st.st_mode))
                do_unlink(path);

            else if (S_ISDIR(st.st_mode))
                pkgdir__rmf(path, mask);
        }
    }
    
    if (vflock)
        vf_lock_release(vflock);
    closedir(dir);
    return 1;
}


int pkgdir__cache_clean(const char *path, const char *mask)
{
    char tmpath[PATH_MAX], path_i[PATH_MAX];

    if (vf_localdirpath(tmpath, sizeof(tmpath), path) < (int)sizeof(tmpath))
        pkgdir__rmf(tmpath, mask);

    n_snprintf(path_i, sizeof(path_i), "%s/%s", path, "packages.i");
    if (vf_localdirpath(tmpath, sizeof(tmpath), path_i) < (int)sizeof(tmpath))
        pkgdir__rmf(tmpath, mask);
    
    return 1;
}
