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
#include "pm/pm.h"


unsigned pkg_get_verify_signflags(struct pkg *pkg) 
{
    unsigned verify_flags = 0;
    if (pkg->pkgdir && (pkg->pkgdir->flags & PKGDIR_VRFYSIGN)) {
        if (pkg->pkgdir->flags & PKGDIR_VRFY_GPG)
            verify_flags |= PKGVERIFY_GPG;
        
        if (pkg->pkgdir->flags & PKGDIR_VRFY_PGP)
            verify_flags |= PKGVERIFY_PGP;
    }

    return verify_flags;
}

void packages_fetch_summary(struct pm_ctx *pmctx,
                            tn_array *pkgs, const char *destdir, int nosubdirs)
{
    long bytesget = 0, bytesdownload = 0, bytesused = 0;
    int i;

    n_assert(nosubdirs == 0); /* not implemented */
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg  *pkg = n_array_nth(pkgs, i);
        char        path[PATH_MAX];
        
        if (sigint_reached())
            break;
        
        bytesget += pkg->fsize;
        bytesused += pkg->size;
        if (pkg->pkgdir && (vf_url_type(pkg->pkgdir->path) & VFURL_REMOTE)) {
            if (pkg_localpath(pkg, path, sizeof(path), destdir)) {
                if (access(path, R_OK) != 0) {
                    bytesdownload += pkg->fsize;
                    
                } else {
                    if (!pm_verify_signature(pmctx, path, PKGVERIFY_MD)) {
                        vf_unlink(path);
                        bytesdownload += pkg->fsize;
                    }
                }
            }
        }
    }
    if (bytesget) {
        char buf[64];
        n_assert(bytesget);
        
        snprintf_size(buf, sizeof(buf), bytesget, 0, 1);
        msg(1, _("Need to get %s of archives"), buf);
        if (bytesdownload == 0)
            msg(1, _("_."));
        else {
            snprintf_size(buf, sizeof(buf), bytesdownload, 0, 1);
            msg(1, _("_, %s of them need to be downloaded.\n"), buf);
        }
        
        if (bytesused) {
            char buf[64];
            snprintf_size(buf, sizeof(buf), bytesused, 0, 1);
            msg(1, _("After unpacking about %s will be used."), buf);
        }
    }
    
    msg(1, "_\n");
}

int packages_fetch(struct pm_ctx *pmctx,
                   tn_array *pkgs, const char *destdir, int nosubdirs)
{
    int       i, nerr, urltype, ncdroms;
    tn_array  *urls = NULL, *packages = NULL;
    tn_array  *urls_arr = NULL;
    tn_hash   *urls_h, *pkgs_h = NULL;
    tn_hash   *pkgdir_labels_h = NULL;

    n_assert(destdir);
    urls_h = n_hash_new(21, (tn_fn_free)n_array_free);
    pkgs_h = n_hash_new(21, (tn_fn_free)n_array_free);
    pkgdir_labels_h = n_hash_new(21, NULL);
    n_hash_ctl(urls_h, TN_HASH_NOCPKEY);
    n_hash_ctl(pkgs_h, TN_HASH_NOCPKEY);
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
            snprintf(path, sizeof(path), "%s/%s", pkgpath, pkg_basename);
            if (access(path, R_OK) != 0) {
                logn(LOGERR, "%s: %m", path);
                nerr++;
                
            } else {
                if (!pm_verify_signature(pmctx, path, PKGVERIFY_MD)) {
                    logn(LOGERR, _("%s: MD5 signature verification failed"),
                         n_basenam(path));
                    nerr++;
                }
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
            int pkg_ok;
            
            pkg_ok = pm_verify_signature(pmctx, path, PKGVERIFY_MD);
            if (pkg_ok)         /* we got it  */
                continue;
            else 
                vf_unlink(path);
        }
        
        if ((urls = n_hash_get(urls_h, pkgpath)) == NULL) {
            urls = n_array_new(n_array_size(pkgs), NULL, NULL);
            n_hash_insert(urls_h, pkgpath, urls);

            packages = n_array_new(n_array_size(pkgs), NULL, NULL);
            n_hash_insert(pkgs_h, pkgpath, packages);
            
            n_array_push(urls_arr, pkgpath);
            n_hash_insert(pkgdir_labels_h, pkgpath, pkg->pkgdir->name);
        }
        
        len = n_snprintf(path, sizeof(path), "%s/%s", pkgpath, pkg_basename);
        
        s = alloca(len + 1);
        memcpy(s, path, len);
        s[len] = '\0';
        n_array_push(urls, s);
        n_array_push(packages, pkg);
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
        packages = n_hash_get(pkgs_h, pkgpath);
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
                
                if (!pm_verify_signature(pmctx, localpath, PKGVERIFY_MD)) {
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
    n_hash_free(pkgs_h);
    n_hash_free(pkgdir_labels_h);
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
        if (fqfn)
            fprintf(stream, "%s\n", pkg_filename_s(pkg));
        else
            fprintf(stream, "%s\n", pkg->name);
    }
    
    if (stream != stdout)
        fclose(stream);
    
    return 1;
}
