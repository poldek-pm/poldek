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
#include <trurl/nmalloc.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>

#include <vfile/vfile.h>

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pkgdir.h"
#include "pndir.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgfl.h"
#include "pkgroup.h"
#include "pkgmisc.h"

struct pkg_data {
    off_t             off_nodep_files;  /* no dep files offset in index */
//    off_t             off_pkguinf;
    struct tndb       *db;
    tn_hash           *db_dscr_h;
    tn_array          *langs;
};


static tn_array *parse_removed(char *str);
static tn_array *parse_depdirs(char *str);

static int do_open(struct pkgdir *pkgdir, unsigned flags);
static int do_load(struct pkgdir *pkgdir, unsigned ldflags);
static void do_free(struct pkgdir *pkgdir);

static
int posthook_diff(struct pkgdir *pd1, struct pkgdir* pd2, struct pkgdir *diff);

struct pkgdir_module pkgdir_module_pndir = {
    PKGDIR_CAP_UPDATEABLE_INC | PKGDIR_CAP_UPDATEABLE, 
    "pndir",
    NULL, 
    "packages.ndir.gz",
    do_open,
    do_load,
    pndir_m_create,
    pndir_m_update, 
    pndir_m_update_a,
    NULL, 
    do_free,
    posthook_diff, 
};


static
int posthook_diff(struct pkgdir *pd1, struct pkgdir* pd2, struct pkgdir *diff)
{
	struct pndir *idx, *idx2;

    pd2 = pd2;                  /* unused */
	if ((idx2 = pd1->mod_data) == NULL)
		return 0;
	
	idx = diff->mod_data;
	
	if (idx == NULL) {
        idx = n_malloc(sizeof(*idx));
        pndir_init(idx);
        diff->mod_data = idx;
	}
	
	idx->md_orig = n_strdup(idx2->dg->md);
	return 1;
}


inline static char *eatws(char *str) 
{
    while (isspace(*str))
        str++;
    return str;
}


inline static char *next_tokn(char **str, char delim, int *toklen) 
{
    char *p, *token;

    
    if ((p = strchr(*str, delim)) == NULL) 
        token = NULL;
    else {
        *p = '\0';
        
        if (toklen)
            *toklen = p - *str;
        p++;
        while(isspace(*p))
            p++;
        token = *str;
        *str = p;
    }
    
    return token;
}


void pndir_init(struct pndir *idx) 
{
	idx->db = NULL;
    idx->dg = NULL;
    idx->idxpath[0] = '\0';
	idx->md_orig = NULL;
    idx->db_dscr_h = NULL;
}


static struct tndb *do_dbopen(const char *path, int vfmode, struct vfile **vf)
{
    struct vfile *vf_;
    struct tndb  *db;
    int fd;
    
    if (vf)
        *vf = NULL;
    
    if ((vf_ = vfile_open(path, VFT_IO, vfmode)) == NULL)
        return NULL;

    if ((fd = dup(vf_->vf_fd)) == -1) {
        logn(LOGERR, "dup(%d): %m", vf_->vf_fd);
        vfile_close(vf_);
        return NULL;
    }
    
    
    if ((db = tndb_dopen(fd, vfile_localpath(vf_))) == NULL)
        vfile_close(vf_);
    
    else {
        if (vf)
            *vf = vf_;
        else
            vfile_close(vf_);
    }
    
    return db;
}


static
int open_dscr(struct pndir *idx, time_t ts, const char *lang) 
{
    char        buf[128], tmpath[PATH_MAX], tss[32];
    const char  *suffix;
    char        *idxpath;
    
    idxpath = idx->idxpath;
    *tss = '\0';
    
    if (ts) {
        int len, tss_len;
        char *p;
        
        tss_len = pndir_tsstr(tss, sizeof(tss), ts);
        len = strlen(idx->idxpath) + 1;
        idxpath = alloca(len);
        memcpy(idxpath, idx->idxpath, len);
        
        if ((p = strstr(idxpath, tss)) && p != idxpath && *(p - 1) == '.') {
            //*(p - 1) = '\0';
            strcpy(p - 1, p + tss_len);
        } else {
            *tss = '\0';
        }
        
    }
    
    suffix = pndir_desc_suffix;
    if (strcmp(lang, "C") == 0) {
        if (*tss) {
            snprintf(buf, sizeof(buf), "%s.%s", pndir_desc_suffix, tss);
            suffix = buf;
        }
        
        
    } else {
        snprintf(buf, sizeof(buf), "%s.%s%s%s", pndir_desc_suffix, lang,
                 *tss ? "." : "", *tss ? tss : "");
        suffix = buf;
    }

    DBGF("mk %s, %s\n", idxpath, suffix);
    pndir_mkidx_pathname(tmpath, sizeof(tmpath), idxpath, suffix);
    
    if (idx->db_dscr_h == NULL)
        idx->db_dscr_h = n_hash_new(21, (tn_fn_free)tndb_close);

    if (!n_hash_exists(idx->db_dscr_h, lang)) {
        struct tndb *db;
        
        msgn(0, _("Opening %s..."), vf_url_slim_s(tmpath, 0));
        if ((db = do_dbopen(tmpath, idx->_vf->vf_mode, NULL))) {
            if (tndb_verify(db))
                n_hash_insert(idx->db_dscr_h, lang, db);
            else {
                tndb_close(db);
                logn(LOGERR, "%s: broken file", vf_url_slim_s(tmpath, 0));
            }
        }
    }

    return n_hash_exists(idx->db_dscr_h, lang);
}



int pndir_open(struct pndir *idx, const char *path, int vfmode, unsigned flags)
{
	pndir_init(idx);

    if ((flags & PKGDIR_OPEN_DIFF) == 0)
        if ((idx->dg = pndir_digest_new(path, vfmode)) == NULL)
            return 0;
    
    idx->db = do_dbopen(path, vfmode, &idx->_vf);
    if (idx->db == NULL)
        goto l_err;
        
    snprintf(idx->idxpath, sizeof(idx->idxpath), "%s", path);
    return 1;


 l_err:
    pndir_close(idx);
    return 0;
}

void pndir_close(struct pndir *idx) 
{
    if (idx->db)
        tndb_close(idx->db);
    
    if (idx->_vf)
        vfile_close(idx->_vf);

    if (idx->dg)
        pndir_digest_free(idx->dg);

	if (idx->md_orig) {
		free(idx->md_orig);
		idx->md_orig = NULL;
	}

    idx->_vf = NULL;
    idx->db = NULL;
    idx->dg = NULL;
    idx->idxpath[0] = '\0';
}


void pndir_destroy(struct pndir *idx)
{
	pndir_close(idx);
}

#if 0
DUPA
static int valid_version(const char *ver, const char *path)
{
    int major, minor;
    
    if (sscanf(ver, "%u.%u", &major, &minor) != 2) {
        logn(LOGERR, _("%s: invalid version string %s"), path, ver);
        return 0;
    }
    
    if (major != FILEFMT_MAJOR) 
        logn(LOGERR, _("%s: unsupported version %s (%d.x is required)"),
            path, ver, FILEFMT_MAJOR);
    
    else if (minor > FILEFMT_MINOR) 
        logn(LOGERR, _("%s: unsupported version %s (upgrade the poldek)"),
            path, ver);

    return major == FILEFMT_MAJOR && minor <= FILEFMT_MINOR;
}
#endif

static
int do_open(struct pkgdir *pkgdir, unsigned flags)
{
    struct tndb_it       it;
    struct pndir         idx;
    time_t               ts = 0, ts_orig = 0;
    const char           *errmsg_brokenidx = _("%s: broken index (empty %s tag)");
    unsigned             vfmode = VFM_RO | VFM_NOEMPTY;
    unsigned             pkgdir_flags = 0;
    char                 *path = pkgdir->path;
    char                 key[TNDB_KEY_MAX + 1], val[4096];
    int                  nerr = 0, klen, vlen;
    tn_array             *avlangs = NULL;
    
    if ((flags & PKGDIR_OPEN_REFRESH) == 0) 
        vfmode |= VFM_CACHE;
    
    if (!pndir_open(&idx, pkgdir->idxpath, vfmode, flags))
        return 0;
    
    nerr = 0;

    if (!tndb_verify(idx.db)) {
        logn(LOGERR, "%s: broken file", vf_url_slim_s(idx._vf->vf_path, 0));
        goto l_end;
    }

    if (!tndb_it_start(idx.db, &it)) {
        nerr++;
        goto l_end;
    }

    vlen = sizeof(val);
    if (!tndb_it_get(&it, key, &klen, val, &vlen)) {
        logn(LOGERR, _("%s: not a poldek index file"), path);
        goto l_end;
    }
    
    if (strcmp(key, pndir_tag_hdr) != 0)
        logn(LOGERR, _("%s: not a poldek index file, %s"), path, val);

    
    while (strcmp(key, pndir_tag_endhdr) != 0) {
        vlen = sizeof(val);
        if (!tndb_it_get(&it, key, &klen, val, &vlen)) {
            logn(LOGERR, _("%s: not a poldek index file"), path);
            goto l_end;
        }
        
        if (strcmp(key, pndir_tag_ts) == 0) {
            if (sscanf(val, "%lu", &ts) != 1) {
                logn(LOGERR, errmsg_brokenidx, path, pndir_tag_ts);
                nerr++;
                goto l_end;
            }
            
        } else if (strcmp(key, pndir_tag_ts_orig) == 0) {
            if (sscanf(val, "%lu", &ts_orig) != 1) {
                logn(LOGERR, errmsg_brokenidx, path, pndir_tag_ts_orig);
                nerr++;
                goto l_end;
            }
            
        } else if (strcmp(key, pndir_tag_removed) == 0) {
            n_assert(pkgdir->removed_pkgs == NULL);
            pkgdir->removed_pkgs = parse_removed(val);

        } else if (strcmp(key, pndir_tag_langs) == 0) {
            n_assert(avlangs == NULL);
            avlangs = parse_depdirs(val);
            
        } else if (strcmp(key, pndir_tag_depdirs) == 0) {
            n_assert(pkgdir->depdirs == NULL);
            pkgdir->depdirs = parse_depdirs(val);
        }
    }
    
    
    
    pkgdir->flags |= pkgdir_flags;
    pkgdir->pkgs = pkgs_array_new(1024);
    pkgdir->ts = ts;
    pkgdir->ts_orig = ts_orig;
    if (ts_orig)
        pkgdir->flags |= PKGDIR_DIFF;
    
    
    if (avlangs) {
        int i;

        for (i=0; i < n_array_size(avlangs); i++)
            n_hash_insert(pkgdir->avlangs_h,
                          (const char*)n_array_nth(avlangs, i), NULL);
            
        pkgdir_setup_langs(pkgdir);
        if (pkgdir->langs) {
            for (i=0; i < n_array_size(pkgdir->langs); i++) {
                const char *lang = n_array_nth(pkgdir->langs, i);
                if (!open_dscr(&idx, pkgdir->ts_orig, lang)) {
                    nerr++;
                    break;
                }
            }
        }
    }
    

 l_end:

    if (nerr == 0) {
        pkgdir->mod_data = n_malloc(sizeof(idx));
        memcpy(pkgdir->mod_data, &idx, sizeof(idx));
        
    } else {
        pndir_close(&idx);
    }
    
    return nerr == 0;
}


static void do_free(struct pkgdir *pkgdir) 
{
    if (pkgdir->mod_data) {
        struct pndir *idx = pkgdir->mod_data;
        pndir_close(idx);
        if (idx->md_orig)
            free(idx->md_orig);
        free(idx);
        pkgdir->mod_data = NULL;
    }
}

static
struct pkg_data *pkg_data_malloc(void)  
{
    struct pkg_data *pd;
    
    pd = n_malloc(sizeof(*pd));
    pd->off_nodep_files = 0; //pd->off_pkguinf = 0;
    pd->db = NULL;
    pd->db_dscr_h = NULL;
    pd->langs = NULL;
    return pd;
}


static
void pkg_data_free(void *ptr) 
{
    struct pkg_data *pd = ptr;

    if (pd->db) {
        tndb_close(pd->db);
        pd->db = NULL;
    }

    if (pd->db_dscr_h) {
        n_hash_free(pd->db_dscr_h);
        pd->db_dscr_h = NULL;
    }

    if (pd->langs) {
        n_array_free(pd->langs);
        pd->langs = NULL;
    }
    free(pd);
}


static 
struct pkguinf *pndir_load_pkguinf(const struct pkg *pkg, void *ptr)
{
    struct pkg_data  *pd = ptr;
    struct pkguinf   *pkgu = NULL;
    struct tndb      *db_C;
    char             key[TNDB_KEY_MAX], val[4096];
    int              klen, vlen;
    
    if (pd->db_dscr_h == NULL)
        return NULL;

    if ((db_C = n_hash_get(pd->db_dscr_h, "C")) == NULL) 
        return NULL;

    klen = pndir_make_pkgkey(key, sizeof(key), pkg);

    if (klen > 0 && (vlen = tndb_get(db_C, key, klen, val, sizeof(val))) > 0) {
        tn_buf     *nbuf;
        tn_buf_it  it;
        
        nbuf = n_buf_new(0);
        n_buf_init(nbuf, val, vlen);
        n_buf_it_init(&it, nbuf);
        pkgu = pkguinf_restore(&it, "C");

        if (pd->langs) {
            int i;
                
            for (i = n_array_size(pd->langs) - 1; i >= 0; i--) {
                struct tndb  *db;
                const char   *lang;

                lang = n_array_nth(pd->langs, i);
                if (strcmp(lang, "C") == 0)
                    continue;
                
                db = n_hash_get(pd->db_dscr_h, lang);
                vlen = tndb_get(db, key, klen, val, sizeof(val));
                //printf("ld %s: %s (%d)\n", pkg_snprintf_s(pkg), lang, vlen);
                if (vlen > 0) {
                    n_buf_clean(nbuf);
                    n_buf_init(nbuf, val, vlen);
                    n_buf_it_init(&it, nbuf);
                    pkguinf_restore_i18n(pkgu, &it, lang);
                }
            }
        }
        
        n_buf_free(nbuf);
    }

    return pkgu;
}

static 
tn_array *pndir_load_nodep_fl(const struct pkg *pkg, void *ptr,
                              tn_array *foreign_depdirs)
{
    struct pkg_data *pd = ptr;
    tn_array *fl = NULL;

    pkg = pkg;
    if (pd->db && pd->off_nodep_files > 0) {
        tn_stream *st = tndb_tn_stream(pd->db);
        //printf("nodep_fl %p\n", pd->vf->vf_tnstream);
        n_stream_seek(st, pd->off_nodep_files, SEEK_SET);
        fl = pkgfl_restore_f(st, foreign_depdirs, 0);
    }
    
    return fl;
}

static
int do_load(struct pkgdir *pkgdir, unsigned ldflags)
{
    struct pndir       *idx;
    struct pkg         *pkg;
    struct pkg_offs    pkgo;
    struct pkg_data    *pkgd;
    struct tndb_it     it;
    tn_stream          *st;
    int                rc, klen;
    char               key[TNDB_KEY_MAX + 1];

    idx = pkgdir->mod_data;
    

    if (!tndb_it_start(idx->db, &it))
        return 0;

    st = tndb_it_stream(&it);
    while ((rc = tndb_it_get_begin(&it, key, &klen, NULL)) > 0) {
        struct pkg *kpkg;
        
        n_assert(klen > 0);
            
        if (*key == '%' && strncmp(key, "%__h_", 5) == 0) { 
            //printf("skip %s\n", key);
            tndb_it_get_end(&it);
            continue;
        }

        if ((kpkg = pndir_parse_pkgkey(key, klen)) == NULL) {
            logn(LOGERR, "%s: parse error", key);
            tndb_it_get_end(&it);
            continue;
        }

        if ((pkg = pkg_restore(st, kpkg, pkgdir->foreign_depdirs,
                               ldflags, &pkgo, pkgdir->path))) {
            pkg->pkgdir = pkgdir;

            pkgd = pkg_data_malloc();
            pkgd->off_nodep_files = pkgo.nodep_files_offs;
            //pkgd->off_pkguinf = pkgo.pkguinf_offs;
            pkgd->db = tndb_ref(idx->db);
            
            if (idx->db_dscr_h)
                pkgd->db_dscr_h = n_ref(idx->db_dscr_h);
            
            if (pkgdir->langs)
                pkgd->langs = n_ref(pkgdir->langs);
            
            pkg->pkgdir_data = pkgd;
            pkg->pkgdir_data_free = pkg_data_free;
            pkg->load_pkguinf = pndir_load_pkguinf;
            pkg->load_nodep_fl = pndir_load_nodep_fl;
            n_array_push(pkgdir->pkgs, pkg);
        }
        tndb_it_get_end(&it);
    }

    return n_array_size(pkgdir->pkgs);
}


static tn_array *parse_removed(char *str) 
{
    char *p, *q;
    tn_array *pkgs;

    pkgs = pkgs_array_new(64);
    p = q = eatws(str);
    while ((p = next_tokn(&q, ' ', NULL)) != NULL) {
        const char   *name = NULL, *ver = NULL, *rel = NULL;
        int32_t      epoch = 0;

        if (*p && parse_nevr(p, &name, &epoch, &ver, &rel)) {
            struct pkg *pkg = pkg_new(name, epoch, ver, rel, NULL, NULL);
            n_array_push(pkgs, pkg);
        }
    }
    
    if (n_array_size(pkgs) == 0) {
        n_array_free(pkgs);
        pkgs = NULL;
    }
    
    return pkgs;
}

static tn_array *parse_depdirs(char *str) 
{
    char *p, *token;
    tn_array *arr;

    arr = n_array_new(16, free, (tn_fn_cmp)strcmp);
    p = str;
    p = eatws(p);

    while ((token = next_tokn(&p, ':', NULL)) != NULL) {
        n_array_push(arr, n_strdup(token));
    }
                
    n_array_push(arr, n_strdup(p));
                
    if (n_array_size(arr)) 
        n_array_sort(arr);

    return arr;
}

int pndir_tsstr(char *tss, int size, time_t ts) 
{
    return strftime(tss, size, "%Y.%m.%d-%H.%M.%S", gmtime(&ts));
}





