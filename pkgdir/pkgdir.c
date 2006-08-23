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
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>
#include <trurl/nmalloc.h>
#include <trurl/n_snprintf.h>

#include <vfile/vfile.h>

#define ENABLE_TRACE 0
#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pkgdir.h"
#include "pkgdir_intern.h"
#include "pkg.h"
#include "capreq.h"
#include "pkgroup.h"
#include "pkgmisc.h"
#include "pkgdir_dirindex.h"

tn_hash *pkgdir__avlangs_new(void)
{
    tn_hash  *avlh = n_hash_new(16, free);
    n_hash_ctl(avlh, TN_HASH_NOCPKEY);
    return avlh;
}

static tn_hash *pkgdir__strip_langs(struct pkgdir *pkgdir)
{
    int      i;
    tn_hash  *avlh = NULL;
    
    if (pkgdir->lc_lang == NULL || pkgdir->langs == NULL)
        return NULL;

    avlh = n_hash_new(16, NULL); /* no free - will fed with avlangs_h items */

    for (i=0;  i < n_array_size(pkgdir->langs); i++) {
        const char *lang = n_array_nth(pkgdir->langs, i);
        struct pkgdir_avlang *avl = n_hash_get(pkgdir->avlangs_h, lang);
        n_assert(avl);
        n_hash_insert(avlh, avl->lang, avl);
    }
    return avlh;
}

void pkgdir__update_avlangs(struct pkgdir *pkgdir, const char *lang, int count)
{
    struct pkgdir_avlang *avl;
    int len = strlen(lang) + 1;

    if ((avl = n_hash_get(pkgdir->avlangs_h, lang))) {
        avl->count += count;
        
    } else {
        avl = n_malloc(sizeof(*avl) + len);
        avl->count = count;
        memcpy(avl->lang, lang, len);
        n_hash_insert(pkgdir->avlangs_h, avl->lang, avl);
    }
}

/* fills pkgdir->langs with preferred languages, more preferred lang first */
void pkgdir__setup_langs(struct pkgdir *pkgdir)
{
    tn_array *avlangs;

    DBGF("pkgdir__setup_langs %s, %s\n", pkgdir->idxpath, pkgdir->lc_lang);
    if (pkgdir->lc_lang == NULL)
        return;

    if (pkgdir->langs != NULL || pkgdir->avlangs_h == NULL)
        return;

    avlangs = n_hash_keys(pkgdir->avlangs_h);
    n_array_sort(avlangs);

    n_assert(pkgdir->langs == NULL);
    pkgdir->langs = lc_lang_select(avlangs, pkgdir->lc_lang);
    

#if ENABLE_TRACE
    {
        int i;
        for (i=0;  i<n_array_size(avlangs); i++) {
            DBGF("av_lang %s\n", n_array_nth(avlangs, i));
        }
        
        if (pkgdir->langs)
            for (i=0;  i<n_array_size(pkgdir->langs); i++) {
                DBGF("lang %s\n", n_array_nth(pkgdir->langs, i));
            }
    }
#endif

    n_array_free(avlangs);
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

static
int make_idxpath(char *dpath, int size, const char *type,
                 const char *path, const char *fn, const char *ext)
{
    int n;
    char *endp;

    n_assert(*path);
    endp = strrchr(path, '\0') - 1;

    if (*endp != '/')
        n = n_snprintf(dpath, size, "%s", path);
        
    else {
        if (fn == NULL) {
            n_assert(type);
            if ((fn = pkgdir_type_default_idxfn(type)) == NULL)
                return 0;
        }
        
        if (ext == NULL)
            ext = pkgdir_type_default_compr(type);
        else if (strcmp(ext, "none") == 0)
            ext = NULL;
            
        n = n_snprintf(dpath, size, "%s%s%s%s%s", path,
                       *endp != '/' ? "/" : "",
                       fn, ext ? "." : "", ext ? ext : "");
    }
    DBGF("%s -> %d, %s\n", path, n, dpath);
    return n;
}

static
char *do_pkgdir__make_idxpath(char *dpath, int dsize, const char *path,
                              const char *type, const char *compress,
                              const char **fnptr)
{
    const char *fn;
    int n;

    if (fnptr)
        *fnptr = NULL;
    
    if ((fn = pkgdir_type_default_idxfn(type)) != NULL) {
        if (fnptr)
            *fnptr = fn;
        
        n = make_idxpath(dpath, dsize, type, path, fn, compress);
        if (n > 0)
            return dpath;
    }
    
    n_snprintf(dpath, dsize, "%s", path);
    return dpath;
}

char *pkgdir__make_idxpath(char *dpath, int dsize, const char *path,
                           const char *type, const char *compress)
{
    return do_pkgdir__make_idxpath(dpath, dsize, path, type, compress, NULL);
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

    pkgdir = n_calloc(sizeof(*pkgdir), 1);

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
    pkgdir->orig_ts = 0;
    pkgdir->orig_idxpath = NULL;

    //pkgdir->avlangs = NULL;
    pkgdir->langs = NULL;
    pkgdir->avlangs_h = NULL;
    pkgdir->lc_lang = NULL;
    pkgdir->mod = pkgdir->mod_data = NULL;
    pkgdir->na = n_alloc_new(128, TN_ALLOC_OBSTACK);
    pkgdir->dirindex = NULL;
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
    enum pkgdir_uprc            uprc = PKGDIR_UPRC_NIL;
    char idxpath[PATH_MAX];
    int  rc;
    
    n_assert(src->path);
	if ((mod = find_module(src->type)) == NULL)
		return 0;

	if (mod->update_a == NULL) {
		logn(LOGERR, _("%s: this type of source is not updateable"), src->type);
		return 0;
	}

    pkgdir__make_idxpath(idxpath, sizeof(idxpath), src->path, src->type,
                         src->compress);
    rc = mod->update_a(src, idxpath, &uprc);
    
    if (rc && uprc == PKGDIR_UPRC_UPTODATE) 
        msgn(1, _("%s is up to date"), source_idstr(src));
    return rc;
}


int pkgdir_update(struct pkgdir *pkgdir)
{
	int rc = 0;
    enum pkgdir_uprc uprc = PKGDIR_UPRC_NIL;
    
	if (pkgdir->mod->update == NULL)
		return 0;

	rc = pkgdir->mod->update(pkgdir, &uprc);
    if (rc) {
        if (uprc == PKGDIR_UPRC_UPTODATE) 
            msgn(1, _("%s is up to date"), pkgdir_idstr(pkgdir));
            
        else if (uprc == PKGDIR_UPRC_UPDATED &&
                 (pkgdir->mod->cap_flags & PKGDIR_CAP_NOSAVAFTUP) == 0) {

            if (pkgdir->mod->cap_flags & PKGDIR_CAP_SAVEABLE)
                rc = pkgdir_save(pkgdir, PKGDIR_CREAT_NOPATCH);
        }
            
    } else if (!rc && uprc == PKGDIR_UPRC_ERR_DESYNCHRONIZED) {
        if (pkgdir->src && (pkgdir->src->flags & PKGSOURCE_AUTOUPA)) {
            msgn(0, _("%s: update failed, trying to update whole index..."),
                 pkgdir_idstr(pkgdir));
            rc = pkgdir_update_a(pkgdir->src);
        } else {
            logn(LOGWARN, _("%s: desynchronized index, try --upa"),
                 pkgdir_idstr(pkgdir));
        }
    }
	
	return rc;
}


int pkgdir_type_info(const char *type) 
{
    const struct pkgdir_module  *mod;
    
    if ((mod = find_module(type)) == NULL)
        return -1;

    return mod->cap_flags;
}


const char *pkgdir_type_default_idxfn(const char *type) 
{
    const struct pkgdir_module  *mod;
    
    if ((mod = find_module(type)) == NULL)
        return NULL;

    return mod->default_fn;
}

const char *pkgdir_type_default_compr(const char *type) 
{
    const struct pkgdir_module  *mod;
    
    if ((mod = find_module(type)) == NULL)
        return NULL;

    return mod->default_compr;
}



struct pkgdir *pkgdir_srcopen(const struct source *src, unsigned flags)
{
    struct pkgdir *pkgdir;

    if (src->flags & PKGSOURCE_NODESC)
        flags |= PKGDIR_OPEN_NODESC;

    pkgdir = pkgdir_open_ext(src->path, src->pkg_prefix,
                             src->type, src->name,
                             src->compress,
                             flags, src->lc_lang);
    if (pkgdir == NULL)
        return NULL;
    
    if (src->flags & (PKGSOURCE_VRFY_GPG | PKGSOURCE_VRFY_SIGN))
        pkgdir->flags |= PKGDIR_VRFY_GPG;
    
    if (src->flags & PKGSOURCE_VRFY_PGP)
        pkgdir->flags |= PKGDIR_VRFY_PGP;
    
    pkgdir->pri = src->pri;
    pkgdir->src = source_link((struct source *)src);
    return pkgdir;
}

struct pkgdir *pkgdir_open(const char *path, const char *pkg_prefix,
                           const char *type, const char *name)
{
    return pkgdir_open_ext(path, pkg_prefix, type, name, NULL, 0, NULL);
}


struct pkgdir *pkgdir_open_ext(const char *path, const char *pkg_prefix,
                               const char *type, const char *name,
                               const char *compress, 
                               unsigned flags, const char *lc_lang)

{
    char                        idxpath[PATH_MAX];
    const char                  *fn;
    struct pkgdir               *pkgdir;
    const struct pkgdir_module  *mod;
    int                         rc;
    unsigned                    saved_flags;
    tn_array                    *pkgs;

    n_assert(type);
    if ((mod = find_module(type)) == NULL)
        return NULL;
    
    pkgdir = pkgdir_malloc();
    if (name)
        pkgdir->name = n_strdup(name);

    if (name && n_str_ne(name, "-"))
        pkgdir->flags |= PKGDIR_NAMED;

    DBGF("pkgdir_open_ext[%s] %s, %s%s\n", type, path,
         pkg_prefix ? "prefix = ":"", pkg_prefix ? pkg_prefix : "", pkgdir);

    fn = NULL;
    do_pkgdir__make_idxpath(idxpath, sizeof(idxpath), path, type,
                            compress, &fn);

    /* fn with subdir -> make prefix without it */
    if (fn && strchr(fn, '/') && pkg_prefix == NULL)
        pkg_prefix = path;

    if (pkg_prefix) 
        pkgdir->path = n_strdup(pkg_prefix);

    else if ((mod->cap_flags & PKGDIR_CAP_NOPREFIX) == 0)
        pkgdir->path = pkgdir_setup_pkgprefix(idxpath);

    else
        pkgdir->path = n_strdup(idxpath);
    
    pkgdir->idxpath = n_strdup(idxpath);
    pkgdir->compress = compress ? n_strdup(compress) : NULL;
    pkgdir->pkgs = pkgs = pkgs_array_new_ex(2048, pkg_strcmp_name_evr_rev);
    
    pkgdir->mod = mod;
    pkgdir->type = mod->name;   /* just reference */

    if (lc_lang)
        pkgdir->lc_lang = n_strdup(lc_lang);

    pkgdir->avlangs_h = pkgdir__avlangs_new();
    rc = 1;

    saved_flags = pkgdir->flags;
    if (mod->open) {
        if (!mod->open(pkgdir, flags)) {
            pkgdir_free(pkgdir);
            return NULL;
        }
    }
    n_assert(pkgdir->pkgs == pkgs);
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

    DBGF("%p %s\n", pkgdir,  pkgdir_idstr(pkgdir));
    n_cfree(&pkgdir->name);
    n_cfree(&pkgdir->path);
    n_cfree(&pkgdir->idxpath);
    n_cfree(&pkgdir->orig_idxpath);
    
    if (pkgdir->depdirs) {
        n_array_free(pkgdir->depdirs);
        pkgdir->depdirs = NULL;
    }

    if (pkgdir->foreign_depdirs) {
        n_array_free(pkgdir->foreign_depdirs);
        pkgdir->foreign_depdirs = NULL;
    }

    if (pkgdir->src) {
        source_free(pkgdir->src);
        pkgdir->src = NULL;
    }

    if (pkgdir->pkgs) {
        int i;
        for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
            if (pkg->pkgdir == pkgdir)
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

    if (pkgdir->mod && pkgdir->mod->free)
        pkgdir->mod->free(pkgdir);

    if (pkgdir->na) {
        DBGF("%p %p %d\n", pkgdir->na, &pkgdir->na->_refcnt, pkgdir->na->_refcnt);
        n_alloc_free(pkgdir->na);
    }
    

    if (pkgdir->prev_pkgdir)
        pkgdir_free(pkgdir->prev_pkgdir);

    if (pkgdir->dirindex)
        pkgdir_dirindex_close(pkgdir->dirindex);
    
    memset(pkgdir, 0, sizeof(*pkgdir));
    free(pkgdir);
}

static void do_ignore(struct pkgdir *pkgdir) 
{
    /* module handles "ignore" itself  */
    if (pkgdir->mod->cap_flags & PKGDIR_CAP_HANDLEIGNORE)
        return;

    if (pkgdir->src && n_array_size(pkgdir->src->ign_patterns))
        packages_score_ignore(pkgdir->pkgs, pkgdir->src->ign_patterns, 1);
}

static void do_open_dirindex(struct pkgdir *pkgdir)
{
    /* XXX: a workaround - tndb cannon create empty files */
    if (n_array_size(pkgdir->pkgs) == 0)
        return;
    
    pkgdir->dirindex = pkgdir_dirindex_open(pkgdir);
    
    /* broken or outdated dirindex, shouldn't happen, but...  */
    if (pkgdir->dirindex == NULL) {
        msgn(1, "Rebuilding %s's directory index...", pkgdir_idstr(pkgdir));
        pkgdir_dirindex_create(pkgdir); /* rebuild, open removes index on fail*/
        pkgdir->dirindex = pkgdir_dirindex_open(pkgdir);
    }
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
                DBGF("ONLYDIR for %s: %s\n", pkgdir->path, dn);
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

    if (pkgdir->flags & PKGDIR_DIFF) {
        n_assert((ldflags & PKGDIR_LD_DOIGNORE) == 0);
        
    } else {
        if (poldek_VERBOSE < 2)
            msgn(1, _("Loading [%s]%s..."), pkgdir->type, pkgdir_idstr(pkgdir));
        else
            msgn(2, _("Loading [%s]%s..."), pkgdir->type,
                 vf_url_slim_s(pkgdir->idxpath, 0));
    }
    
    rc = 0;
    if (pkgdir->mod->load(pkgdir, ldflags) >= 0) {
        int i;

        rc = 1;
        pkgdir->flags |= PKGDIR_LOADED;

        if (ldflags & PKGDIR_LD_DIRINDEX)
            pkgdir_dirindex_create(pkgdir);
        
        n_array_sort(pkgdir->pkgs);
        for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgdir->pkgs, i);
            pkg->pkgdir = pkgdir;
        }

        if (ldflags & PKGDIR_LD_DOIGNORE)
            do_ignore(pkgdir);
        
        if ((ldflags & PKGDIR_LD_NOUNIQ) == 0)
            pkgdir__uniq(pkgdir);

        if (pkgdir->depdirs == NULL)
            pkgdir__setup_depdirs(pkgdir);

        pkgdir__setup_langs(pkgdir);
    }
    
    msgn(3, ngettext("%d package loaded",
                     "%d packages loaded", n_array_size(pkgdir->pkgs)),
         n_array_size(pkgdir->pkgs));

    n_assert(pkgdir->ts);       /* ts must be set by backend */
    if (pkgdir->ts == 0)
		pkgdir->ts = time(0);
    
    pkgdir->_ldflags = ldflags;
    
    if (ldflags & PKGDIR_LD_DIRINDEX)
        do_open_dirindex(pkgdir);

    return rc;
}

#if DEVEL
static int ncalls_deepcmp_nevr_rev_verify = 0;
#endif

static
int deepcmp_nevr_rev_verify(const struct pkg *p1, const struct pkg *p2)
{
    register int rc;

#if DEVEL
    if (ncalls_deepcmp_nevr_rev_verify >= 0) {
        ncalls_deepcmp_nevr_rev_verify++;
        if (p1->pkgdir &&
            ncalls_deepcmp_nevr_rev_verify > 10 * n_array_size(p1->pkgdir->pkgs)) {
            logn(LOGNOTICE, "devel: %d: too many compares",
                 ncalls_deepcmp_nevr_rev_verify);
            ncalls_deepcmp_nevr_rev_verify = -1; /* stop it */
        }
    }
#endif    
    if ((rc = pkg_deepcmp_name_evr_rev(p1, p2)) == 0) {
        logn(LOGERR | LOGDIE, "packages %s and %s are equal to me, give up",
             pkg_snprintf_s(p1), pkg_snprintf_s0(p2));
    }
    
    return rc;
}


int pkgdir__uniq(struct pkgdir *pkgdir) 
{
    int n = 0;

    pkgdir->flags |= PKGDIR_UNIQED;
    
    if (pkgdir->pkgs == NULL || n_array_size(pkgdir->pkgs) == 0)
        return 0;

#if DEVEL
    ncalls_deepcmp_nevr_rev_verify = 0;
#endif
    
    n = n_array_size(pkgdir->pkgs);
    n_array_isort_ex(pkgdir->pkgs, (tn_fn_cmp)deepcmp_nevr_rev_verify);
    n_array_uniq_ex(pkgdir->pkgs, (tn_fn_cmp)pkg_cmp_uniq_name_evr_arch);
    n -= n_array_size(pkgdir->pkgs);
    
    if (n) {
        char m[1024];
        const char *name;
        
        snprintf(m, sizeof(m), ngettext("removed %d duplicate package",
                                        "removed %d duplicate packages", n), n);
        
        name = pkgdir_idstr(pkgdir);
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


void pkgdir__setup_depdirs(struct pkgdir *pkgdir) 
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

const char *pkgdir_localidxpath(struct pkgdir *pkgdir)
{
    if (pkgdir->mod->localidxpath)
        return pkgdir->mod->localidxpath(pkgdir);
    return pkgdir->idxpath;
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

    /*
      MESS: pkgdir's mod && mod_data are replaced to saving it as other type.
      The consequence is that module's create() nor pkg's load_pkguinf() and
      load_nodep_fl() must NOT rely on pkgdir->mod_data -- modules should use
      pkg's pkgdir_data to pass its arguments to this functions.
    */
    if (!pkgdir_is_type(pkgdir, type)) {
        if ((mod = find_module(type)) == NULL)
            return 0;
    
        orig_mod = pkgdir->mod;
        mod_data = pkgdir->mod_data;
        pkgdir->mod = mod;
        pkgdir->mod_data = NULL;
    }

    if (pkgdir->mod->create == NULL) {
        logn(LOGERR, _("%s: repository could not be created (missing "
                       "feature)"), type);
        return 0;
    }

    if (pkgdir->ts == 0)
        pkgdir->ts = time(NULL);
    
    rc = pkgdir->mod->create(pkgdir, path, flags);
    
    if (orig_mod) {
        pkgdir->mod = orig_mod;
        pkgdir->mod_data = mod_data;
    }
    
    return rc;
}

int pkgdir_save(struct pkgdir *pkgdir, unsigned flags) 
{
    return pkgdir_save_as(pkgdir, pkgdir->type,
                          pkgdir_localidxpath(pkgdir), flags);
}


static
struct pkgdir *load_orig_pkgdir(struct pkgdir *pkgdir, const char *path,
                                const char *idxpath, const char *type)
{
    struct pkgdir *orig = NULL;
    const char *orig_path;
    char orig_name[64];

    orig_path = path ? path : idxpath ? idxpath : pkgdir->path;
    n_assert(orig_path);
        
    if (access(orig_path, R_OK) == 0) {
        n_snprintf(orig_name, sizeof(orig_name), "previous %s", 
                   vf_url_slim_s(orig_path, 0));

        orig = pkgdir_open_ext(orig_path, pkgdir->path, type,
                               orig_name, NULL, PKGDIR_OPEN_ALLDESC,
                               pkgdir->lc_lang);
    }
        
    if (orig && pkgdir_load(orig, NULL, 0) <= 0) {
        pkgdir_free(orig);
        orig = NULL;
    }
    return orig;
}

    
extern int pdir_pkgdir_uniq(struct pkgdir *pkgdir);

int pkgdir_save_as(struct pkgdir *pkgdir, const char *type,
                   const char *path, unsigned flags)
{
    struct pkgdir               *orig, *diff;
	const struct pkgdir_module  *mod;
    const char                  *idxpath = NULL;
    tn_hash                     *avlangs_h, *avlangs_h_tmp;
    int                         nerr = 0;

    n_assert(pkgdir->idxpath);
    mod = pkgdir->mod;
    if (type) {
        if (!pkgdir_is_type(pkgdir, type))
            mod = find_module(type);
        
    } else {
        type = pkgdir->type;
        if (path == NULL)
            idxpath = pkgdir_localidxpath(pkgdir);
    }

	if (mod == NULL)
		return 0;
    
    avlangs_h = avlangs_h_tmp = NULL;
    
    /* strip langs to current locale settings? */
    if (flags & PKGDIR_CREAT_MINi18n) { 
        n_assert(flags & PKGDIR_CREAT_NOPATCH);
        if ((avlangs_h = pkgdir__strip_langs(pkgdir))) {
            avlangs_h_tmp = pkgdir->avlangs_h;
            pkgdir->avlangs_h = avlangs_h;
        }
    }

    /* sanity check: UNIQ is requested for already uniqued pkgdir */
    if ((flags & PKGDIR_CREAT_NOUNIQ) == 0 &&
        (pkgdir->flags & (PKGDIR_DIFF | PKGDIR_UNIQED)) == 0) {
        n_assert(0);
        pkgdir__uniq(pkgdir);
    }

    orig = NULL;
    if (pkgdir->prev_pkgdir) {  /* already loaded in source.c */
        orig = pkgdir->prev_pkgdir;
        
    } else if ((!pkgdir_isremote(pkgdir)) && 
               (flags & PKGDIR_CREAT_NOPATCH) == 0 && /* nopach requested    */
               (mod->cap_flags & PKGDIR_CAP_UPDATEABLE_INC) && /* non diffaware */
               (idxpath && access(idxpath, R_OK) == 0)) /* exists? */
        orig = load_orig_pkgdir(pkgdir, path, idxpath, type);
    

    n_assert(nerr == 0);

    /* XXX pdir must be uniqued by NEVR (is not MULTILIB-able), if not, it will break
       backward compat with 0.18.x series. */
    if (n_str_eq(type, "pdir"))
        pdir_pkgdir_uniq(pkgdir);
    
    if (orig == NULL) {
        if (!do_create(pkgdir, type, path, flags))
            nerr++;
        
    } else {
        int create = 1;

        if (orig->ts > pkgdir->ts) {
            logn(LOGWARN, _("clock skew detected; create index with fake "
                            "timestamp %lu %lu"), (unsigned long)orig->ts,
                 (unsigned long)pkgdir->ts);
            pkgdir->ts = orig->ts + 1;
        }

        create = 1;
        if ((diff = pkgdir_diff(orig, pkgdir)))
            diff->ts = pkgdir->ts;
        else if ((flags & PKGDIR_CREAT_IFORIGCHANGED))
            create = 0;         /* not a difference -> do not create */

        
        if (create) {           /* save index */
            if (!do_create(pkgdir, type, path, flags))
                nerr++;

        } else {
            msgn(1, _("%s: index not changed, not saved"),
                 vf_url_slim_s(orig->idxpath, 0));
        }
        
        if (diff && (flags & PKGDIR_CREAT_NOPATCH) == 0) { /* save diff? */
            if (!do_create(diff, type, NULL, flags))
                nerr++;
        }
    }
    
    if (orig && orig != pkgdir->prev_pkgdir) {
        pkgdir_free(orig);
        orig = NULL;
    } 
 
    if (avlangs_h_tmp) {
        pkgdir->avlangs_h = avlangs_h_tmp;
        n_hash_free(avlangs_h);
    }
    
    return nerr == 0;
}


int pkgdir_add_package(struct pkgdir *pkgdir, struct pkg *pkg)
{
    if (n_array_bsearch(pkgdir->pkgs, pkg))
        return 0;
    n_array_push(pkgdir->pkgs, pkg_link(pkg));
    pkgdir->flags |= PKGDIR_CHANGED;
    return 1;
}

int pkgdir_remove_package(struct pkgdir *pkgdir, struct pkg *pkg)
{
    int n;
    
    if ((n = n_array_bsearch_idx(pkgdir->pkgs, pkg)) < 0)
        return 0;
    
    n_array_remove_nth(pkgdir->pkgs, n);
    pkgdir->flags |= PKGDIR_CHANGED;
    return 1;
}

        
    
    

