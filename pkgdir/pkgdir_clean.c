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

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pkgdir.h"


static int do_unlink(const char *path) 
{
    msgn(2, _("Removing %s"), vf_url_slim_s(path, 69));
    return vf_localunlink(path);
}

int pkgdir_rmf(const char *dirpath, const char *mask) 
{
    struct dirent  *ent;
    DIR            *dir;
    struct stat    st;
    char           *sepchr = "/";
    int            msg_displayed = 0;


    log(LOGNOTICE, "RMF %s, %s\n", dirpath, mask);
    
    if (stat(dirpath, &st) != 0)
        return 0;
    
    if (S_ISREG(st.st_mode) && mask == NULL) 
        return do_unlink(dirpath);
    
    if ((dir = opendir(dirpath)) == NULL) {
        if (verbose > 2)
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

        if (msg_displayed == 0) {
            msgn(1, _("Cleaning up %s..."), dirpath);
            msg_displayed = 1;
        }

        snprintf(path, sizeof(path), "%s%s%s", dirpath, sepchr, ent->d_name);
        if (stat(path, &st) == 0) {
            if (S_ISREG(st.st_mode))
                do_unlink(path);
                
            else if (S_ISDIR(st.st_mode))
                pkgdir_rmf(path, mask);
        }
    }
    
    closedir(dir);
    return 1;
}

static int unlink_vf_dir(const char *path, const char *mask)
{
    char tmpath[PATH_MAX];

    log(LOGNOTICE, "RM %s, %s\n", path, mask);
    if (vf_localdirpath(tmpath, sizeof(tmpath), path) < (int)sizeof(tmpath))
        pkgdir_rmf(tmpath, mask);
    
    return 1;
}


int pkgdir_clean_cache(const char *type, const char *path, unsigned flags)
{
    const struct pkgdir_module  *mod;
    char                        url[PATH_MAX], *p;
    int                         urltype;

    
    if ((urltype = vf_url_type(path)) == VFURL_UNKNOWN)
        return 1;
    
    if (urltype & VFURL_LOCAL)
        return 1;
    
    if ((mod = pkgdir_find_mod(type)) == NULL) {
        logn(LOGERR, _("%s: unknown index type"), type);
        return 0;
    }

    pkgdir_make_idx_url(url, sizeof(url), path, mod->idx_filename);
    if ((p = strrchr(url, '/'))) {
        char mask[1024], *q;
        
        *p = '\0';
        p++;
        
        if ((q = strrchr(n_basenam(p), '.')) != NULL) {
            q++;
            if (strcmp(q, "gz") == 0 || strcmp(q, "bz2"))
                *q = '\0';
        }
        
        n_snprintf(mask, sizeof(mask), "%s*", p);
        unlink_vf_dir(url, mask);
    }
    
    return 1;
}

