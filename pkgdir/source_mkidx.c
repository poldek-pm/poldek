/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

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

#include <string.h>
#include <limits.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <trurl/nmalloc.h>
#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>
#include <trurl/nhash.h>

#include <vfile/vfile.h>

#define ENABLE_TRACE 0

#include "pkgdir.h"
#include "pkgdir_intern.h"
#include "source.h"
#include "misc.h"
#include "log.h"
#include "poldek_term.h"
#include "i18n.h"
#include "conf.h"
#include "source.h"

static const char *make_idxpath(char *path, int size,
                                struct source *src, const char *type,
                                const char *idxpath, tn_hash *kw)
{
    if (idxpath == NULL) {
        char *s;
        n_assert(src);
        n_strdupap(src->path, &s);
        idxpath = s;
    }

    n_assert(idxpath);
    
    if (!util__isdir(idxpath)) {
        n_snprintf(path, size, "%s", idxpath);
        idxpath = path;

    } else {
        char *compress = kw ? n_hash_get(kw, "compress") : NULL;
        
        if (compress == NULL)
            compress = src ? src->compress : NULL;
        
        
        idxpath = pkgdir__make_idxpath(path, size, idxpath, type,
                                       compress);
    }
    
    n_assert(idxpath);
    return idxpath;
}


static struct pkgdir *load_pkgdir(const struct source *src,
                                  const char *type, const char *idxpath, 
                                  int with_prev)
{
    struct pkgdir   *pkgdir;
    unsigned        ldflags = 0;
    
    pkgdir = pkgdir_srcopen(src, 0);
    if (pkgdir == NULL)
        return 0;

    if (with_prev)
        n_assert(idxpath);
    
    /* load previous index if it exists */
    if (with_prev && source_is_type(src, "dir") && access(idxpath, R_OK) == 0) {
        struct pkgdir *pdir;
        char orig_name[64];

        n_snprintf(orig_name, sizeof(orig_name), "previous %s",
                   vf_url_slim_s(idxpath, 0));

        pdir = pkgdir_open_ext(idxpath,
                               src->pkg_prefix, type,
                               orig_name, NULL, PKGDIR_OPEN_ALLDESC,
                               src->lc_lang);
        
        if (pdir && !pkgdir_load(pdir, NULL, ldflags)) {
            pkgdir_free(pdir);
            pdir = NULL;
        }
        
        if (pdir) {
            n_assert((pdir->_ldflags & PKGDIR_LD_DOIGNORE) == 0);
        }
        pkgdir->prev_pkgdir = pdir;
    }
    
    if (!pkgdir_load(pkgdir, NULL, ldflags)) {
        pkgdir_free(pkgdir);
        pkgdir = NULL;
        
    } else {
        n_assert((pkgdir->_ldflags & PKGDIR_LD_DOIGNORE) == 0);
    }
    
    return pkgdir;
}

static int create_idx(struct pkgdir *pkgdir, const char *type,
                      const char *idxpath, unsigned cr_flags, tn_hash *kw) 
{
    struct source *src = pkgdir->src;
    
    n_assert(type);
    n_assert(src);
    n_assert(idxpath);
    
    if (kw && n_hash_exists(kw, "v018x"))
        cr_flags |= PKGDIR_CREAT_v018x;

    return pkgdir_save_as(pkgdir, type, idxpath, cr_flags);
}

static int do_makeidx(struct source *src,
                      const char *type, const char *idxpath,
                      unsigned cr_flags, tn_hash *kw) 
{
    struct pkgdir   *pkgdir;
    char            path[PATH_MAX];
    int             rc = 0;
    
    n_assert(type);

    idxpath = make_idxpath(path, sizeof(path), src, type, idxpath, kw);
    n_assert(idxpath);
    
#if 0                           /* XXX: disabled  */
    if (0 && source_is_type(src, "dir") && !util__isdir(src->path)) {
        char *tmp, *dn;
        n_strdupap(src->path, &tmp);
        dn = n_dirname(tmp);
        if (util__isdir(dn))
            source_set(&src->path, dn);
    }
#endif
    
    msgn(1, "Creating %s index of %s (type=%s)...", type, src->path, src->type);
    DBGF("mkidx[%s => %s] %s %d\n", src->type, type, src->path, cr_flags);

    if ((pkgdir = load_pkgdir(src, type, idxpath, 1))) {
        n_assert((pkgdir->_ldflags & PKGDIR_LD_DOIGNORE) == 0);
        rc = create_idx(pkgdir, type, idxpath, cr_flags, kw);
    }
    
    if (pkgdir)
        pkgdir_free(pkgdir);
    
    return rc;
}

static const char *determine_stype(struct source *src, const char *idxpath)
{
    if (src->original_type)
        return src->original_type;

    idxpath = idxpath;
    
    /* with type and not named i.e --st TYPE -s PATH */
    if ((src->flags & PKGSOURCE_TYPE) && (src->flags & PKGSOURCE_NAMED) == 0)
        return src->type;
    
    if (util__isdir(src->path))
        return "dir";
    
    else if (src->type)  /* not a dir, an URL */
        return src->type;

    return poldek_conf_PKGDIR_DEFAULT_TYPE;
}

int source_make_idx(struct source *src, const char *stype, 
                    const char *dtype, const char *idxpath,
                    unsigned flags, tn_hash *kw)
{
    struct source *ssrc;
    int typcaps;
    int rc = 0;

    DBGF("%s(src=%s) => %s\n", stype, src->type ? src->type : "null", dtype);

    if (stype == NULL)
        stype = determine_stype(src, idxpath);
    
    if (src->type == NULL)
        source_set_default_type(src);
    n_assert(src->type);

    
    if (dtype == NULL) {
           /* if not from config */
        if ((src->flags & PKGSOURCE_NAMED) == 0) {
            if (n_str_eq(src->type, "dir"))
                dtype = poldek_conf_PKGDIR_DEFAULT_TYPE;
            /* stype not default one, so guess destination type is default */
            else if (n_str_ne(stype, poldek_conf_PKGDIR_DEFAULT_TYPE))
                dtype = poldek_conf_PKGDIR_DEFAULT_TYPE;
        }
        
        if (dtype == NULL)
            dtype = src->type;
    }
    
    ssrc = source_clone(src);
    /* swap types */
    source_set_type(ssrc, stype);
    ssrc->flags &= ~(PKGSOURCE_NAMED);
    
    rc = 1;
    if ((typcaps = pkgdir_type_info(dtype)) < 0)
        rc = 0;
    
    else if ((typcaps & PKGDIR_CAP_SAVEABLE) == 0) {
        logn(LOGERR, _("%s: repository could not be created (missing "
                       "feature)"), dtype);
        rc = 0;

    } else if (idxpath == NULL) {
        if (source_is_remote(src)) { 
            logn(LOGERR, _("%s: unable to write remote index"),
                 source_idstr(src));
            rc = 0;
            
        } else if (source_is_type(ssrc, dtype)) { /* same type */
            struct stat st;

            if (stat(ssrc->path, &st) == 0) {
                logn(LOGERR, _("%s: refusing to overwrite index"),
                     source_idstr(ssrc));
                rc = 0;
            }
            /* if not exists, let do_source_make_idx() to shout */
        }
    }

    if (rc) {
        DBGF("do %s %s(%s) => %s\n", ssrc->path, stype, src->type ? src->type : "null", dtype);
        rc = do_makeidx(ssrc, dtype, idxpath, flags, kw);
        //rc = do_source_make_idx(ssrc, dtype, idxpath, flags, kw);
    }
    
    source_free(ssrc);
    return rc;
}

static int do_make_merged_idx(tn_array *sources,
                              const char *type, const char *idxpath,
                              unsigned cr_flags, tn_hash *kw) 
{
    struct source   *src;
    struct pkgdir   *pkgdir;
    char            path[PATH_MAX], sstr[256];
    int             rc = 1, n = 0, i;
    
    n_assert(type);
    n_assert(idxpath);
    
    idxpath = make_idxpath(path, sizeof(path), NULL, type, idxpath, kw);
    n_assert(idxpath);
    
    n = 0;
    for (i=0; i < n_array_size(sources); i++) {
        struct source *s = n_array_nth(sources, i);
        n += n_snprintf(&sstr[n], sizeof(sstr) - n, "  - %s (type=%s)\n",
                        source_idstr(s), s->type);
    }

    src = n_array_nth(sources, 0);
    msgn(1, "Creating merged %s index of:\n%s", type, sstr);
    
    if ((pkgdir = load_pkgdir(src, type, idxpath, 0))) {
        tn_array *pdirs = n_array_new(8, (tn_fn_free)pkgdir_free, NULL);
        int i;
        
        for (i=1; i < n_array_size(sources); i++) {
            struct source *s = n_array_nth(sources, i);
            struct pkgdir *p = load_pkgdir(s, type, idxpath, 0);

            if (p == NULL) {
                rc = 0;
                break;
            }
            
            n_array_push(pdirs, p);
        }

        if (rc) {
            for (i=0; i < n_array_size(pdirs); i++) {
                struct pkgdir *p = n_array_nth(pdirs, i);
                pkgdir_add_packages(pkgdir, p->pkgs);
            }

            rc = create_idx(pkgdir, type, idxpath, cr_flags, kw);
        }

        n_array_free(pdirs);
        pkgdir_free(pkgdir);
    }

    return rc;
}

int source_make_merged_idx(tn_array *sources,
                           const char *dtype, const char *idxpath,
                           unsigned flags, tn_hash *kw)
{
    tn_array *ssources;
    const char *stype = NULL;
    int typcaps;
    int rc = 0, i;

    DBGF("%s(src=%s) => %s\n", stype, src->type ? src->type : "null", dtype);

    n_assert(idxpath);


    for (i=0; i<n_array_size(sources); i++) {
        struct source *src = n_array_nth(sources, i);

        if (stype == NULL)
            stype = determine_stype(src, idxpath);

        DBGF("%s %s\n", src->path, src->type);
        if (src->type == NULL)
            source_set_default_type(src);

        n_assert(src->type);
    }
    

    if (dtype == NULL) {
        for (i=0; i<n_array_size(sources); i++) {
            struct source *src = n_array_nth(sources, i);

            /* if not from config */
            if ((src->flags & PKGSOURCE_NAMED) == 0) {
                if (n_str_eq(src->type, "dir"))
                    dtype = poldek_conf_PKGDIR_DEFAULT_TYPE;
                /* stype not default one, so guess destination type is default */
                else if (n_str_ne(stype, poldek_conf_PKGDIR_DEFAULT_TYPE))
                    dtype = poldek_conf_PKGDIR_DEFAULT_TYPE;
            }
            if (dtype)
                break;
        }
        
        if (dtype == NULL) {
            struct source *src = n_array_nth(sources, 0);
            dtype = src->type;
        }
    }
    
    ssources = n_array_clone(sources);
    for (i=0; i<n_array_size(sources); i++) {
        struct source *ssrc, *src = n_array_nth(sources, i);
        ssrc = source_clone(src);
        n_array_push(ssources, ssrc);
    }
    
    rc = 1;
    if ((typcaps = pkgdir_type_info(dtype)) < 0) {
        rc = 0;
    
    } else if ((typcaps & PKGDIR_CAP_SAVEABLE) == 0) {
        logn(LOGERR, _("%s: repository could not be created (missing "
                       "feature)"), dtype);
        rc = 0;
        
    } else {
        rc = do_make_merged_idx(ssources, dtype, idxpath, flags, kw);
    }

    n_array_free(ssources);
    return rc;
}


