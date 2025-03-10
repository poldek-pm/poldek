/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include "tags.h"

struct pkg_data {
    off_t             off_nodep_files;  /* no dep files offset in index */
//    off_t             off_pkguinf;
    struct tndb       *db;
    tn_hash           *db_dscr_h;
    tn_array          *langs;
};


static tn_array *parse_removed(char *str);
static tn_array *parse_depdirs(char *str);
static int parse_avlangs(char *str, struct pkgdir *pkgdir);

static void pndir_close(struct pndir *idx);

static int do_open(struct pkgdir *pkgdir, unsigned flags);
static int do_load(struct pkgdir *pkgdir, unsigned ldflags);
static void do_free(struct pkgdir *pkgdir);

static
int posthook_diff(struct pkgdir *pd1, struct pkgdir* pd2, struct pkgdir *diff);

struct pkgdir_module pkgdir_module_pndir = {
    NULL,
    PKGDIR_CAP_UPDATEABLE_INC | PKGDIR_CAP_UPDATEABLE |
    PKGDIR_CAP_HANDLEIGNORE,
    "pndir",
    NULL,
    "Native poldek's index format",
    "packages.ndir",
    "gz",
    do_open,
    do_load,
    pndir_m_create,
    pndir_m_update,
    pndir_m_update_a,
    NULL,
    do_free,
    pndir_localidxpath,
    posthook_diff,
};


const char *pndir_localidxpath(const struct pkgdir *pkgdir)
{
    struct pndir *idx = pkgdir->mod_data;
    DBGF("path %s\n", pkgdir->idxpath);
    //n_assert(0);
    if (idx && idx->_vf)
        return vfile_localpath(idx->_vf);
    return pkgdir->idxpath;
}

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
    memset(idx, 0, sizeof(*idx));
    idx->db = NULL;
    idx->dg = NULL;
    idx->idxpath[0] = '\0';
    idx->md_orig = NULL;
    idx->db_dscr_h = NULL;
}

static struct tndb *do_dbopen(const char *path, int vfmode, struct vfile **vf,
                              const char *srcnam)
{
    struct vfile *vf_;
    struct tndb  *db;
    int fd;

    if (vf)
        *vf = NULL;

    if ((vf_ = vfile_open_ul(path, VFT_IO, vfmode, srcnam)) == NULL)
        return NULL;

    if ((fd = dup(vf_->vf_fd)) == -1) {
        logn(LOGERR, "dup(%d): %m", vf_->vf_fd);
        vfile_close(vf_);
        return NULL;
    }

    if ((db = tndb_dopen(fd, vfile_localpath(vf_))) == NULL) {
        vfile_close(vf_);

    } else {
        if (vf)
            *vf = vf_;
        else
            vfile_close(vf_);
    }

    return db;
}


static
struct tndb *do_open_dscr(struct pndir *idx, int vfmode, struct vfile **vf,
                          time_t ts, const char *lang)
{
    char        buf[128], tmpath[PATH_MAX], tss[32];
    const char  *suffix;
    char        *idxpath;
    const char  *dbid, *langid;

    pndir_db_dscr_idstr(lang, &dbid, &langid);

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
    if (*langid == '\0') {
        if (*tss) {
            snprintf(buf, sizeof(buf), "%s.%s", pndir_desc_suffix, tss);
            suffix = buf;
        }


    } else {
        snprintf(buf, sizeof(buf), "%s.%s%s%s", pndir_desc_suffix, langid,
                 *tss ? "." : "", *tss ? tss : "");
        suffix = buf;
    }

    DBGF("mk %s, %s\n", idxpath, suffix);
    pndir_mkidx_pathname(tmpath, sizeof(tmpath), idxpath, suffix);

    msgn(3, _("Opening %s..."), vf_url_slim_s(tmpath, 0));
    return do_dbopen(tmpath, vfmode, vf, idx->srcnam);
}


static
int open_dscr(struct pndir *idx, int vfmode, time_t ts, const char *lang)
{
    struct tndb  *db = NULL;
    struct vfile *vf = NULL;

    if (idx->db_dscr_h == NULL)
        idx->db_dscr_h = pndir_db_dscr_h_new();

    if (pndir_db_dscr_h_get(idx->db_dscr_h, lang))
        return 1;

    if ((db = do_open_dscr(idx, vfmode, &vf, ts, lang)) == NULL)
        return 0;

    if (tndb_verify(db)) {
        pndir_db_dscr_h_insert(idx->db_dscr_h, lang, db);

    } else {
        logn(LOGERR, "%s: broken file", vf_url_slim_s(tndb_path(db), 0));

        if (vf->vf_flags & VF_FRMCACHE) { /* not fully downloaded? */
            n_assert(vfmode & VFM_CACHE);
            vfmode &= ~VFM_CACHE;
            vfmode |= VFM_NODEL;

            tndb_close(db);
            vfile_close(vf);

            return open_dscr(idx, vfmode, ts, lang);
        }
        tndb_close(db);
    }

    if (vf)
        vfile_close(vf);
    return pndir_db_dscr_h_get(idx->db_dscr_h, lang) != NULL;
}

static
int pndir_open(struct pndir *idx, struct pkgdir *pkgdir, int vfmode, unsigned flags)
{
    pndir_init(idx);

    if ((flags & PKGDIR_OPEN_DIFF) == 0) {
        if ((idx->dg = pndir_digest_new(pkgdir->idxpath, vfmode, pkgdir->name)) == NULL)
            return 0;

        if (*idx->dg->compr) {
            DBGF("md.compr %s\n", idx->dg->compr);
            pkgdir__set_compr(pkgdir, idx->dg->compr);
        }
    }

    if (pkgdir->name)
        idx->srcnam = n_strdup(pkgdir->name);

    idx->db = do_dbopen(pkgdir->idxpath, vfmode, &idx->_vf, idx->srcnam);
    if (idx->db == NULL)
        goto l_err;

    snprintf(idx->idxpath, sizeof(idx->idxpath), "%s", pkgdir->idxpath);

    return 1;

 l_err:
    pndir_close(idx);
    return 0;
}

static
int pndir_open_verify(struct pndir *idx, struct pkgdir *pkgdir,
                      int vfmode, unsigned flags)
{
    int rc;

    if (!pndir_open(idx, pkgdir, vfmode, flags))
        return 0;

    rc = 1;
    if (!tndb_verify(idx->db)) {
        logn(LOGERR, "%s: broken file", vf_url_slim_s(idx->_vf->vf_path, 0));
        rc = 0;

        if (idx->_vf->vf_flags & VF_FRMCACHE) { /* not fully downloaded? */
            n_assert(vfmode & VFM_CACHE);
            vfmode &= ~VFM_CACHE;
            vfmode |= VFM_NODEL;
            pndir_close(idx);

            return pndir_open_verify(idx, pkgdir, vfmode, flags);
        }

        pndir_close(idx);
    }
    return rc;
}

static
void pndir_close(struct pndir *idx)
{
    if (idx->db)
        tndb_close(idx->db);

    if (idx->db_dscr_h)
	n_hash_free(idx->db_dscr_h);

    if (idx->_vf)
        vfile_close(idx->_vf);

    if (idx->dg)
        pndir_digest_free(idx->dg);

    n_cfree(&idx->md_orig);
    n_cfree(&idx->srcnam);
    idx->_vf = NULL;
    idx->db = NULL;
    idx->dg = NULL;
    idx->idxpath[0] = '\0';
}

#if 0
static
void pndir_destroy(struct pndir *idx)
{
	pndir_close(idx);
}
#endif

#if 0
TODO
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
    unsigned long        ts = 0, ts_orig = 0;
    const char           *errmsg_brokenidx = _("%s: broken index (empty %s tag)");
    unsigned             vfmode = VFM_RO | VFM_NOEMPTY | VFM_NODEL;
    unsigned             pkgdir_flags = 0;
    char                 *path = pkgdir->path;
    char                 key[TNDB_KEY_MAX + 1], *val = NULL;
    unsigned             klen, vlen, vlen_max;
    int                  nerr = 0;

    pndir_init(&idx);

    if ((flags & PKGDIR_OPEN_REFRESH) == 0)
        vfmode |= VFM_CACHE;

    DBGF("%s [%s]\n", pkgdir->idxpath, pkgdir->name);
    if (!pndir_open_verify(&idx, pkgdir, vfmode, flags))
        return 0;

    nerr = 0;

    if (!tndb_verify(idx.db)) {
        logn(LOGERR, "%s: data digest mismatch, broken file", vf_url_slim_s(idx._vf->vf_path, 0));
        nerr++;
        goto l_end;
    }

    if (!tndb_it_start(idx.db, &it)) {
        nerr++;
        goto l_end;
    }

    vlen = 1024;
    vlen_max = vlen;
    val = n_malloc(vlen);
    if (!tndb_it_rget(&it, key, &klen, (void**)&val, &vlen)) {
        logn(LOGERR, _("%s: not a poldek index file"), pkgdir->idxpath);
        nerr++;
        goto l_end;
    }

    if (!hdr_eq(key, pndir_tag_hdr)) {
        logn(LOGERR, _("%s: not a poldek index file, %s"),
             pkgdir->idxpath, val);
        nerr++;
        goto l_end;
    }

    while (!hdr_eq(key, pndir_tag_endhdr)) {
        if (vlen < vlen_max)   /* to avoid needless tndb_it_rget()'s reallocs */
            vlen = vlen_max;
        else
            vlen_max = vlen;

        if (!tndb_it_rget(&it, key, &klen, (void**)&val, &vlen)) {
            logn(LOGERR, _("%s: not a poldek index file"), pkgdir->idxpath);
            nerr++;
            goto l_end;
        }

        if (hdr_eq(key, pndir_tag_opt)) {
            tn_array *opts = parse_depdirs(val);
            int i;

            for (i=0; i<n_array_size(opts); i++) {
                char *opt = n_array_nth(opts, i);
                if (strcmp(opt, "nodesc") == 0)
                    idx.crflags |= PKGDIR_CREAT_NODESC;
                else if (strcmp(opt, "nofl") == 0)
                    idx.crflags |= PKGDIR_CREAT_NOFL;
                else if (strcmp(opt, "nouniq") == 0)
                    idx.crflags |= PKGDIR_CREAT_NOUNIQ;
                else if (poldek_VERBOSE > 2)
                    logn(LOGWARN, _("%s:%s: unknown index opt"), pkgdir->idxpath, opt);
            }
            n_array_free(opts);

        } else if (hdr_eq(key, pndir_tag_ts_orig)) {
            if (sscanf(val, "%lu", &ts_orig) != 1) {
                logn(LOGERR, errmsg_brokenidx, path, pndir_tag_ts_orig);
                nerr++;
                goto l_end;
            }

        } else if (hdr_eq(key, pndir_tag_ts)) {
            if (sscanf(val, "%lu", &ts) != 1) {
                logn(LOGERR, errmsg_brokenidx, path, pndir_tag_ts);
                nerr++;
                goto l_end;
            }

        } else if (hdr_eq(key, pndir_tag_removed)) {
            n_assert(pkgdir->removed_pkgs == NULL);
            pkgdir->removed_pkgs = parse_removed(val);

        } else if (hdr_eq(key, pndir_tag_langs)) {
            parse_avlangs(val, pkgdir);

        } else if (hdr_eq(key, pndir_tag_pkgroups)) {
            tn_buf *nbuf;
            tn_buf_it group_it;

            nbuf = n_buf_new(0);
            n_buf_init(nbuf, val, vlen);
            n_buf_it_init(&group_it, nbuf);
            pkgdir->pkgroups = pkgroup_idx_restore(&group_it, 0);
            n_buf_free(nbuf);

        } else if (hdr_eq(key, pndir_tag_depdirs)) {
            n_assert(pkgdir->depdirs == NULL);
            pkgdir->depdirs = parse_depdirs(val);
        }
    }

    pkgdir->flags |= pkgdir_flags;
    pkgdir->ts = ts;
    pkgdir->size = tndb_size(idx.db);
    pkgdir->orig_ts = ts_orig;
    if (ts_orig)
        pkgdir->flags |= PKGDIR_DIFF;

    if ((idx.crflags & PKGDIR_CREAT_NODESC) == 0 &&
        (flags & PKGDIR_OPEN_NODESC) == 0)
    {
        pkgdir__setup_langs(pkgdir);
        if (pkgdir->langs) {
            int i, loadC = 0, loadi18n = 0;
            tn_array *langs;


            if (flags & PKGDIR_OPEN_ALLDESC)
                langs = n_hash_keys(pkgdir->avlangs_h);
            else
                langs = n_ref(pkgdir->langs);

            for (i=0; i < n_array_size(langs); i++) {
                const char *lang = n_array_nth(langs, i);
                DBGF("lang %s\n", lang);
                if (strcmp(lang, "C") == 0)
                    loadC = 1;
                else
                    loadi18n = 1;
            }
            n_array_free(langs);
            n_assert(loadC);

            if (loadC)
                if (!open_dscr(&idx, vfmode, pkgdir->orig_ts, "C"))
                    nerr++;

            if (nerr == 0 && loadi18n) {
                n_assert(loadC);
                if (!open_dscr(&idx, vfmode, pkgdir->orig_ts, "i18n"))
                    nerr++;
            }
        }

#if 0                           /* separate LANG files; obsoleted */
        if (pkgdir->langs) {
            for (i=0; i < n_array_size(pkgdir->langs); i++) {
                const char *lang = n_array_nth(pkgdir->langs, i);
                if (!open_dscr(&idx, pkgdir->orig_ts, lang)) {
                    nerr++;
                    break;
                }
            }
        }

#endif
    }


 l_end:
    if (nerr == 0) {
        /* keep tndb iterator state to start loading packages without backward seek */
        idx._tndb_first_pkg_nrec = it._nrec;
        idx._tndb_first_pkg_offs = it._off;

        pkgdir->mod_data = n_malloc(sizeof(idx));
        memcpy(pkgdir->mod_data, &idx, sizeof(idx));

    } else {
        pndir_close(&idx);
    }

    if (val)
        free(val);

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
struct pkg_data *pkg_data_malloc(tn_alloc *na)
{
    struct pkg_data *pd;

    pd = na->na_malloc(na, sizeof(*pd));
    pd->off_nodep_files = 0; //pd->off_pkguinf = 0;
    pd->db = NULL;
    pd->db_dscr_h = NULL;
    pd->langs = NULL;
    return pd;
}


static
void pkg_data_free(tn_alloc *na, void *ptr)
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
    na->na_free(na, pd);
}


static
struct pkguinf *pndir_m_load_pkguinf(tn_alloc *na, const struct pkg *pkg,
                                     void *ptr, tn_array *langs)
{
    struct pkg_data  *pd = ptr;

    if (pd->db_dscr_h == NULL)
        return NULL;

    return pndir_load_pkguinf(na, pd->db_dscr_h, pkg, langs ? langs : pd->langs);
}

static
tn_tuple *pndir_load_nodep_fl(tn_alloc *na, const struct pkg *pkg, void *ptr,
                              tn_array *foreign_depdirs)
{
    struct pkg_data *pd = ptr;
    tn_tuple *fl = NULL;

    pkg = pkg;
    if (pd->db && pd->off_nodep_files > 0) {
        tn_stream *st = tndb_tn_stream(pd->db);
        //printf("nodep_fl %p\n", pd->vf->vf_tnstream);
        n_stream_seek(st, pd->off_nodep_files, SEEK_SET);
        pkgfl_restore_st(na, &fl, st, foreign_depdirs, 0);
    }

    return fl;
}

static
int do_load(struct pkgdir *pkgdir, unsigned ldflags)
{
    struct pndir       *idx;
    struct pkg         *pkg = NULL;
    struct pkg_offs    pkgo;
    struct pkg_data    *pkgd;
    struct tndb_it     it;
    tn_stream          *st;
    tn_array           *ign_patterns = NULL;
    unsigned           klen, vlen;
    int                rc, nerr = 0;
    char               key[TNDB_KEY_MAX + 1], path[PATH_MAX];

    idx = pkgdir->mod_data;
    if (!tndb_it_start(idx->db, &it))
        return 0;

    /* start from first package position */
    it._nrec = idx->_tndb_first_pkg_nrec;
    it._off = idx->_tndb_first_pkg_offs;

    vf_url_slim(path, sizeof(path), pkgdir->idxpath, 0);

    if ((ldflags & PKGDIR_LD_DOIGNORE) && pkgdir->src &&
        n_array_size(pkgdir->src->ign_patterns)) {
        ign_patterns = pkgdir->src->ign_patterns;
    }

    DBGF("ign_patterns %p\n", ign_patterns);

    st = tndb_it_stream(&it);

    while ((rc = tndb_it_get_begin(&it, key, &klen, &vlen)) > 0) {
        struct pkg kpkg;

        n_assert(klen > 0);

        if (*key == '%' && strncmp(key, "%__h_", 5) == 0)
            goto l_continue_loop;

        if (pndir_parse_pkgkey(key, klen, &kpkg) == NULL) {
            logn(LOGERR, "%s: parse error", key);
            nerr++;
            goto l_continue_loop;
        }

        if (ign_patterns) {
            char buf[512];
            int i;

            pkg_snprintf(buf, sizeof(buf), &kpkg);
            for (i=0; i < n_array_size(ign_patterns); i++) {
                char *p = n_array_nth(ign_patterns, i);
                if (fnmatch(p, buf, 0) == 0) {
                    msgn(3, "pndir: ignored %s", pkg_snprintf_s(pkg));
                    goto l_continue_loop;
                }
            }
        }

        pkg = pkg_restore_st(st, pkgdir->na, &kpkg, pkgdir->foreign_depdirs,
                             ldflags, &pkgo, path);

        DBGF("%s -> %p\n", pkg_snprintf_s(&kpkg), pkg);
        if (pkg == NULL) {
            nerr++;
            goto l_continue_loop;
        }

        pkg->pkgdir = pkgdir;

        pkgd = pkg_data_malloc(pkgdir->na);
        pkgd->off_nodep_files = pkgo.nodep_files_offs;
        //pkgd->off_pkguinf = pkgo.pkguinf_offs;
        pkgd->db = tndb_ref(idx->db);

        if (idx->db_dscr_h)
            pkgd->db_dscr_h = n_ref(idx->db_dscr_h);

        if (pkgdir->langs)
            pkgd->langs = n_ref(pkgdir->langs);

        pkg->pkgdir_data = pkgd;
        pkg->pkgdir_data_free = pkg_data_free;
        pkg->load_pkguinf = pndir_m_load_pkguinf;
        pkg->load_nodep_fl = pndir_load_nodep_fl;

        n_array_push(pkgdir->pkgs, pkg);

    l_continue_loop:
        if (!tndb_it_get_end(&it) || nerr > 0) {
            logn(LOGERR, "%s: iteration error, broken file", path);
            nerr++;
            break;
        }
    }

    if (nerr)
        n_array_clean(pkgdir->pkgs);

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
        struct pkg  *pkg;

        if (*p == '\0')
            continue;

        if ((pkg = pndir_parse_pkgkey(p, strlen(p), NULL)) == NULL) {
            if (poldek_util_parse_nevr(p, &name, &epoch, &ver, &rel)) {
                pkg = pkg_new(name, epoch, ver, rel, NULL, NULL);
            }
        }

        if (pkg)
            n_array_push(pkgs, pkg);
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

static inline int up_avlangs(char *s, struct pkgdir *pkgdir)
{
    char *p;
    int count;

    p = strchr(s, '|');
    if (!p) {                /* snap legacy... */
        pkgdir__update_avlangs(pkgdir, s, 10000);
        return 1;
    }

    *p = '\0';
    p++;

    if (sscanf(p, "%d", &count) != 1)
        n_die("%s|%s: bad langs format!", s, p);
    pkgdir__update_avlangs(pkgdir, s, count);
    return 1;
}


static int parse_avlangs(char *str, struct pkgdir *pkgdir)
{
    char *p, *token;

    p = str;
    p = eatws(p);

    while ((token = next_tokn(&p, ':', NULL)) != NULL) {
        up_avlangs(token, pkgdir);
    }
    up_avlangs(p, pkgdir);
    return 1;
}


int pndir_tsstr(char *tss, int size, time_t ts)
{
    return strftime(tss, size, "%Y.%m.%d-%H.%M.%S", gmtime(&ts));
}
