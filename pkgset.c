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

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <obstack.h>
#include <fnmatch.h>

#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/nstr.h>

#include <vfile/vfile.h>

#include "rpm/rpm.h"
#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"
#include "usrset.h"
#include "pkgset-req.h"
#include "split.h"
#include "poldek_term.h"

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/* prototypes from ask.c */
int ask_yn(int default_a, const char *fmt, ...);
int ask_pkg(const char *capname, struct pkg **pkgs, struct pkg *deflt);

#define obstack_chunk_alloc n_malloc
#define obstack_chunk_free  n_free

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
    
    inst->rootdir = NULL;
    inst->fetchdir = NULL;
    inst->cachedir = NULL;
    inst->dumpfile = NULL;
    inst->rpmopts = NULL;
    inst->rpmacros = NULL;
    inst->askpkg_fn = ask_pkg;
    inst->ask_fn = ask_yn;
    inst->rpmacros = n_array_new(2, NULL, NULL);
    inst->rpmopts = n_array_new(4, NULL, (tn_fn_cmp)strcmp);
    inst->hold_patterns = n_array_new(4, free, (tn_fn_cmp)strcmp);
    inst->ign_patterns = n_array_new(4, free, (tn_fn_cmp)strcmp);
}

struct pkgset *pkgset_new(unsigned optflags)
{
    struct pkgset *ps;
    
    ps = pkg_alloc(sizeof(*ps));
    memset(ps, 0, sizeof(*ps));
    ps->pkgs = pkgs_array_new(2048);
    ps->ordered_pkgs = NULL;
    
    /* just merge pkgdirs->depdirs */
    ps->depdirs = n_array_new(64, NULL, (tn_fn_cmp)strcmp);
    n_array_ctl(ps->depdirs, TN_ARRAY_AUTOSORTED);
    
    
    ps->pkgdirs = n_array_new(4, (tn_fn_free)pkgdir_free, NULL);
    ps->flags = optflags;
    ps->rpmcaps = rpm_rpmlib_caps();
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
    n_array_free(ps->pkgs);

    if (ps->rpmcaps) {
        n_array_free(ps->rpmcaps);
        ps->rpmcaps = NULL;
    }
}


int pkgset_rpmprovides(const struct pkgset *ps, const struct capreq *req)
{
    struct capreq *cap;
    
    if (ps->rpmcaps == NULL)
        return 1;               /* no caps -> assume yes */

    cap = n_array_bsearch_ex(ps->rpmcaps, req,
                             (tn_fn_cmp)capreq_cmp_name);
    
    if (cap && cap_match_req(cap, req, 1))
        return 1;
    
    return 0;
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
    capreq_idx_init(&ps->cap_idx, CAPREQ_IDX_CAP,  8 * n_array_size(ps->pkgs) + 103);
    capreq_idx_init(&ps->req_idx, CAPREQ_IDX_REQ,  18 * n_array_size(ps->pkgs) + 103);
    capreq_idx_init(&ps->obs_idx, CAPREQ_IDX_REQ,  n_array_size(ps->pkgs)/100 + 103);
    file_index_init(&ps->file_idx, 30 * n_array_size(ps->pkgs) + 103);
    ps->flags |= _PKGSET_INDEXES_INIT;

    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
        
#if 0                           /* testing stuff */
        if (strcmp(pkg->name, "SVGATextMode") == 0) {
            struct capreq *req = capreq_new_evr("ghostscript-fonts-std",
                                                n_strdup("6.0-7"), REL_LT, CAPREQ_CNFL);
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


int pkgset_setup(struct pkgset *ps, unsigned flags) 
{
    int n;
    int strict;
    int v = verbose;

    
    strict = ps->flags & PSVERIFY_MERCY ? 0 : 1;

    n = n_array_size(ps->pkgs);
    n_array_sort(ps->pkgs);
    
    if (flags & PSET_DO_UNIQ_PKGNAME) {
        n_array_isort_ex(ps->pkgs, (tn_fn_cmp)pkg_cmp_name_srcpri);
        n_array_uniq_ex(ps->pkgs, (tn_fn_cmp)pkg_cmp_name_uniq);
            
    } else {
        n_array_isort_ex(ps->pkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev_srcpri);
        n_array_uniq_ex(ps->pkgs, (tn_fn_cmp)pkg_cmp_uniq);
    }
        
        
    if (n != n_array_size(ps->pkgs)) {
        n -= n_array_size(ps->pkgs);
        msgn(1, ngettext(
                 "Removed %d duplicate package from available set",
                 "Removed %d duplicate packages from available set", n), n);
    }
        
    pkgset_index(ps);
    mem_info(1, "MEM after index");

    
    v = verbose;    
    if (flags & PSET_VERIFY_FILECNFLS) 
        msgn(1, _("\nVerifying files conflicts..."));
    else
        verbose = -1;
    
    file_index_find_conflicts(&ps->file_idx, strict);
    verbose = v;
    
    pkgset_verify_deps(ps, strict, flags & PSET_VERIFY_DEPS);
    mem_info(1, "MEM after verify deps");

    if (flags & PSET_VERIFY_CNFLS)
        msgn(1, _("\nVerifying packages conflicts..."));
    pkgset_verify_conflicts(ps, strict, flags & PSET_VERIFY_CNFLS);

    pkgset_order(ps, flags & PSET_VERIFY_ORDER);
    mem_info(1, "MEM after order");

    set_capreq_allocfn(n_malloc, n_free, NULL, NULL);
    return ps->nerrors == 0;
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

unsigned insts_to_pkginst_flags(unsigned instflags) 
{
    unsigned flags = 0;

    if (instflags & INSTS_NODEPS)
        flags |= PKGINST_NODEPS;

    if (instflags & INSTS_FORCE)
        flags |= PKGINST_FORCE;

    if (instflags & INSTS_TEST)
        flags |= PKGINST_TEST;

    if (instflags & INSTS_JUSTDB)
        flags |= PKGINST_JUSTDB;
    
    if (instflags & INSTS_UPGRADE)
        flags |= PKGINST_UPGRADE;

    return flags;
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
                is_remote = vf_url_type(pkgpath) & VFURL_REMOTE;
            
            if (verbose > 1) {
                char *p = pkg_is_hand_marked(pkg) ? "" : "dep";
                if (pkg_has_badreqs(pkg)) 
                    msg(2, "not%sInstall %s\n", p, pkg->name);
                else
                    msg(2, "%sInstall %s\n", p, pkgpath);
            }

            if (inst->flags & (INSTS_TEST | INSTS_RPMTEST))
                continue;
            
            if (pkgdb_install(inst->db, pkgpath,
                              insts_to_pkginst_flags(inst->flags | INSTS_NODEPS)))
		logn(LOGWARN|LOGFILE, "INST-OK %s", pkg->name);
	    else {
		logn(LOGERR|LOGFILE, "INST-ERR %s", pkg->name);
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



int packages_dump(tn_array *pkgs, const char *path, unsigned flags)
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
            if (flags & INSTS_JUSTPRINT_N)
                fprintf(stream, "%s\n", pkg->name);
            else
                fprintf(stream, "%s\n", pkg_filename_s(pkg));
        }
    }
    
    if (stream != stdout)
        fclose(stream);
    
    return 1;
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
