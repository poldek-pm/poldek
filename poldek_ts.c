/*
  Copyright (C) 2000 - 2004 Pawel A. Gajda <mis@pld.org.pl>

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

#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>

#include <trurl/nmalloc.h>
#include <trurl/nstr.h>
#define POLDEK_INTERNAL
#include "vfile/vfile.h"
#include "pkgdir/source.h"
#include "pkgdb.h"
#include "poldek.h"
#include "poldek_term.h"
#include "pkgset.h"
#include "arg_packages.h"
#include "misc.h"
#include "log.h"
#include "i18n.h"

#define bitvect_slot_itype  unsigned int
#define bitvect_slot_size   sizeof(bitvect_slot_itype) * CHAR_BIT
#define bitvect_mask(b)     (1 << ((b) % (bitvect_slot_size)))
#define bitvect_slot(b)     ((b) / (bitvect_slot_size))
#define bitvect_set(a, b)   ((a)[bitvect_slot(b)] |= bitvect_mask(b))
#define bitvect_clr(a, b)   ((a)[bitvect_slot(b)] &= ~(bitvect_mask(b)))
#define bitvect_isset(a, b) ((a)[bitvect_slot(b)] & bitvect_mask(b))

extern int poldek_term_ask_yn(int default_a, const char *fmt, ...);
extern int poldek_term_ask_pkg(const char *capname, struct pkg **pkgs,
                               struct pkg *deflt);


extern
int do_poldek_ts_upgrade_dist(struct poldek_ts *ts);
extern
int do_poldek_ts_install(struct poldek_ts *ts, struct install_info *iinf);
extern
int do_poldek_ts_uninstall(struct poldek_ts *ts, struct install_info *iinf);


#define TS_CONFIG_LATER (1 << 0)

struct poldek_ts *poldek_ts_new(struct poldek_ctx *ctx)
{
    struct poldek_ts *ts;

    ts = n_malloc(sizeof(*ts));
    poldek_ts_init(ts, ctx);
    return ts;
}

void poldek_ts_free(struct poldek_ts *ts)
{
    poldek_ts_destroy(ts);
    free(ts);
}


void poldek_ts_setop(struct poldek_ts *ts, int optv, int on_off)
{
    n_assert(bitvect_slot(optv) < sizeof(ts->_opvect)/sizeof(bitvect_slot_itype));
    if (on_off)
        bitvect_set(ts->_opvect, optv);
    else
        bitvect_clr(ts->_opvect, optv);

    //printf("setop %d TO %d\n", optv, on_off);
}

int poldek_ts_getop(const struct poldek_ts *ts, int optv)
{
    n_assert(bitvect_slot(optv) < sizeof(ts->_opvect)/sizeof(bitvect_slot_itype));
    return bitvect_isset(ts->_opvect, optv) > 0;
}

int poldek_ts_getop_v(const struct poldek_ts *ts, int optv, ...)
{
    unsigned v = 0;
    va_list ap;

    va_start(ap, optv);
    if (poldek_ts_getop(ts, optv)) {
        printf("getop_v %d ON\n", optv);
        v++;
    }
    
        
    while ((optv = va_arg(ap, int)) > 0) {
        if (poldek_ts_getop(ts, optv)) {
            printf("getop_v %d ON\n", optv);
            v++;
        }
        
    }
    va_end(ap);
    return v;
}

int poldek_ts_is_interactive_on(const struct poldek_ts *ts)
{
    return ts->getop_v(ts, POLDEK_OP_CONFIRM_INST, POLDEK_OP_CONFIRM_UNINST,
                       POLDEK_OP_EQPKG_ASKUSER, 0);
}


static void cp_str(char **dst, const char *src)
{
    if (src)
        *dst = n_strdup(src);
    else
        *dst = NULL;
}

int poldek_ts_init(struct poldek_ts *ts, struct poldek_ctx *ctx)
{

    memset(ts, 0, sizeof(*ts));
    ts->setop = poldek_ts_setop;
    ts->getop = poldek_ts_getop;
    ts->getop_v = poldek_ts_getop_v;
    
    if (ctx) {
        ts->ctx = ctx;
        memcpy(ts->_opvect, ctx->ts->_opvect, sizeof(ts->_opvect));
        if (!poldek__is_setup_done(ctx))
            ts->_iflags |= TS_CONFIG_LATER;
        
    }

    ts->aps = arg_packages_new();

    ts->db = NULL;

    if (ctx) {
        cp_str(&ts->rootdir, ctx->ts->rootdir);
        cp_str(&ts->fetchdir, ctx->ts->fetchdir);
        cp_str(&ts->cachedir, ctx->ts->cachedir);
        cp_str(&ts->dumpfile, ctx->ts->dumpfile);
        cp_str(&ts->prifile, ctx->ts->prifile);
        
        ts->rpmacros = n_array_dup(ctx->ts->rpmacros, (tn_fn_dup)strdup);
        ts->rpmopts = n_array_dup(ctx->ts->rpmopts, (tn_fn_dup)strdup);
        ts->hold_patterns = n_array_dup(ctx->ts->hold_patterns,
                                        (tn_fn_dup)strdup);
        ts->ign_patterns = n_array_dup(ctx->ts->ign_patterns,
                                       (tn_fn_dup)strdup);
    } else {
        ts->rootdir = NULL;
        ts->fetchdir = NULL;
        ts->cachedir = NULL;
        ts->dumpfile = NULL;
        ts->prifile = NULL;

        ts->rpmacros = n_array_new(2, free, NULL);
        ts->rpmopts = n_array_new(4, free, (tn_fn_cmp)strcmp);
        ts->hold_patterns = n_array_new(4, free, (tn_fn_cmp)strcmp);
        ts->ign_patterns = n_array_new(4, free, (tn_fn_cmp)strcmp);
    }
    dbgf_("%p->%p, %p\n", ts, ts->hold_patterns, ctx);
    ts->askpkg_fn = poldek_term_ask_pkg;
    ts->ask_fn = poldek_term_ask_yn;
    
    return 1;
}


void poldek_ts_destroy(struct poldek_ts *ts)
{
    ts->_flags = 0;
    ts->ctx = NULL;
    
    if (ts->db)
        pkgdb_free(ts->db);
    ts->db = NULL;

    n_cfree(&ts->rootdir);
    n_cfree(&ts->fetchdir);
    n_cfree(&ts->cachedir);
    n_cfree(&ts->dumpfile);
    n_cfree(&ts->prifile);
    
    if (ts->rpmopts)
        n_array_free(ts->rpmopts);

    if (ts->rpmacros)
        n_array_free(ts->rpmacros);

    if (ts->hold_patterns)
        n_array_free(ts->hold_patterns);

    if (ts->ign_patterns)
        n_array_free(ts->ign_patterns);

    ts->rpmopts = NULL;
    ts->rpmacros = NULL;
    ts->hold_patterns = ts->ign_patterns = NULL;
}


#undef poldek_ts_setf
#undef poldek_ts_clrf
#undef poldek_ts_issetf

void poldek_ts_setf(struct poldek_ts *ts, uint32_t flag)
{
    ts->_flags |= flag;
    if (flag & POLDEK_TS_INSTALL) {
        printf("POLDEK_TS_INSTALL_ON\n");
    }
    
}

void poldek_ts_clrf(struct poldek_ts *ts, uint32_t flag)
{
    ts->_flags &= ~flag;
    if (flag & POLDEK_TS_INSTALL) {
        printf("POLDEK_TS_INSTALL_OFF\n");
    }
}

uint32_t poldek_ts_issetf(struct poldek_ts *ts, uint32_t flag)
{
    return ts->_flags & flag;
}

static char *prepare_path(char *pathname) 
{
    if (pathname == NULL)
        return pathname;

    if (vf_url_type(pathname) & VFURL_LOCAL) {
        char path[PATH_MAX];
        const char *ppath;
        
        if ((ppath = abs_path(path, sizeof(path), pathname)))
            return n_strdup(ppath);
    }
    
    return pathname;
}


char *poldek__conf_path(char *s, char *v)
{
    char *ss;

    if ((v && s && v != s) || s == NULL) {
        if (s)
            free(s);
        s = n_strdup(v);
    }
    
    ss = prepare_path(s);
    if (ss != s) {
        free(s);
        s = ss;
    }
    
    return s;
}


int poldek_ts_configure(struct poldek_ts *ts, int param, ...) 
{
    va_list ap;
    int rc;
    
    va_start(ap, param);
    rc = poldek_ts_vconfigure(ts, param, ap);
    va_end(ap);
    return rc;
}


int poldek_ts_vconfigure(struct poldek_ts *ts, int param, va_list ap) 
{
    int      rc;
    char     *vs;
    unsigned uv, uv_val;

    
    rc = 1;
    vs = va_arg(ap, char*);

    switch (param) {
        case POLDEK_CONF_OPT:
            uv = va_arg(ap, unsigned);
            uv_val = va_arg(ap, unsigned);
            ts->setop(ts, uv, uv_val);
            break;

        case POLDEK_CONF_CACHEDIR:
            if (vs) {
                printf("cachedirX0 %s\n", vs);
                ts->cachedir = poldek__conf_path(ts->cachedir, vs);
                trimslash(ts->cachedir);
                printf("cachedirX %s\n", ts->cachedir);
            }
            break;

        case POLDEK_CONF_FETCHDIR:
            if (vs) {
                ts->fetchdir = poldek__conf_path(ts->fetchdir, vs);
                trimslash(ts->fetchdir);
                printf("fetchdir %s\n", ts->fetchdir);
            }
            break;

        case POLDEK_CONF_ROOTDIR:
            vs = va_arg(ap, char*);
            if (vs) {
                ts->rootdir = poldek__conf_path(ts->rootdir, vs);
                trimslash(ts->rootdir);
                printf("rootdir %s\n", ts->rootdir);
            }
            break;

        case POLDEK_CONF_DUMPFILE:
            vs = va_arg(ap, char*);
            if (vs) {
                printf("dumpfile %s\n", vs);
                ts->dumpfile = poldek__conf_path(ts->dumpfile, vs);
            }
            break;

        case POLDEK_CONF_PRIFILE:
            vs = va_arg(ap, char*);
            if (vs) {
                printf("prifile %s\n", vs);
                ts->prifile = poldek__conf_path(ts->prifile, vs);
            }
            break;

        
        case POLDEK_CONF_RPMMACROS:
            n_array_push(ts->rpmacros, n_strdup(vs));
            break;

        case POLDEK_CONF_RPMOPTS:
            n_array_push(ts->rpmopts, n_strdup(vs));
            break;

        case POLDEK_CONF_HOLD:
        case POLDEK_CONF_IGNORE: {
            tn_array *patterns = NULL;
            
            if (param == POLDEK_CONF_HOLD)
                patterns = ts->hold_patterns;
            else if (param == POLDEK_CONF_IGNORE)
                patterns = ts->ign_patterns;

            if (vs) {
                if (strchr(vs, ',') == NULL) {
                    n_array_push(patterns, n_strdup(vs));
                
                } else {
                    const char **tl_save, **tl;
                    
                    tl = tl_save = n_str_tokl(vs, ",");
                    while (*tl) {
                        n_array_push(patterns, n_strdup(*tl));
                        tl++;
                    }
                    n_str_tokl_free(tl_save);
                }
            }
            break;
        }
            
        default:
            n_assert(0);
    }
    
    va_end(ap);
    return rc;
}

void install_info_init(struct install_info *iinf) 
{
    iinf->installed_pkgs = pkgs_array_new(16);
    iinf->uninstalled_pkgs = pkgs_array_new(16);
}

void install_info_destroy(struct install_info *iinf) 
{
    if (iinf->installed_pkgs) {
        n_array_free(iinf->installed_pkgs);
        iinf->installed_pkgs = NULL;
    }
    
    if (iinf->uninstalled_pkgs) {
        n_array_free(iinf->uninstalled_pkgs);
        iinf->uninstalled_pkgs = NULL;
    }
}


int poldek_ts_add_pkg(struct poldek_ts *ts, struct pkg *pkg)
{
    return arg_packages_add_pkg(ts->aps, pkg);
}

int poldek_ts_add_pkgmask(struct poldek_ts *ts, const char *mask)
{
    return arg_packages_add_pkgmask(ts->aps, mask);
}

int poldek_ts_add_pkglist(struct poldek_ts *ts, const char *path)
{
    return arg_packages_add_pkglist(ts->aps, path);
}

int poldek_ts_add_pkgfile(struct poldek_ts *ts, const char *path)
{
    return arg_packages_add_pkgfile(ts->aps, path);
}



void poldek_ts_clean_arg_pkgmasks(struct poldek_ts *ts)
{
    arg_packages_clean(ts->aps);
}

const tn_array* poldek_ts_get_arg_pkgmasks(struct poldek_ts *ts) 
{
    return arg_packages_get_pkgmasks(ts->aps);
}

int poldek_ts_get_arg_count(struct poldek_ts *ts) 
{
    return arg_packages_size(ts->aps);
}


static
int ts_mark_arg_packages(struct poldek_ts *ts, int withdeps) 
{
    int rc = 1;
    
    arg_packages_setup(ts->aps);
    if (arg_packages_size(ts->aps) > 0) {
        tn_array *pkgs = arg_packages_resolve(ts->aps, ts->ctx->ps->pkgs, 0);
        if (pkgs == NULL)
            rc = 0;
        else {
            rc = pkgset_mark_packages(ts->ctx->ps, pkgs, withdeps,
                                      ts->getop_v(ts, POLDEK_OP_NODEPS,
                                                  POLDEK_OP_FORCE, 0));
            n_array_free(pkgs);
        }
    }
    
    return rc;
}


/*
 * Dist Instalation
 */ 
struct inf {
    int       npackages;
    double    nbytes;
    double    nfbytes; 
};


static void is_marked_mapfn(struct pkg *pkg, struct inf *inf) 
{
    if (pkg_is_marked(pkg)) {
        inf->npackages++;
        inf->nbytes += pkg->size;
        inf->nfbytes += pkg->fsize;
    }
}

static
int do_install_dist(struct poldek_ts *ts)
{
    int               i, ninstalled, nerr, is_remote = -1;
    double            ninstalled_bytes; 
    struct inf        inf;
    char              tmpdir[PATH_MAX];
    
    n_assert(ts->db->rootdir);
    if (!is_rwxdir(ts->db->rootdir)) {
        logn(LOGERR, "access %s: %m", ts->db->rootdir);
        return 0;
    }
    
    unsetenv("TMPDIR");
    unsetenv("TMP");

    snprintf(tmpdir, sizeof(tmpdir), "%s/tmp", ts->db->rootdir);
    mkdir(tmpdir, 0755);
    rpm_define("_tmpdir", "/tmp");
    rpm_define("_tmppath", "/tmp");
    rpm_define("tmppath", "/tmp");
    rpm_define("tmpdir", "/tmp");
    
    nerr = 0;
    ninstalled = 0;
    ninstalled_bytes = 0;
    
    memset(&inf, 0, sizeof(inf));
    n_array_map_arg(ts->ctx->ps->pkgs, (tn_fn_map2)is_marked_mapfn, &inf);

    for (i=0; i<n_array_size(ts->ctx->ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ts->ctx->ps->ordered_pkgs, i);
        
        if (pkg_is_marked(pkg)) {
            char *pkgpath = pkg_path_s(pkg);

            if (is_remote == -1)
                is_remote = vf_url_type(pkgpath) & VFURL_REMOTE;
            
            if (verbose > 1) {
                char *p = pkg_is_hand_marked(pkg) ? "" : "dep";
                if (pkg_has_badreqs(pkg)) 
                    msg(2, "not%sInstall %s\n", p, pkg->name);
                else
                    msg(2, "%sInstall %s\n", p, pkgpath);
            }

            if (ts->getop(ts, POLDEK_OP_TEST))
                continue;
            
            if (pkgdb_install(ts->db, pkgpath, ts))
                logn(LOGNOTICE | LOGFILE, "INST-OK %s", pkg->name);
            
            else {
                logn(LOGERR | LOGFILE, "INST-ERR %s", pkg->name);
                nerr++;
            }
            
            ninstalled++;
            ninstalled_bytes += pkg->size;
            inf.nfbytes -= pkg->fsize;
            printf_c(PRCOLOR_YELLOW,
                     _(" %d of %d (%.2f of %.2f MB) packages done"),
                     ninstalled, inf.npackages,
                     ninstalled_bytes/(1024*1000), 
                     inf.nbytes/(1024*1000));

            if (is_remote)
                printf_c(PRCOLOR_YELLOW, _("; %.2f MB to download"),
                         inf.nfbytes/(1024*1000));
            printf_c(PRCOLOR_YELLOW, "\n");
        }
    }
    
    if (nerr) 
        logn(LOGERR, _("there were errors during install"));
    
    return nerr == 0;
}


static
int mkdbdir(const char *rootdir) 
{
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "%s%s", rootdir, "/var");
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        logn(LOGERR, "mkdir %s: %m", path);
        return 0;
    }
    
    snprintf(path, sizeof(path), "%s%s", rootdir, "/var/lib");
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        logn(LOGERR, "mkdir %s: %m", path);
        return 0;
    }

    snprintf(path, sizeof(path), "%s%s", rootdir, "/var/lib/rpm");
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        logn(LOGERR, "mkdir %s: %m", path);
        return 0;
    }

    return 1;
}

static void cp_str_ifnull(char **dst, const char *src)
{
    if (*dst == NULL && src)
        *dst = n_strdup(src);
}

static void cp_arr_ifnull(tn_array **dst, tn_array *src)
{
    if ((*dst == NULL || n_array_size(*dst) == 0) && n_array_size(src)) {
        if (*dst)
            n_array_free(*dst);
        *dst = n_ref(src);
    }
}


static int ts_prerun(struct poldek_ts *ts, struct install_info *iinf) 
{
    int rc = 1;

    n_assert(ts->ctx);
    n_assert(poldek__is_setup_done(ts->ctx));

    if (ts->_iflags & TS_CONFIG_LATER)
        poldek__apply_tsconfig(ts->ctx, ts);
              
    cp_str_ifnull(&ts->rootdir, ts->ctx->ts->rootdir);
    cp_str_ifnull(&ts->fetchdir, ts->ctx->ts->fetchdir);
    cp_str_ifnull(&ts->cachedir, ts->ctx->ts->cachedir);
    cp_str_ifnull(&ts->dumpfile, ts->ctx->ts->dumpfile);
    cp_str_ifnull(&ts->prifile, ts->ctx->ts->prifile);

    cp_arr_ifnull(&ts->rpmacros, ts->ctx->ts->rpmacros);
    cp_arr_ifnull(&ts->rpmopts, ts->ctx->ts->rpmopts);
    dbgf_("%p->%p, %p->%p\n", ts, ts->hold_patterns, ts->ctx->ts,
          ts->ctx->ts->hold_patterns);
    cp_arr_ifnull(&ts->hold_patterns, ts->ctx->ts->hold_patterns);
    cp_arr_ifnull(&ts->ign_patterns, ts->ctx->ts->ign_patterns);
    
    if (ts->rootdir == NULL)
        ts->rootdir = n_strdup("/");
    
    if (ts->getop(ts, POLDEK_OP_RPMTEST)) {
        if (verbose < 1)
            verbose += 1;
        
    } else if (ts->getop_v(ts, POLDEK_OP_JUSTFETCH, POLDEK_OP_JUSTPRINT, 0)) {
        if (!is_rwxdir(ts->rootdir)) {
            logn(LOGERR, "%s: %m", ts->rootdir);
            rc = 0;
        }
    }
    
    if (ts->getop(ts, POLDEK_OP_MKDBDIR)) {
        if (!mkdbdir(ts->rootdir))
            rc = 0;
    }
    
    if (!rc)
        return rc;
    
    rc = arg_packages_setup(ts->aps);
    
    if (rc && iinf)
        install_info_init(iinf);
    
    if (poldek_ts_issetf(ts, POLDEK_TS_INSTALL))
        poldek_load_sources(ts->ctx);
    
    return rc;
}
        
        
static
int ts_run_install_dist(struct poldek_ts *ts) 
{
    int rc;

    n_assert(poldek_ts_issetf(ts, POLDEK_TS_INSTALL));
    
    if (!ts_prerun(ts, NULL))
        return 0;

    if (!ts_mark_arg_packages(ts, 1))
        return 0;
    
    if (ts->getop(ts, POLDEK_OP_RPMTEST))
        ts->db = pkgdb_open(ts->rootdir, NULL, O_RDONLY);
    else 
        ts->db = pkgdb_creat(ts->rootdir, NULL);
    
    if (ts->db == NULL) 
        return 0;
    
    rc = do_install_dist(ts);
    pkgdb_free(ts->db);
    ts->db = NULL;
    return rc;
}


static
int ts_run_upgrade_dist(struct poldek_ts *ts) 
{
    int rc;

    n_assert(poldek_ts_issetf(ts, POLDEK_TS_UPGRADE));
    
    if (!ts_mark_arg_packages(ts, 1))
        return 0;

    ts->db = pkgdb_open(ts->rootdir, NULL, O_RDONLY);
    if (ts->db == NULL)
        return 0;
    
    rc = do_poldek_ts_upgrade_dist(ts);
    pkgdb_free(ts->db);
    ts->db = NULL;
    return rc;
}



static        
int ts_run_install(struct poldek_ts *ts, struct install_info *iinf) 
{
    int rc;

    n_assert(poldek_ts_issetf(ts, POLDEK_TS_INSTALL));
    if (!ts_mark_arg_packages(ts, 0))
        return 0;

    ts->db = pkgdb_open(ts->rootdir, NULL, O_RDONLY);
    if (ts->db == NULL)
        return 0;
    printf("poldek_ts_do_install0 %d\n", arg_packages_size(ts->aps));
    rc = do_poldek_ts_install(ts, iinf);
    pkgdb_free(ts->db);
    ts->db = NULL;
    return rc;
}


static
int ts_run_uninstall(struct poldek_ts *ts, struct install_info *iinf) 
{
    int rc;

    n_assert(poldek_ts_issetf(ts, POLDEK_TS_UNINSTALL));

    printf("poldek_ts_do_uninstall %d\n", arg_packages_size(ts->aps));
    
    ts->db = pkgdb_open(ts->rootdir, NULL, O_RDONLY);
    if (ts->db == NULL)
        return 0;
    
    rc = do_poldek_ts_uninstall(ts, iinf);
    pkgdb_free(ts->db);
    ts->db = NULL;
    return rc;
}


int poldek_ts_run(struct poldek_ts *ts, struct install_info *iinf)
{
    int rc = 0;

    if (!ts_prerun(ts, iinf))
        return 0;
    
    if (poldek_ts_issetf(ts, POLDEK_TS_UNINSTALL)) {
        rc = ts_run_uninstall(ts, iinf);
        
    } else if (poldek_ts_issetf(ts, POLDEK_TS_INSTALLDIST)) {
        rc = ts_run_install_dist(ts);

    } else if (poldek_ts_issetf(ts, POLDEK_TS_UPGRADE) &&
               poldek_ts_issetf(ts, POLDEK_TS_DIST)) {
        rc = ts_run_upgrade_dist(ts);
        
    } else if (poldek_ts_issetf(ts, POLDEK_TS_INSTALL | POLDEK_TS_UPGRADE)) {
        rc = ts_run_install(ts, iinf);
        
    } else {
        printf("f = %x\n", ts->_flags);
        n_assert(0);
    }

    return rc;
}


