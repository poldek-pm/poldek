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
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>

#include <vfile/vfile.h>

#include <sigint/sigint.h>

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgmisc.h"
#include "pkgdir/pkgdir.h"
#include "misc.h"
#include "rpm/rpm.h"

#ifdef HAVE_RPM_4_0    
int package_verify_sign(const char *path, unsigned flags) 
{
    unsigned rpmflags = 0;

    if (access(path, R_OK) != 0) {
        logn(LOGERR, "%s: verify signature failed: %m", path);
        return 0;
    }
    
    if (flags & PKGVERIFY_GPG)
        rpmflags |= VRFYSIG_SIGNGPG;

    if (flags & PKGVERIFY_PGP)
        rpmflags |= VRFYSIG_SIGNPGP;
    
    if (flags & PKGVERIFY_MD)
        rpmflags |= VRFYSIG_DGST;

    return rpm_verify_signature(path, rpmflags);
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
    
    if ((flags & PKGVERIFY_GPG) == 0)
        argv[n++] = "--nogpg";

    if ((flags & PKGVERIFY_PGP) == 0)
        argv[n++] = "--nopgp";
    
    
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
            p += n_snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", argv[i]);
        *p = '\0';
        msgn(1, _("Executing%s..."), buf);
    }
    
    return rpmr_exec(cmd, argv, 0, 4) == 0;
}

#endif /* HAVE_RPMCHECKSIG */

int package_verify_pgpg_sign(const struct pkg *pkg, const char *localpath) 
{
    int rc = 1;

    if (pkg->pkgdir->flags & PKGDIR_VRFYSIGN) {
        int rv = rpmlib_verbose, v = verbose;
        unsigned verify_flags = 0;
        
        verbose = rpmlib_verbose = 1;

        if (pkg->pkgdir->flags & PKGDIR_VRFY_GPG)
            verify_flags |= PKGVERIFY_GPG;

        if (pkg->pkgdir->flags & PKGDIR_VRFY_PGP)
            verify_flags |= PKGVERIFY_PGP;
        
        if (!package_verify_sign(localpath, verify_flags)) {
            logn(LOGERR, "%s: signature verification failed", pkg_snprintf_s(pkg));
            rc = 0;
        }

        rpmlib_verbose = rv;
        verbose = v;
    }
    
    return rc;
}


int packages_fetch(tn_array *pkgs, const char *destdir, int nosubdirs)
{
    int       i, nerr, urltype, ncdroms;
    tn_array  *urls = NULL;
    tn_array  *urls_arr = NULL;
    tn_hash   *urls_h = NULL;
    tn_hash   *pkgdir_labels_h = NULL;


    n_assert(destdir);
    urls_h = n_hash_new(21, (tn_fn_free)n_array_free);
    pkgdir_labels_h = n_hash_new(21, NULL);
    n_hash_ctl(urls_h, TN_HASH_NOCPKEY);
    urls_arr = n_array_new(n_array_size(pkgs), NULL, (tn_fn_cmp)strcmp);
    
    // group by URL
    ncdroms = 0;
    nerr = 0;
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg  *pkg = n_array_nth(pkgs, i);
        char        *pkgpath = pkg->pkgdir->path;
        char        path[PATH_MAX], *s;
        const char  *pkg_basename;
        int         len;

        if (sigint_reached())
            break;
        
        urltype = vf_url_type(pkgpath);
        
        if (urltype == VFURL_CDROM)
            ncdroms++;

        pkg_basename = pkg_filename_s(pkg);

        if (urltype == VFURL_PATH) {
            int v = rpmlib_verbose;
            
            snprintf(path, sizeof(path), "%s/%s", pkgpath, pkg_basename);
            
            if (access(path, R_OK) != 0) {
                logn(LOGERR, "%s: %m", path);
                nerr++;
                
            } else {
                rpmlib_verbose = -2; /* be quiet */
                if (!package_verify_sign(path, PKGVERIFY_MD)) {
                    logn(LOGERR, _("%s: MD5 signature verification failed"),
                         n_basenam(path));
                    nerr++;
                }
                rpmlib_verbose = v;
            }
            	
            continue;
        }
        
        
        if (nosubdirs) {
            snprintf(path, sizeof(path), "%s/%s", destdir, pkg_basename);
            
        } else {
            char buf[PATH_MAX];
            
            vf_url_as_dirpath(buf, sizeof(buf), pkgpath);
            snprintf(path, sizeof(path), "%s/%s/%s", destdir, buf,
                     pkg_basename);
        }

        if (access(path, R_OK) == 0) {
            int pkg_ok, v = rpmlib_verbose;
            
            rpmlib_verbose = -2; /* be quiet */
            pkg_ok = package_verify_sign(path, PKGVERIFY_MD);
            rpmlib_verbose = v;
            
            if (pkg_ok)         /* we got it  */
                continue;
            else 
                vf_unlink(path);
        }
        
        if ((urls = n_hash_get(urls_h, pkgpath)) == NULL) {
            urls = n_array_new(n_array_size(pkgs), NULL, NULL);
            n_hash_insert(urls_h, pkgpath, urls);
            n_array_push(urls_arr, pkgpath);

            n_hash_insert(pkgdir_labels_h, pkgpath, pkg->pkgdir->name);
        }
        
        len = n_snprintf(path, sizeof(path), "%s/%s", pkgpath, pkg_basename);
        
        s = alloca(len + 1);
        memcpy(s, path, len);
        s[len] = '\0';
        n_array_push(urls, s);
    }
    
    if (sigint_reached())
        goto l_end;
    
    /* files must be copied if they are taken from more than
       one removable media;
       
       Not so nice, but it works
    */
    if (ncdroms > 1) 
        putenv("POLDEK_VFJUGGLE_CPMODE=copy");
    
    else if (ncdroms == 1) 
        putenv("POLDEK_VFJUGGLE_CPMODE=link");

    
    for (i=0; i < n_array_size(urls_arr); i++) {
        char path[PATH_MAX];
        const char *real_destdir, *pkgdir_name;
        char *pkgpath = n_array_nth(urls_arr, i);

        if (sigint_reached())
            break;

        urls = n_hash_get(urls_h, pkgpath);
        real_destdir = destdir;
        if (nosubdirs == 0) {
            char buf[1024];
            
            vf_url_as_dirpath(buf, sizeof(buf), pkgpath);
            snprintf(path, sizeof(path), "%s/%s", destdir, buf);
            real_destdir = path;
        }

        pkgdir_name = n_hash_get(pkgdir_labels_h, pkgpath);
        if (!vf_fetcha_sl(urls, real_destdir, pkgdir_name))
            nerr++;
        
        else {
            int j;
                
            for (j=0; j < n_array_size(urls); j++) {
                char localpath[PATH_MAX];
                snprintf(localpath, sizeof(localpath), "%s/%s", real_destdir,
                         n_basenam(n_array_nth(urls, j)));
                if (!package_verify_sign(localpath, PKGVERIFY_MD)) {
                    logn(LOGERR, _("%s: MD5 signature verification failed"),
                         n_basenam(localpath));
                    nerr++;
                }
            }
        }
    }

 l_end:
    if (sigint_reached())
        nerr++;

    n_array_free(urls_arr);
    n_hash_free(urls_h);
    return nerr == 0;
}


int packages_dump(tn_array *pkgs, const char *path, int fqfn)
{
    int i;
    FILE *stream = stdout;

    if (path) {
        if ((stream = fopen(path, "w")) == NULL) {
            logn(LOGERR, "fopen %s: %m", path);
            return 0;
        }
        fprintf(stream, "# Packages to install (in the right order)\n");
    }
    
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        if (pkg_is_marked(pkg)) {
            if (fqfn)
                fprintf(stream, "%s\n", pkg_filename_s(pkg));
            else
                fprintf(stream, "%s\n", pkg->name);
        }
    }
    
    if (stream != stdout)
        fclose(stream);
    
    return 1;
}
