/* 
  Copyright (C) 2001 - 2002 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

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

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"
#include "rpm.h"

#ifdef HAVE_RPMCHECKSIG     /* don't really know in RPM versions with rpmCheckSig() */

int package_verify_sign(const char *path, unsigned flags) 
{
    unsigned rpmflags = 0;

    if (access(path, R_OK) != 0) {
        logn(LOGERR, "verify signature %s: %m", path);
        return 0;
    }
    
    if (flags & PKGVERIFY_PGPG)
        rpmflags |= CHECKSIG_PGP | CHECKSIG_GPG;
    
    if (flags & PKGVERIFY_MD)
        rpmflags |= CHECKSIG_MD5;

    return rpm_verify_signature(path, flags);
}

#else  /* HAVE_RPMCHECKSIG */

int package_verify_sign(const char *path, unsigned flags) 
{
    char **argv;
    char *cmd;
    int i, n, nopts = 0;
    
    
    n = 32;
    
    argv = alloca((n + 1) * sizeof(*argv));
    argv[n] = NULL;
    n = 0;
    
    cmd = "/bin/rpm";
    argv[n++] = "rpm";
    argv[n++] = "-K";

    nopts = n;
    
    if ((flags & PKGVERIFY_PGPG) == 0) {
        argv[n++] = "--nopgp";
        argv[n++] = "--nogpg";
    }
    
    if ((flags & PKGVERIFY_MD) == 0) {
        argv[n++] = "--nomd5";
    }
    n_assert(n > nopts);        /* any PKGVERIFY_* given? */
    
    argv[n++] = (char*)path;
    nopts = n;
    argv[n++] = NULL;
    
    if (verbose > 1) {
        char buf[1024], *p;
        p = buf;
        
        for (i=0; i < nopts; i++) 
            p += snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", argv[i]);
        *p = '\0';
        msgn(1, _("Executing%s..."), buf);
    }
    
    return rpmr_exec(cmd, argv, 0, 4) == 0;
}

#endif /* HAVE_RPMCHECKSIG */

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
        const char  *pkg_basename;
        int         len;
        
        urltype = vfile_url_type(pkgpath);
        
        if (urltype == VFURL_CDROM)
            ncdroms++;

        if (urltype == VFURL_PATH) {
            if (!package_verify_sign(pkgpath, PKGVERIFY_MD))
                nerr++;
            continue;
        }

        pkg_basename = pkg_filename_s(pkg);
        if (nosubdirs) {
            snprintf(path, sizeof(path), "%s/%s", destdir, pkg_basename);
            
        } else {
            char buf[PATH_MAX];
            
            vfile_url_as_dirpath(buf, sizeof(buf), pkgpath);
            snprintf(path, sizeof(path), "%s/%s/%s", destdir, buf,
                     pkg_basename);
        }

        if (access(path, R_OK) == 0) {
            int v = rpmlib_verbose;
            
            rpmlib_verbose = -2; /* be quiet */
            if (package_verify_sign(path, PKGVERIFY_MD)) {
                rpmlib_verbose = v;
                continue;           /* already downloaded */
                
            } else if (vfile_valid_path(path)) 
                unlink(path);
            
            rpmlib_verbose = v;
        }
        
        if ((urls = n_hash_get(urls_h, pkgpath)) == NULL) {
            urls = n_array_new(n_array_size(pkgs), NULL, NULL);
            n_hash_insert(urls_h, pkgpath, urls);
            n_array_push(urls_arr, pkgpath);
        }
        
        len = snprintf(path, sizeof(path), "%s/%s", pkgpath, pkg_basename);
        
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
        
        else {
            int j;
                
            for (j=0; j < n_array_size(urls); j++) {
                char localpath[PATH_MAX];
                snprintf(localpath, sizeof(localpath), "%s/%s", real_destdir,
                         n_basenam(n_array_nth(urls, j)));
                if (!package_verify_sign(localpath, PKGVERIFY_MD))
                    nerr++;
            }
        }
    }
    
    n_array_free(urls_arr);
    n_hash_free(urls_h);
    return nerr == 0;
}

