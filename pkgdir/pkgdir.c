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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>
#include <trurl/nmalloc.h>
#include <trurl/n_snprintf.h>

#include <vfile/vfile.h>

#define PKGDIR_INTERNAL
#define PKGDIR_MODULE

#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pkgdir.h"
#include "pkg.h"
#include "capreq.h"
#include "pkgroup.h"

static
tn_hash *pkgdir_strip_langs(struct pkgdir *pkgdir)
{
    int      i;
    tn_hash  *avlh = NULL;
    
    if (pkgdir->lc_lang == NULL || pkgdir->langs == NULL)
        return NULL;

    avlh = n_hash_dup(pkgdir->avlangs_h, (tn_fn_dup)n_strdup);
    n_hash_clean(avlh);

    for (i=0;  i < n_array_size(pkgdir->langs); i++) {
        const char *lang = n_array_nth(pkgdir->langs, i);
        n_hash_insert(avlh, lang, NULL);
    }
    
    return avlh;
}


void pkgdir_setup_langs(struct pkgdir *pkgdir)
{
    tn_array *avlangs;

#if 0    
    if (pkgdir->avlangs) {
        iny i;
        
        pkgdir->avlangs_h = n_hash_new(21, NULL);
        for (i=0; i < n_array_size(pkgdir->avlangs); i++) {
            n_hash_insert(pkgdir->avlangs_h,
                          n_array_nth(pkgdir->avlangs, i), NULL);
        }
    }
#endif
    
    //printf("pkgdir_setup_langs %s, %s\n", pkgdir->idxpath, pkgdir->lc_lang);
    if (pkgdir->lc_lang == NULL)
        return;

    if (pkgdir->langs != NULL || pkgdir->avlangs_h == NULL)
        return;

    avlangs = n_hash_keys(pkgdir->avlangs_h);
    n_array_sort(avlangs);
    n_assert(pkgdir->langs == NULL);
    pkgdir->langs = lc_lang_select(avlangs, pkgdir->lc_lang);
    n_array_free(avlangs);
#if 0
    {
        int i;
        for (i=0;  i<n_array_size(pkgdir->avlangs); i++) {
            printf("lav %s\n", n_array_nth(pkgdir->avlangs, i));
        }
    
        for (i=0;  i<n_array_size(pkgdir->langs); i++) {
            printf("l %s\n", n_array_nth(pkgdir->langs, i));
        }
    }
#endif    
    
}


char *pkgdir_setup_pkgprefix(const char *path) 
{
    char *dn = NULL, *bn, *buf, *rpath = NULL;
    int len;

    len = strlen(path);
    buf = alloca(len + 1);
    memcpy(buf, path, len);
    buf[len] = '\0';
    
    n_basedirnam(buf, &dn, &bn);
    if (dn)
        rpath = n_strdup(dn);
    else
        rpath = n_strdup(".");

    return rpath;
}

#if 0
static char *eat_zlib_ext(char *path) 
{
    char *p;
    
    if ((p = strrchr(n_basenam(path), '.')) != NULL) 
        if (strcmp(p, ".gz") == 0)
            *p = '\0';

    return path;
}
#endif

int pkgdir_make_idx_url(char *durl, int size,
                        const char *url, const char *filename)
{
    int n;
    
    if (url[strlen(url) - 1] != '/')
        n = n_snprintf(durl, size, "%s", url);
        
    else
        n = n_snprintf(durl, size, "%s%s", url, filename);

    return n;
}


#if 0
void pkgs_dump(tn_array *pkgs, const char *hdr) 
{
    int i;

    fprintf(stderr, "\nDUMP %d %s\n", n_array_size(pkgs), hdr);
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        fprintf(stderr, "P %s\n", pkg_snprintf_s(pkg));
        n_assert((int)pkg->reqs != 2);
    }
}
#endif

struct pkgdir *pkgdir_malloc(void)
{
    struct pkgdir *pkgdir;

    pkgdir = n_malloc(sizeof(*pkgdir));
    memset(pkgdir, 0, sizeof(*pkgdir));

    pkgdir->name = NULL;
    pkgdir->path = NULL;
    pkgdir->idxpath = NULL;
    pkgdir->pkgs = NULL;
    pkgdir->pri = 0;
    
    pkgdir->depdirs = NULL;
    pkgdir->foreign_depdirs = NULL;
    
    pkgdir->pkgroups = NULL;
    pkgdir->flags = 0;
    pkgdir->ts = 0;

    pkgdir->removed_pkgs = NULL;
    pkgdir->ts_orig = 0;

    //pkgdir->avlangs = NULL;
    pkgdir->langs = NULL;
    pkgdir->avlangs_h = NULL;
    pkgdir->lc_lang = NULL;
    pkgdir->mod = pkgdir->mod_data = NULL;

    return pkgdir;
}


static
const struct pkgdir_module *find_module(const char *type)
{
    const struct pkgdir_module  *mod;
    
    if ((mod = pkgdir_mod_find(type)) == NULL) {
        logn(LOGERR, _("%s: unknown index type"), type);
        return NULL;
    }

    return mod;
}


int pkgdir_update_a(const struct source *src)
{
	const struct pkgdir_module  *mod;
	char                        idxpath[PATH_MAX];
    const char                  *path = NULL;
    int                         rc;
    

    n_assert(src->path);
    
	if ((mod = find_module(src->type)) == NULL)
		return 0;

	if (mod->update == NULL) {
		logn(LOGERR, _("%s: this type of source is not updateable"), src->type);
		return 0;
	}

    if (mod->idx_filename) {
        struct source *tmp;
        pkgdir_make_idx_url(idxpath, sizeof(idxpath), src->path, mod->idx_filename);
        path = src->path;
        tmp = (struct source*)src;
        tmp->path = idxpath;
    }

    rc = mod->update_a(src);

    if (mod->idx_filename) {
        struct source *tmp;
        tmp = (struct source*)src;
        tmp->path = (char*)path;
    }
    
    return rc;
}


int pkgdir_update(struct pkgdir *pkgdir, int *npatches)
{
	int is_updated = 0, rc = 0;
    
    if (npatches)
        *npatches = 0;
    
	if (pkgdir->mod->update == NULL)
		return 0;

	rc = pkgdir->mod->update(pkgdir, &is_updated);
	if (rc && is_updated)
		rc = pkgdir_create_idx(pkgdir, pkgdir->type, NULL, 0);
	
	return rc;
}


int pkgdir_type_info(const char *type) 
{
    const struct pkgdir_module  *mod;
    
    if ((mod = find_module(type)) == NULL)
        return -1;

    return mod->cap_flags;
}


const char *pkgdir_type_idxfn(const char *type) 
{
    const struct pkgdir_module  *mod;
    
    if ((mod = find_module(type)) == NULL)
        return NULL;

    return mod->idx_filename;
}


int pkgdir_type_make_idxpath(const char *type, char *path, size_t size,
                             const char *url) 
{
    const struct pkgdir_module  *mod;
    
    if ((mod = find_module(type)) == NULL)
        return 0;
    
    return pkgdir_make_idx_url(path, size, url, mod->idx_filename);
}


struct pkgdir *pkgdir_srcopen(const struct source *src, unsigned flags)
{
    struct pkgdir *pkgdir;

    pkgdir = pkgdir_open_ext(src->path, src->pkg_prefix, src->type,
                             src->name, flags, src->lc_lang);
    if (pkgdir == NULL)
        return NULL;
    
    if (src->flags & (PKGSOURCE_VRFY_GPG | PKGSOURCE_VRFY_SIGN))
        pkgdir->flags |= PKGDIR_VRFY_GPG;
    
    if (src->flags & PKGSOURCE_VRFY_PGP)
        pkgdir->flags |= PKGDIR_VRFY_PGP;
    
    pkgdir->pri = src->pri;
    return pkgdir;
}

struct pkgdir *pkgdir_open(const char *path, const char *pkg_prefix,
                           const char *type, const char *name)
{
    return pkgdir_open_ext(path, pkg_prefix, type, name, 0, NULL);
}


struct pkgdir *pkgdir_open_ext(const char *path, const char *pkg_prefix,
                               const char *type, const char *name,
                               unsigned flags, const char *lc_lang)

{
    char                        *idx_filename;
    char                        idx_path[PATH_MAX];
    struct pkgdir               *pkgdir;
    const struct pkgdir_module  *mod;
    int                         rc;
    unsigned                    saved_flags;

    n_assert(type);
    if ((mod = find_module(type)) == NULL)
        return NULL;
    
    idx_filename = mod->idx_filename;

    pkgdir = pkgdir_malloc();
    pkgdir->name = n_strdup(name);

    if (name && strcmp(name, "-") != 0)
        pkgdir->flags |= PKGDIR_NAMED;

    
    pkgdir_make_idx_url(idx_path, sizeof(idx_path), path, idx_filename);

    if (pkg_prefix) 
        pkgdir->path = n_strdup(pkg_prefix);

    else if ((mod->cap_flags & PKGDIR_CAP_NOPREFIX) == 0)
        pkgdir->path = pkgdir_setup_pkgprefix(idx_path);

    else
        pkgdir->path = n_strdup(idx_path);

    
    pkgdir->idxpath = n_strdup(idx_path);
    pkgdir->pkgs = pkgs_array_new(2048);
    pkgdir->mod = mod;
    pkgdir->type = mod->name;   /* just reference */

    if (lc_lang)
        pkgdir->lc_lang = n_strdup(lc_lang);

    pkgdir->avlangs_h = n_hash_new(41, free);
    //pkgdir->langs = n_array_new(2, free, NULL);
    rc = 1;

    saved_flags = pkgdir->flags;
    if (mod->open) {
        if (!mod->open(pkgdir, flags)) {
            pkgdir_free(pkgdir);
            return NULL;
        }
    }
    n_assert((pkgdir->flags & saved_flags) == saved_flags);
    if (pkgdir->langs && n_array_size(pkgdir->langs) == 0) {
        n_array_free(pkgdir->langs);
        pkgdir->langs = NULL;
    }
    
            
    if (pkgdir->depdirs) {
        n_array_ctl(pkgdir->depdirs, TN_ARRAY_AUTOSORTED);
        n_array_sort(pkgdir->depdirs);
    }

    pkgdir->flags |= flags;
    return pkgdir;
}


void pkgdir_free(struct pkgdir *pkgdir) 
{
    if (pkgdir->name) {
        free(pkgdir->name);
        pkgdir->name = NULL;
    }
    
    if (pkgdir->path) {
        free(pkgdir->path);
        pkgdir->path = NULL;
    }

    if (pkgdir->idxpath) {
        free(pkgdir->idxpath);
        pkgdir->idxpath = NULL;
    }

    if (pkgdir->depdirs) {
        n_array_free(pkgdir->depdirs);
        pkgdir->depdirs = NULL;
    }

    if (pkgdir->foreign_depdirs) {
        n_array_free(pkgdir->foreign_depdirs);
        pkgdir->foreign_depdirs = NULL;
    }

    if (pkgdir->pkgs) {
        int i;
        
        for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
            pkg->pkgdir = NULL;
        }
        n_array_free(pkgdir->pkgs);
        pkgdir->pkgs = NULL;
    }

    if (pkgdir->pkgroups) {
        pkgroup_idx_free(pkgdir->pkgroups);
        pkgdir->pkgroups = NULL;
    }
    
    if (pkgdir->avlangs_h) {
        n_hash_free(pkgdir->avlangs_h);
        pkgdir->avlangs_h = NULL;
    }

    if (pkgdir->langs) {
        n_array_free(pkgdir->langs);
        pkgdir->langs = NULL;
    }

    if (pkgdir->lc_lang) {
        free(pkgdir->lc_lang);
        pkgdir->lc_lang = NULL;
    }

    pkgdir->flags = 0;
    free(pkgdir);
}


int pkgdir_load(struct pkgdir *pkgdir, tn_array *depdirs, unsigned ldflags)
{
    tn_array *foreign_depdirs = NULL;
    int rc;

    
    if ((ldflags & PKGDIR_LD_FULLFLIST) == 0 && depdirs && pkgdir->depdirs) {
        int i;
        foreign_depdirs = n_array_new(16, NULL, (tn_fn_cmp)strcmp);
        
        for (i=0; i < n_array_size(depdirs); i++) {
            char *dn = n_array_nth(depdirs, i);
            if (n_array_bsearch(pkgdir->depdirs, dn) == NULL) {
                DBGMSG_F("ONLYDIR for %s: %s\n", pkgdir->path, dn);
                if (*dn == '/' && *(dn + 1) != '\0') 
                    dn++;
                n_array_push(foreign_depdirs, dn);
            }
        }
        
        if (n_array_size(foreign_depdirs) == 0) {
            n_array_free(foreign_depdirs);
            foreign_depdirs = NULL;
        }
    }
    
    pkgdir->foreign_depdirs = foreign_depdirs;

    msgn(1, _("Loading %s..."), pkgdir_idstr(pkgdir));
    rc = 0;
    if (pkgdir->mod->load(pkgdir, ldflags) >= 0) {
        int i;

        rc = 1;
        pkgdir->flags |= PKGDIR_LOADED;

        n_array_sort(pkgdir->pkgs);
        
        if ((ldflags & PKGDIR_LD_NOUNIQ) == 0)
            pkgdir_uniq(pkgdir);

        for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
            pkg->pkgdir = pkgdir;
        }
        
        if (pkgdir->depdirs == NULL)
            pkgdir_setup_depdirs(pkgdir);

        pkgdir_setup_langs(pkgdir);
    }
    
    msgn(2, ngettext("%d package loaded",
                     "%d packages loaded", n_array_size(pkgdir->pkgs)),
         n_array_size(pkgdir->pkgs));
    
    if (pkgdir->ts == 0)
		pkgdir->ts = time(0);
    
    return rc;
}


int pkgdir_uniq(struct pkgdir *pkgdir) 
{
    int n = 0;

    pkgdir->flags |= PKGDIR_UNIQED;
    
    if (pkgdir->pkgs == NULL || n_array_size(pkgdir->pkgs) == 0)
        return 0;

    n = n_array_size(pkgdir->pkgs);
    n_array_isort_ex(pkgdir->pkgs, (tn_fn_cmp)pkg_deepcmp_name_evr_rev_verify);
    n_array_uniq_ex(pkgdir->pkgs, (tn_fn_cmp)pkg_cmp_uniq);
    n -= n_array_size(pkgdir->pkgs);
    
    if (n) {
        char m[1024], *name;
        
        snprintf(m, sizeof(m), ngettext("removed %d duplicate package",
                                        "removed %d duplicate packages", n), n);
        
        name = pkgdir->idxpath ? pkgdir->idxpath :
            pkgdir->path ? pkgdir->idxpath : pkgdir->name;
        
        if (name)
            logn(LOGWARN, "%s: %s", name, m);
        else 
            logn(LOGWARN, "%s", m);
    }
    return n;
}

int pkgdir_isremote(struct pkgdir *pkgdir)
{
    return vf_url_type(pkgdir->path) & VFURL_REMOTE;
}


static char *std_depdirs[] = { "bin", "etc", "lib", "sbin", "usr/X11R6/bin",
                               "usr/bin", "usr/lib", "usr/sbin", NULL };


static void is_depdir_req(const struct capreq *req, tn_array *depdirs) 
{
    if (capreq_is_file(req)) {
        const char *reqname;
        char *p;
        int reqlen;
        
        reqname = capreq_name(req);
        reqlen = strlen(reqname);
        
        p = strrchr(reqname, '/');
        
        if (p != reqname) {
            char *dirname;
            int len;

            len = p - reqname;
            dirname = alloca(len + 1);
            memcpy(dirname, reqname, len);
            dirname[len] = '\0';
            p = dirname;

            
        } else if (*(p+1) != '\0') {
            char *dirname;
            dirname = alloca(reqlen + 1);
            memcpy(dirname, reqname, reqlen + 1);
            p = dirname;
        }

        if (*(p+1) != '\0' && *p == '/')
            p++;
        
        if (n_array_bsearch(depdirs, p) == NULL) {
            n_array_push(depdirs, n_strdup(p));
            n_array_sort(depdirs);
        }
    }
}


void pkgdir_setup_depdirs(struct pkgdir *pkgdir) 
{
    int i;

    n_assert(pkgdir->depdirs == NULL);
    pkgdir->depdirs = n_array_new(16, free, (tn_fn_cmp)strcmp);
    n_array_ctl(pkgdir->depdirs, TN_ARRAY_AUTOSORTED);

    i = 0;
    while (std_depdirs[i] != NULL)
        n_array_push(pkgdir->depdirs, n_strdup(std_depdirs[i++]));

    n_array_sort(pkgdir->depdirs);
    for (i=0; i<n_array_size(pkgdir->pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);

        if (pkg->reqs) 
            n_array_map_arg(pkg->reqs, (tn_fn_map2) is_depdir_req,
                            pkgdir->depdirs);
    }
}

static 
int do_create(struct pkgdir *pkgdir, const char *type,
              const char *path, unsigned flags) 
{
    const struct pkgdir_module  *mod, *orig_mod;
    void  *mod_data;
    int rc;

    orig_mod = NULL;
    mod_data = NULL;
    
    if (!pkgdir_is_type(pkgdir, type)) {
        if ((mod = find_module(type)) == NULL)
            return 0;
    
        orig_mod = pkgdir->mod;
        mod_data = pkgdir->mod_data;
        pkgdir->mod = mod;
        pkgdir->mod_data = NULL;
    }
    
    rc = pkgdir->mod->create(pkgdir, path, flags);
    
    if (orig_mod) {
        pkgdir->mod = orig_mod;
        pkgdir->mod_data = mod_data;
    }
    
    return rc;
}


int pkgdir_save(struct pkgdir *pkgdir, const char *type,
                const char *path, unsigned flags)
{
    struct pkgdir               *orig, *diff;
	const struct pkgdir_module  *mod;
    int                         nerr = 0;
    tn_hash                     *avlh, *avlh_tmp;
	
    if (type != NULL && strcmp(type, pkgdir->type) != 0) 
		mod = find_module(type);
	else 
 		mod = pkgdir->mod;
	
	if (mod == NULL)
		return 0;

    avlh = avlh_tmp = NULL;
    if (flags & PKGDIR_CREAT_MINi18n) {
        n_assert(flags & PKGDIR_CREAT_NOPATCH);
        if ((avlh = pkgdir_strip_langs(pkgdir))) {
            avlh_tmp = pkgdir->avlangs_h;
            pkgdir->avlangs_h = avlh;
        }
    }
    

    if ((flags & PKGDIR_CREAT_NOUNIQ) == 0 &&
        (pkgdir->flags & (PKGDIR_DIFF | PKGDIR_UNIQED)) == 0) {
        n_assert(0);
        pkgdir_uniq(pkgdir);
    }
	
    if ((flags & PKGDIR_CREAT_NOPATCH) ||
        (mod->cap_flags & PKGDIR_CAP_UPDATEABLE_INC) == 0 ||
        access(path, R_OK) != 0) {
        
        if (!do_create(pkgdir, type, path, flags))
            nerr++;
        goto l_end;
    }
    
    msgn(1, _("Loading previous %s..."), vf_url_slim_s(path, 0));
    orig = pkgdir_open(path, NULL, type, "");
    if (orig != NULL && pkgdir_load(orig, NULL, 0) > 0) {
        if (orig->ts > pkgdir->ts) {
            logn(LOGWARN, _("clock skew detected; create index with fake "
                            "timestamp %lu %lu"), orig->ts, pkgdir->ts);
        }
            
        if (orig->ts >= pkgdir->ts) 
            pkgdir->ts = orig->ts + 1;
        
        if ((diff = pkgdir_diff(orig, pkgdir))) {
            diff->ts = pkgdir->ts;
            if (!do_create(diff, type, path, flags))
                nerr++;
        }
    }

    if (nerr == 0 && !do_create(pkgdir, type, path, flags))
        nerr++;

    
 l_end:
    if (avlh_tmp) {
        pkgdir->avlangs_h = avlh_tmp;
        n_hash_free(avlh);
    }
    
    return nerr == 0;
}


