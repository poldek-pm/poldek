/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
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
#include <obstack.h>
#include <fnmatch.h>

#include <rpm/rpmlib.h>
#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/nstr.h>
#include <vfile/vfile.h>

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"
#include "usrset.h"
#include "rpmadds.h"
#include "pkgset-req.h"
#include "split.h"
#include "term.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/* prototypes from ask.c */
int ask_yn(int default_a, const char *fmt, ...);
int ask_pkg(const char *virtual, struct pkg **pkgs);

#define obstack_chunk_alloc malloc
#define obstack_chunk_free  free

struct obstack_s {
    int ucnt;
    struct obstack ob;
};

static struct obstack_s idx_obs;  /* for indexes */
static struct obstack_s pkg_obs;  /* for packages */


static
void obstacks_init(void) 
{
    n_assert(idx_obs.ucnt == 0);
    
    if (idx_obs.ucnt)
        idx_obs.ucnt++;
    else {
        obstack_init(&idx_obs.ob);
        obstack_chunk_size(&idx_obs.ob) = 1024*128;
        idx_obs.ucnt++;
    }

    if (pkg_obs.ucnt)
        pkg_obs.ucnt++;
    else {
        obstack_init(&pkg_obs.ob);
        obstack_chunk_size(&pkg_obs.ob) = 1024*128;
        pkg_obs.ucnt++;
    }
}


static
void obstacks_free(void) 
{
    if (--idx_obs.ucnt <= 0) 
        obstack_free(&idx_obs.ob, NULL);

    if (--pkg_obs.ucnt <= 0) 
        obstack_free(&pkg_obs.ob, NULL);
    
    idx_obs.ucnt = 0;
}


void *pkg_alloc(size_t size) 
{
    return obstack_alloc(&pkg_obs.ob, size);
}


static
void *idx_alloc(size_t size) 
{
    return obstack_alloc(&idx_obs.ob, size);
}

static
void fake_free(void *p)     /* do nothing */
{
    p = p;
}

int pkgsetmodule_init(void) 
{
    idx_obs.ucnt = 0;
    pkg_obs.ucnt = 0;
    obstacks_init();
    // disabled: set_pkg_allocfn(pkg_alloc, fake_free);
    set_capreq_allocfn(idx_alloc, fake_free, NULL, NULL);
    return 1;
}


void pkgsetmodule_destroy(void) 
{
    //disabled: set_pkg_allocfn(pkg_alloc, fake_free);
    set_capreq_allocfn(idx_alloc, fake_free, NULL, NULL);
    obstacks_free();
}


void inst_s_init(struct inst_s *inst)
{
    inst->flags = INSTS_FOLLOW;
    inst->db = NULL;
    
    inst->instflags = 0;
    inst->rootdir = NULL;
    inst->fetchdir = NULL;
    inst->cachedir = setup_cachedir();
    inst->dumpfile = NULL;
    inst->rpmopts = NULL;
    inst->rpmacros = NULL;
    inst->askpkg_fn = ask_pkg;
    inst->ask_fn = ask_yn;
    inst->rpmacros = n_array_new(2, NULL, NULL);
    inst->rpmopts = n_array_new(4, NULL, (tn_fn_cmp)strcmp);
    inst->hold_pkgnames = n_array_new(4, free, (tn_fn_cmp)strcmp);
}




static tn_array *get_rpmlibcaps(void) 
{
    char **names = NULL, **versions = NULL, *evr;
    int *flags = NULL, n, i;
    tn_array *caps;
    
#if HAVE_RPMGETRPMLIBPROVIDES
    n = rpmGetRpmlibProvides((const char ***)&names, &flags, (const char ***)&versions);
#else
    return capreq_arr_new();
#endif    
    if (n <= 0)
        return NULL;

    caps = capreq_arr_new(0);
    
    evr = alloca(128);
    
    for (i=0; i<n; i++) {
        struct capreq *cr;

        n_assert(flags[i] & RPMSENSE_EQUAL);
        n_assert(!(flags[i] & (RPMSENSE_LESS | RPMSENSE_GREATER)));

        n_strncpy(evr, versions[i], 128);
        cr = capreq_new_evr(names[i], evr, REL_EQ, 0);
        n_array_push(caps, cr);
    }

    if (names)
        free(names);
    
    if (flags)
        free(flags);

    if (versions)
        free(versions);
    
    n_array_sort(caps);
    return caps;
}


struct pkgset *pkgset_new(unsigned optflags)
{
    struct pkgset *ps;
    
    ps = pkg_alloc(sizeof(*ps));
    memset(ps, 0, sizeof(*ps));
    ps->pkgs = pkgs_array_new(1024);
    ps->ordered_pkgs = NULL;
    
    /* just merge pkgdirs->depdirs */
    ps->depdirs = n_array_new(64, NULL, (tn_fn_cmp)strcmp);
    n_array_ctl(ps->depdirs, TN_ARRAY_AUTOSORTED);
    
    
    ps->pkgdirs = n_array_new(4, (tn_fn_free)pkgdir_free, NULL);
    ps->flags = optflags;
    ps->rpmcaps = get_rpmlibcaps();
    return ps;
}


void pkgset_free(struct pkgset *ps) 
{
    if (ps->flags & _PKGSET_INDEXES_INIT) {
        capreq_idx_destroy(&ps->cap_idx);
        capreq_idx_destroy(&ps->req_idx);
        capreq_idx_destroy(&ps->obs_idx);
        file_index_destroy(&ps->file_idx);
        ps->flags &= (unsigned)~_PKGSET_INDEXES_INIT;
    }

    if (ps->depdirs) {
        n_array_free(ps->depdirs);
        ps->depdirs = NULL;
    }

    if (ps->pkgdirs) {
        n_array_free(ps->pkgdirs);
        ps->pkgdirs = NULL;
    }

    if (ps->ordered_pkgs) {
        n_array_free(ps->ordered_pkgs);
        ps->ordered_pkgs = NULL;
    }

    if (ps->rpmcaps) {
        n_array_free(ps->rpmcaps);
        ps->rpmcaps = NULL;
    }

    n_array_free(ps->pkgs);
}


static void mapfn_free_pkgfl(struct pkg *pkg) 
{
    if (pkg->fl)
        n_array_free(pkg->fl);
    pkg->fl = NULL;
}


void pkgset_free_indexes(struct pkgset *ps) 
{
    if (ps->flags & _PKGSET_INDEXES_INIT) {
        capreq_idx_destroy(&ps->cap_idx);
        capreq_idx_destroy(&ps->req_idx);
        capreq_idx_destroy(&ps->obs_idx);
        file_index_destroy(&ps->file_idx);
        
        ps->flags &= (unsigned)~_PKGSET_INDEXES_INIT;
    }

    n_array_map(ps->pkgs, (tn_fn_map1)mapfn_free_pkgfl);
}


int pkgset_has_errors(struct pkgset *ps) 
{
    int rc;

    rc = ps->nerrors;
    ps->nerrors = 0;
    return rc;
}

static void sort_pkg_caps(struct pkg *pkg) 
{
    if (pkg->caps)
        n_array_sort(pkg->caps);
}

static
void add_self_cap(struct pkgset *ps) 
{
    n_assert(ps->pkgs);
    n_array_map(ps->pkgs, (tn_fn_map1)pkg_add_selfcap);
}


static
int pkgfl2fidx(const struct pkg *pkg, struct file_index *fidx)
{
    int i, j;

    if (pkg->fl == NULL)
        return 1;
    
    for (i=0; i<n_array_size(pkg->fl); i++) {
        struct pkgfl_ent *flent;
        void *fidx_dir;
        
        flent = n_array_nth(pkg->fl, i);
        fidx_dir = file_index_add_dirname(fidx, flent->dirname);
        for (j=0; j<flent->items; j++) {
            file_index_add_basename(fidx, fidx_dir,
                                    flent->files[j], (struct pkg*)pkg);
        }
        	
    }
    return 1;
}

static 
int pkgset_index(struct pkgset *ps) 
{
    int i, j;
    
    msg(2, "Indexing...\n");
    add_self_cap(ps);
    n_array_map(ps->pkgs, (tn_fn_map1)sort_pkg_caps);

    /* build indexes */
    capreq_idx_init(&ps->cap_idx, 10003);
    capreq_idx_init(&ps->req_idx, 10003);
    capreq_idx_init(&ps->obs_idx, 103);
    file_index_init(&ps->file_idx, 100003);
    ps->flags |= _PKGSET_INDEXES_INIT;

    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);

#if 0                           /* testing sruff */
        if (strcmp(pkg->name, "SVGATextMode") == 0) {
            struct capreq *req = capreq_new_evr("ghostscript-fonts-std",
                                                strdup("6.0-7"), REL_LT, CAPREQ_CNFL);
            if (pkg->cnfls == NULL) 
                pkg->cnfls = capreq_arr_new(2);

            n_array_push(pkg->cnfls, req);
        }

        

        if (strcmp(pkg->name, "libltdl-devel") == 0) {
            struct capreq *req = capreq_new("postfix", 0, 0, 0, 0, CAPREQ_OBCNFL);
            if (pkg->cnfls == NULL) 
                pkg->cnfls = capreq_arr_new(2);
            n_array_push(pkg->cnfls, req);
        }

        if (strcmp(pkg->name, "exim") == 0) {
            struct capreq *req = capreq_new("wget", 0, 0, 0, 0, CAPREQ_REQ);
           if (pkg->reqs == NULL) 
                pkg->reqs = capreq_arr_new(2);

            n_array_push(pkg->reqs, req);
        }
        
        if (strcmp(pkg->name, "libxml") == 0) {
            struct capreq *req = capreq_new("postfix", 0, 0, 0, 0, CAPREQ_OBCNFL);
            if (pkg->cnfls == NULL) 
                pkg->cnfls = capreq_arr_new(2);

            n_array_push(pkg->cnfls, req);
        }


        if (strcmp(pkg->name, "wget") == 0) {
            struct capreq *req = capreq_new("exim", 0, 0, 0, 0, CAPREQ_OBCNFL);
            if (pkg->cnfls == NULL)
                pkg->cnfls = capreq_arr_new(2);

            n_array_push(pkg->cnfls, req);
        }
#endif

        if (i % 200 == 0) 
            msg(3, " %d..\n", i);
        if (pkg->caps)
            for (j=0; j<n_array_size(pkg->caps); j++) {
                struct capreq *cap = n_array_nth(pkg->caps, j);
                capreq_idx_add(&ps->cap_idx, capreq_name(cap), pkg, 1);
            }

        if (pkg->reqs)
            for (j=0; j<n_array_size(pkg->reqs); j++) {
                struct capreq *req = n_array_nth(pkg->reqs, j);
                capreq_idx_add(&ps->req_idx, capreq_name(req), pkg, 0);
            }

        if (pkg->cnfls)
            for (j=0; j<n_array_size(pkg->cnfls); j++) {
                struct capreq *cnfl = n_array_nth(pkg->cnfls, j);
                if (cnfl_is_obsl(cnfl))
                    capreq_idx_add(&ps->obs_idx, capreq_name(cnfl), pkg, 0);
            }
        
        pkgfl2fidx(pkg, &ps->file_idx);
    }
    
    file_index_setup(&ps->file_idx);
    msg(3, " ..%d done\n", i);
    
    return 0;
}

#if 0
static void mapfn_clean_pkg_color(struct pkg *pkg) 
{
    pkg_set_color(pkg, PKG_COLOR_WHITE);
}
#endif


static void set_priorities(tn_array *pkgs, const char *pri_fpath) 
{
    if (pri_fpath == NULL) {
        pri_fpath = "/etc/poldek-pkgsplit.conf";
        if (access(pri_fpath, R_OK) != 0) 
            pri_fpath = "/etc/poldek-pri.conf";
    }

    if (access(pri_fpath, R_OK) == 0) 
        packages_set_priorities(pkgs, pri_fpath);
}

int pkgset_setup(struct pkgset *ps, const char *pri_fpath) 
{
    int n;
    int strict;
    
    strict = ps->flags & PSVERIFY_MERCY ? 0 : 1;

    n = n_array_size(ps->pkgs);
    n_array_sort(ps->pkgs);

    if ((ps->flags & PSMODE_MKIDX) == 0) {
        n_array_uniq_ex(ps->pkgs, (tn_fn_cmp)pkg_cmp_uniq);
        
        if (n != n_array_size(ps->pkgs)) {
            n -= n_array_size(ps->pkgs);
            msgn(1, ngettext(
                "Removed %d duplicate package from available set",
                "Removed %d duplicate packages from available set", n), n);
        }
    }
    
        
    pkgset_index(ps);
    mem_info(1, "MEM after index");

    if (ps->flags & PSVERIFY_FILECNFLS) {
        msgn(1, _("\nVerifying files conflicts..."));
        file_index_find_conflicts(&ps->file_idx, strict);
    }

    pkgset_verify_deps(ps, strict);
    mem_info(1, "MEM after verify deps");

    if (ps->flags & PSVERIFY_CNFLS)
        msgn(1, _("\nVerifying packages conflicts..."));
    pkgset_verify_conflicts(ps, strict);

    set_priorities(ps->pkgs, pri_fpath);
    pkgset_order(ps);
    mem_info(1, "MEM after order");

    //set_pkg_allocfn(malloc, free);
    set_capreq_allocfn(malloc, free, NULL, NULL);
    return ps->nerrors == 0;
}

    
/*
 * Instalation
 */ 
static void visit_mark_reqs(struct pkg *parent_pkg, struct pkg *pkg, int deep) 
{
    int i;
    
    if (pkg_is_dep_marked(pkg))
        return;
    
    if (pkg_isnot_marked(pkg)) {
        n_assert(parent_pkg != NULL);
        msgn_i(1, deep, _("%s marks %s"), pkg_snprintf_s(parent_pkg),
               pkg_snprintf_s0(pkg));
        pkg_dep_mark(pkg);
    }
    
    deep += 2;
    if (pkg->reqpkgs) {
        for (i=0; i<n_array_size(pkg->reqpkgs); i++) {
            struct reqpkg *rpkg = n_array_nth(pkg->reqpkgs, i);

            if (pkg_is_marked(rpkg->pkg))
                continue;
            
            if (!pkg_is_dep_marked(rpkg->pkg)) {
                int markit = 1;
                
                if (rpkg->flags & REQPKG_MULTI) {
                    struct reqpkg *eqpkg;
                    int n;

                    n = 1;
                    eqpkg = rpkg->adds[0];
                    while (eqpkg) {
                        if (pkg_is_marked(eqpkg->pkg)) {
                            markit = 0;
                            break;
                        }
                        eqpkg = rpkg->adds[n++];
                    }
                }

                if (markit)
                    visit_mark_reqs(pkg, rpkg->pkg, deep);
            }
        }
    }
}

static 
int mark_dependencies(struct pkgset *ps, unsigned instflags) 
{
    int i, j;
    int req_nerr = 0, cnfl_nerr = 0;

    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
    
        if (pkg_is_hand_marked(pkg)) 
            visit_mark_reqs(NULL, pkg, 0);
    }

    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);

        if (pkg_isnot_marked(pkg))
            continue;
        
        if (pkg_has_badreqs(pkg)) {
            logn(LOGERR, _("%s: broken dependencies"), pkg_snprintf_s(pkg));
            req_nerr++;
        }
        
        if (pkg->cnflpkgs == NULL)
            continue;

        for (j=0; j<n_array_size(pkg->cnflpkgs); j++) {
            struct cnflpkg *cpkg = n_array_nth(pkg->cnflpkgs, j);
            if (pkg_is_marked(cpkg->pkg)) {
                logn(LOGERR, _("conflict between %s and %s"), pkg->name,
                     cpkg->pkg->name);
                cnfl_nerr++;
            }
        }
    }

    if (instflags & PKGINST_NODEPS)
        req_nerr = 0;

    if (instflags & PKGINST_FORCE)
        cnfl_nerr = 0;
    
    return (cnfl_nerr + req_nerr) == 0;
}

#if 0
/*
  function prepares directory where downloaded packages will be stored,
  used only for install-dist
 */
static int setup_tmpdir(const char *rootdir) 
{
    char path[PATH_MAX];
    
    if (!is_rwxdir(rootdir)) {
        logn(LOGERR, "access %s: %m", rootdir);
        return 0;
    }

    snprintf(path, sizeof(path), "%s%s", rootdir, tmpdir());

    n_assert (*path != '\0');

    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        logn(LOGERR, "mkdir %s: %m", path);
        return 0;
    }
    
    if (!is_rwxdir(path)) {
        logn(LOGERR, "access %s: %m", path);
        return 0;
    }
    
    vfile_configure(path, -1);
    return 1;
}
#endif

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
    

int pkgset_install_dist(struct pkgset *ps, struct inst_s *inst)
{
    int               i, ninstalled, nerr, is_remote = -1;
    double            ninstalled_bytes; 
    struct inf        inf;
    char              tmpdir[PATH_MAX];
    
    n_assert(inst->db->rootdir);
    if (!is_rwxdir(inst->db->rootdir)) {
        logn(LOGERR, "access %s: %m", inst->db->rootdir);
        return 0;
    }
    
    unsetenv("TMPDIR");
    unsetenv("TMP");

    snprintf(tmpdir, sizeof(tmpdir), "%s/tmp", inst->db->rootdir);
    mkdir(tmpdir, 0755);
    rpm_define("_tmpdir", "/tmp");
    rpm_define("_tmppath", "/tmp");
    rpm_define("tmppath", "/tmp");
    rpm_define("tmpdir", "/tmp");
    
    if (!mark_dependencies(ps, inst->instflags))
        return 0;

    
    nerr = 0;
    ninstalled = 0;
    ninstalled_bytes = 0;
    
    memset(&inf, 0, sizeof(inf));
    n_array_map_arg(ps->pkgs, (tn_fn_map2)is_marked_mapfn, &inf);

    for (i=0; i<n_array_size(ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
        
        if (pkg_is_marked(pkg)) {
            char *pkgpath = pkg_path_s(pkg);

            if (is_remote == -1)
                is_remote = vfile_url_type(pkgpath) & VFURL_REMOTE;
            
            if (verbose > 1) {
                char *p = pkg_is_hand_marked(pkg) ? "" : "dep";
                if (pkg_has_badreqs(pkg)) 
                    msg(2, "not%sInstall %s\n", p, pkg->name);
                else
                    msg(2, "%sInstall %s\n", p, pkgpath);
            }

            if (inst->instflags & PKGINST_TEST)
                continue;
            
            if (!pkgdb_install(inst->db, pkgpath,
                               inst->instflags | PKGINST_NODEPS)) 
                nerr++;

            
            ninstalled++;
            ninstalled_bytes += pkg->size;
            inf.nfbytes -= pkg->fsize;
            printf_c(PRCOLOR_YELLOW,
                     _(" %d of %d (%.2f of %.2f MB) packages done"),
                     ninstalled, inf.npackages,
                     ninstalled_bytes/(1024*1000), 
                     inf.nbytes/(1024*1000));

            if (is_remote)
                printf_c(PRCOLOR_YELLOW, _("; (%.2f MB left to download)"),
                         inf.nfbytes/(1024*1000));
            printf_c(PRCOLOR_YELLOW, "\n");
        }
    }
    
    if (nerr) 
        logn(LOGERR, _("there were errors during install"));
    
    return nerr == 0;
}


static void mapfn_mark(struct pkg *pkg, unsigned *flags) 
{
    n_assert(flags);
    
    if (*flags & PS_MARK_OFF_ALL) {
        if ((*flags & PS_MARK_ON_INTERNAL) && pkg_is_marked(pkg))
            pkg_mark_i(pkg);
        pkg_unmark(pkg);
        
    } else if (*flags & PS_MARK_OFF_DEPS) {
        if ((*flags & PS_MARK_ON_INTERNAL)) 
            if (pkg_is_dep_marked(pkg))
                pkg_mark_i(pkg);

        if (pkg_is_dep_marked(pkg))
            pkg_unmark(pkg);
    }
}


void pkgset_mark(struct pkgset *ps, unsigned flags) 
{
    if (ps->pkgs) 
        n_array_map_arg(ps->pkgs, (tn_fn_map2) mapfn_mark, &flags);
    
}


inline static int mark_package(struct pkg *pkg, int nodeps)
{
    if (pkg_has_badreqs(pkg) && nodeps == 0) {
        logn(LOGERR, _("mark: %s: broken dependencies"), pkg_snprintf_s(pkg));
        
    } else {
        pkg_hand_mark(pkg);
        msgn(1, _("mark %s"), pkg_snprintf_s(pkg));
    }
    return pkg_is_marked(pkg);
}


tn_array *pkgset_getpkgs(const struct pkgset *ps) 
{
    int i;
    tn_array *pkgs;
    
    pkgs = n_array_new(n_array_size(ps->pkgs), NULL, (tn_fn_cmp)pkg_cmp_name);

    for (i=0; i<n_array_size(ps->pkgs); i++) 
        n_array_push(pkgs, n_array_nth(ps->pkgs, i));

    n_array_sort(pkgs);
    return pkgs;
}


static
int pkg_match_pkgdef(const struct pkg *pkg, const struct pkgdef *pdef) 
{
    int rc = 1;
    
    if (pdef->pkg->epoch && pkg->epoch != pdef->pkg->epoch)
        rc = 0;
    
    if (rc && *pdef->pkg->ver) 
        if (strcmp(pdef->pkg->ver, pkg->ver) != 0) 
            rc = 0;
    
    if (rc && *pdef->pkg->rel)
        if (strcmp(pdef->pkg->rel, pkg->rel) != 0)
            rc = 0;
#if 0    
    msgn(1, "MATCH %d e%d e%d %s %s", rc, 
        pkg->epoch, pdef->pkg->epoch,
        pkg_snprintf_s(pkg),
        pkg_snprintf_s0(pdef->pkg));
#endif    
    return rc;
}

static
int pkgset_mark_pkgdef_exact(struct pkgset *ps, const struct pkgdef *pdef,
                             int nodeps) 
{
    int i, marked = 0, matched = 0;
    struct pkg *pkg, tmpkg, *findedpkg;
    
    
    n_assert(pdef->pkg != NULL);
    
    tmpkg.name = pdef->pkg->name;
    n_array_sort(ps->pkgs);

    i = n_array_bsearch_idx_ex(ps->pkgs, pdef->pkg, (tn_fn_cmp)pkg_cmp_name); 
    if (i < 0) {
        logn(LOGERR, _("mark: %s not found"), pdef->pkg->name);
        return 0;
    }
    
    findedpkg = pkg = n_array_nth(ps->pkgs, i);
    
    if (pkg_match_pkgdef(pkg, pdef)) {
        marked = mark_package(pkg, nodeps);
        matched = 1;
        
    } else {
        i++;
        while (i < n_array_size(ps->pkgs)) {
            pkg = n_array_nth(ps->pkgs, i++);
            
            if (strcmp(pkg->name, pdef->pkg->name) != 0) 
                break;
            
            if (pkg_match_pkgdef(pkg, pdef)) {
                marked = mark_package(pkg, nodeps);
                matched = 1;
                break;
            }
        }
    }

    if (!marked && !matched) 
        logn(LOGERR, _("mark: %s: versions not match"), pdef->pkg->name);
    
    return marked;
}


static
int pkgset_mark_pkgdefs_patterns(struct pkgset *ps, tn_array *pkgdefs,
                                 int nodeps) 
{
    int i, j, nerr = 0;
    int *matches;


    matches = alloca(n_array_size(pkgdefs) * sizeof(*matches));
    memset(matches, 0, n_array_size(pkgdefs) * sizeof(*matches));
    
    
    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
        
        for (j=0; j<n_array_size(pkgdefs); j++) {
            struct pkgdef *pdef = n_array_nth(pkgdefs, j);
            if (pdef->tflags != PKGDEF_PATTERN)
                continue;

            n_assert(pdef->pkg != NULL);
            if (fnmatch(pdef->pkg->name, pkg->name, 0) == 0)
                matches[j] += mark_package(pkg, nodeps);
        }
    }

    for (j=0; j<n_array_size(pkgdefs); j++) {
        struct pkgdef *pdef = n_array_nth(pkgdefs, j);
        if (pdef->tflags != PKGDEF_PATTERN)
            continue;
        
        if (matches[j] == 0) {
            logn(LOGERR, _("mark: %s: no such package found"), pdef->pkg->name);
            nerr++;
        }
    }
    
    return nerr;
}



int pkgset_mark_usrset(struct pkgset *ps, struct usrpkgset *ups,
                       struct inst_s *inst, int markflag)
{
    int i, nerr = 0, nodeps, npatterns = 0;

    pkgset_mark(ps, PS_MARK_OFF_ALL);

    if (ps->flags & PSMODE_INSTALL_DIST)
        nodeps = inst->instflags & PKGINST_NODEPS;
    else
        nodeps = 1;
    
    for (i=0; i<n_array_size(ups->pkgdefs); i++) {
        struct pkgdef *pdef = n_array_nth(ups->pkgdefs, i);

        if (pdef->tflags & (PKGDEF_REGNAME | PKGDEF_PKGFILE)) { 
            if (!pkgset_mark_pkgdef_exact(ps, pdef, nodeps))
                nerr++;
            
        } else if (pdef->tflags & PKGDEF_PATTERN) {
            npatterns++;

        } else if (pdef->tflags & PKGDEF_VIRTUAL) { /* VIRTUAL implies OPTIONAL */
            tn_array *avpkgs;
                
            if (pdef->pkg == NULL) {
                logn(LOGERR, _("virtual %s: default package expected"),
                    pdef->virtname);
                nerr++;
                    
            }

            avpkgs = pkgset_lookup_cap(ps, pdef->virtname);
            if (avpkgs == NULL || n_array_size(avpkgs) == 0) {
                logn(LOGERR, _("virtual %s not found"), pdef->virtname);
                nerr++;
                    
            } else {
                struct pkg *pkg;
                int n = 0;
                
                pkg = n_array_nth(avpkgs, 0);
                n = n;
#if 0                           /* NFY */
                n = inst->askpkg_fn(pdef->virtname, avpkgs);
                pkg = n_array_nth(avpkgs, n);
#endif                
                    
                if (pdef->pkg == NULL) {
                    logn(LOGWARN, _("%s: missing default package, using %s"),
                        pdef->virtname, pkg->name);
                        
                } else {
                    int i, found = 0;
                        
                    for (i=0; i<n_array_size(avpkgs); i++) {
                        pkg = n_array_nth(avpkgs, i);
                        if (strcmp(pkg->name, pdef->pkg->name) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    
                    if (found == 0) {
                        pkg = n_array_nth(avpkgs, 0);
                        logn(LOGWARN, _("%s: default package %s not found, "
                                        "using %s"), pdef->virtname,
                             pdef->pkg->name, pkg->name);
                    }
                } 
                            
                if (!mark_package(pkg, nodeps))
                    nerr++;
            }
                
            if (avpkgs)
                n_array_free(avpkgs);
            
        } else if (pdef->tflags & PKGDEF_OPTIONAL) {
#if 0                        /* NFY */
            if ((inst->flags & INSTS_CONFIRM_INST) && inst->ask_fn &&
                inst->ask_fn(0, "Install %s? [y/N]", pdef->pkg->name));
#endif                
            if (!pkgset_mark_pkgdef_exact(ps, pdef, nodeps))
                nerr++;
            
        } else {
            n_assert(0);
        }
    }

    if (npatterns)
        nerr += pkgset_mark_pkgdefs_patterns(ps, ups->pkgdefs, nodeps);
    
    if (markflag == MARK_DEPS) {
        msgn(1, _("\nProcessing dependencies..."));
        if (!mark_dependencies(ps, nodeps))
            nerr++;
        
        if (nerr) {
            pkgset_mark(ps, PS_MARK_OFF_ALL);
            logn(LOGERR, _("Buggy package set."));
            
        } else {
            msgn(1, _("Package set OK"));
        }
    }
    
    
    return nerr == 0;
}


tn_array *pkgset_lookup_cap(struct pkgset *ps, const char *capname)
{
    const struct capreq_idx_ent *ent;
    tn_array *pkgs = NULL;
    
    if ((ent = capreq_idx_lookup(&ps->cap_idx, capname))) {
        int i;
        
        pkgs = n_array_new(ent->items, NULL, (tn_fn_cmp)pkg_cmp_name_evr_rev);
        for (i=0; i<ent->items; i++) 
            n_array_push(pkgs, ent->pkgs[i]);

        if (n_array_size(pkgs) == 0) {
            n_array_free(pkgs);
            pkgs = NULL;
        }
    }

    return pkgs;
}

int pkgset_dump_marked_fqpns(struct pkgset *ps, const char *dumpfile)
{
    int i;
    FILE *stream = stdout;

    if (dumpfile) {
        if ((stream = fopen(dumpfile, "w")) == NULL) {
            logn(LOGERR, "fopen %s: %m", dumpfile);
            return 0;
        }
        fprintf(stream, "# Packages to install (in the right order)\n");
    }
    
    for (i=0; i<n_array_size(ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
        if (pkg_is_marked(pkg))
            fprintf(stream, "%s\n", pkg_filename_s(pkg));
    }
    
    return 1;
}
