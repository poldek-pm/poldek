/* 
  Copyright (C) 2001 - 2002 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>

#include <vfile/vfile.h>

#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"


int packages_fetch(tn_array *pkgs, const char *destdir, int nosubdirs)
{
    int       i, nerr, urltype, ncdroms;
    tn_array  *urls = NULL;
    tn_array  *urls_arr = NULL;
    tn_hash   *urls_h = NULL;


    urls_h = n_hash_new(21, (tn_fn_free)n_array_free);
    urls_arr = n_array_new(n_array_size(pkgs), NULL, (tn_fn_cmp)strcmp);
    
    n_hash_ctl(urls_h, TN_HASH_NOCPKEY);
    
    // group by URL
    ncdroms = 0;
    for (i=0; i<n_array_size(pkgs); i++) {
        struct pkg  *pkg = n_array_nth(pkgs, i);
        char        *pkgpath = pkg->pkgdir->path;
        char        path[PATH_MAX], *s;
        int         len;

        if ((urltype = vfile_url_type(pkgpath)) == VFURL_PATH)
            continue;
        
        if (urltype == VFURL_CDROM)
            ncdroms++;

        if ((urls = n_hash_get(urls_h, pkgpath)) == NULL) {
            urls = n_array_new(n_array_size(pkgs), NULL, NULL);
            n_hash_insert(urls_h, pkgpath, urls);
            n_array_push(urls_arr, pkgpath);
        }
        
        len = snprintf(path, sizeof(path), "%s/%s", pkgpath,
                       pkg_filename_s(pkg));
        
        s = alloca(len + 1);
        memcpy(s, path, len);
        s[len] = '\0';
        n_array_push(urls, s);
    }

    /* files must be copied if they are taken from more than
       one removable media;
       
       Not so nice, but it works
    */
    if (ncdroms > 1) 
        putenv("POLDEK_VFJUGGLE_CPMODE=copy");
    
    else if (ncdroms == 1) 
        putenv("POLDEK_VFJUGGLE_CPMODE=link");

    nerr = 0;
    for (i=0; i<n_array_size(urls_arr); i++) {
        char path[PATH_MAX];
        const char *real_destdir;
        char *pkgpath = n_array_nth(urls_arr, i);

        urls = n_hash_get(urls_h, pkgpath);
        real_destdir = destdir;
        if (nosubdirs == 0) {
            char buf[1024];
            
            vfile_url_as_dirpath(buf, sizeof(buf), pkgpath);
            snprintf(path, sizeof(path), "%s/%s", destdir, buf);
            real_destdir = path;
        }
        
        if (!vfile_fetcha(real_destdir, urls, VFURL_UNKNOWN))
            nerr++;
    }
    
    n_array_free(urls_arr);
    n_hash_free(urls_h);
    return nerr == 0;
}


int old_packages_fetch(tn_array *pkgs, const char *destdir, int nosubdirs)
{
    int       i, nerr, urltype;
    tn_array  *urls = NULL;
    tn_array  *urls_arr = NULL;
    tn_hash   *urls_h = NULL;


    urls_h = n_hash_new(21, (tn_fn_free)n_array_free);
    urls_arr = n_array_new(n_array_size(pkgs), NULL, (tn_fn_cmp)strcmp);
    
    n_hash_ctl(urls_h, TN_HASH_NOCPKEY);
    
    for (i=0; i<n_array_size(pkgs); i++) {
        struct pkg  *pkg = n_array_nth(pkgs, i);
        char        *pkgpath = pkg->pkgdir->path;
        char        path[PATH_MAX], *s;
        int         len;

        if ((urltype = vfile_url_type(pkgpath)) == VFURL_PATH)
            continue;

        if ((urls = n_hash_get(urls_h, pkgpath)) == NULL) {
            urls = n_array_new(n_array_size(pkgs), NULL, NULL);
            n_hash_insert(urls_h, pkgpath, urls);
            n_array_push(urls_arr, pkgpath);
        }
        
        len = snprintf(path, sizeof(path), "%s/%s", pkgpath,
                       pkg_filename_s(pkg));
        
        s = alloca(len + 1);
        memcpy(s, path, len);
        s[len] = '\0';
        n_array_push(urls, s);
    }

    nerr = 0;
    for (i=0; i<n_array_size(urls_arr); i++) {
        char path[PATH_MAX];
        char *pkgpath = n_array_nth(urls_arr, i);

        urls = n_hash_get(urls_h, pkgpath);

        if (nosubdirs == 0) {
            char buf[1024];
            
            vfile_url_as_dirpath(buf, sizeof(buf), pkgpath);
            snprintf(path, sizeof(path), "%s/%s", destdir, buf);
            destdir = path;
        }
        
        if (!vfile_fetcha(destdir, urls, VFURL_UNKNOWN))
            nerr++;
    }
    
    n_array_free(urls_arr);
    n_hash_free(urls_h);
    return nerr == 0;
}

