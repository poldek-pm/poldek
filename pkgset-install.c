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

#include <rpmlib.h>
#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>

#include "log.h"
#include "pkg.h"
#include "pkgset-def.h"
#include "pkgset.h"
#include "misc.h"
#include "rpmadds.h"
#include "pkgset-req.h"
#include "vfile.h"

#define INST_INSTALL  1
#define INST_UPGRADE  2

struct upgrade_s {
    tn_array       *avpkgs;     
    tn_array       *install_pkgs; /* pkgs to install */
    tn_hash        *depcache;     /* cache of resolved  */
    tn_array       *install_rnos; /* recnos of upgraded packages */
    
    tn_array       *orphan_pkgs;  /* packages which requires
                                     uninstalled ones */
    
    tn_array       *orphan_rnos;  /* recnos of above  */
    
    int            ndberrs;
    int            ndep;
    int            ninstall;
    int            dep_nerr;
    int            cnfl_nerr;
    
    struct inst_s  *inst;
};


/* anyone of pkgs is marked? */
__inline__
static int one_is_marked(struct pkg *pkgs[], int npkgs) 
{
    int i;

    for (i=0; i<npkgs; i++) 
        if (pkg_is_marked(pkgs[i])) 
            return 1;
    
    return 0;
}


static int dump_pkgs_fqpns(struct pkgset *ps, struct upgrade_s *upg)
{
    int i;
    FILE *stream = stdout;
    
    if (upg->inst->dumpfile) {
        if ((stream = fopen(upg->inst->dumpfile, "w")) == NULL) {
            log(LOGERR, "fopen %s: %m\n", upg->inst->dumpfile);
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


static int fetch_pkgs(struct pkgset *ps, struct upgrade_s *upg) 
{
    int rc, i, urltype;
    tn_array *urls = NULL;
    
    
    if (ps->path == NULL) {
        log(LOGERR, "Oj!, packages url not set!?\n");
        return 0;
    }

    if ((urltype = vfile_url_type(ps->path)) == VFURL_PATH) {
        log(LOGERR, "Think! Packages path is not remote URL\n");
        return 0;
    }

    if (n_array_size(upg->install_pkgs) == 1) {
        char path[PATH_MAX];
        
        snprintf(path, sizeof(path), "%s/%s", ps->path,
                 pkg_filename_s(n_array_nth(upg->install_pkgs, 0)));
        
        return vfile_fetch(upg->inst->fetchdir, path, urltype);
    } 

    urls = n_array_new(n_array_size(upg->install_pkgs), NULL, NULL);
    
    for (i=0; i<n_array_size(ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
        if (pkg_is_marked(pkg)) {
            char path[PATH_MAX], *s;
            int len;
            
            len = snprintf(path, sizeof(path), "%s/%s", ps->path, 
                           pkg_filename_s(pkg));
            
            s = alloca(len + 1);
            memcpy(s, path, len);
            s[len] = '\0';
            n_array_push(urls, s);
        }
    }
    
    rc = vfile_fetcha(upg->inst->fetchdir, urls, urltype);
    if (urls)
        n_array_free(urls);
    return rc;
}



static int runrpm(struct pkgset *ps, struct upgrade_s *upg) 
{
    char *argv[128 + n_array_size(ps->pkgs) + 1];
    char *cmd, *cmdbn;
    int rc, i, n, nopts = 0;
    struct runst rst;
    
    argv[128 + n_array_size(ps->pkgs) + 1] = NULL;
    n = 0;

    if (upg->inst->flags & INSTS_JUSTPRINT) {
        return dump_pkgs_fqpns(ps, upg);
        
    } else if (upg->inst->flags & INSTS_JUSTFETCH) {
        return fetch_pkgs(ps, upg);
        
    } else {
        int nv = verbose;
        
        cmd = "/bin/rpm";
        cmdbn = "rpm";
        argv[n++] = cmdbn;

        if (ps->flags & PSMODE_INSTALL)
            argv[n++] = "--install";
        else if (ps->flags & PSMODE_UPGRADE)
            argv[n++] = "--upgrade";
        else {
            n_assert(0);
            abort();
        }

        if (nv) {
            argv[n++] = "-vh";
            nv--;
        }

        while (nv--) 
            argv[n++] = "-v";

        if (upg->inst->instflags & PKGINST_TEST)
            argv[n++] = "--test";
        
        if (upg->inst->instflags & PKGINST_JUSTDB)
            argv[n++] = "--justdb";

        if (upg->inst->instflags & PKGINST_FORCE)
            argv[n++] = "--force";
        
        if (upg->inst->instflags & PKGINST_NODEPS)
            argv[n++] = "--nodeps";

        if (upg->inst->rpmacros) 
            for (i=0; i<n_array_size(upg->inst->rpmacros); i++) {
                argv[n++] = "--define";
                argv[n++] = n_array_nth(upg->inst->rpmacros, i);
            }

        if (upg->inst->rpmopts) 
            for (i=0; i<n_array_size(upg->inst->rpmopts); i++)
                argv[n++] = n_array_nth(upg->inst->rpmopts, i);
    }
    nopts = n;
    
    for (i=0; i<n_array_size(ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
        if (pkg_is_marked(pkg)) {
            char path[PATH_MAX], *s;
            int len;
            
            if (pkg->dn) 
                len = snprintf(path, sizeof(path), "%s/%s", pkg->dn, 
                               pkg_filename_s(pkg));
            else if (ps->path) 
                len = snprintf(path, sizeof(path), "%s/%s", ps->path, 
                               pkg_filename_s(pkg));
            else
                len = snprintf(path, sizeof(path), "%s", pkg_filename_s(pkg));

            s = alloca(len + 1);
            memcpy(s, path, len);
            s[len] = '\0';
            argv[n++] = s;
        }
    }

    n_assert(n > nopts); 
    argv[n++] = NULL;
    
    if (verbose > 0) {
        char buf[1024], *p;
        p = buf;
        
        for (i=0; i<nopts; i++) 
            p += snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", argv[i]);
        *p = '\0';
        msg(1, "$Running%s...\n", buf);
    }
    
    if (p_open(&rst, cmd, argv) == NULL) 
        return 0;
    
    n = 0;
    if (verbose == 0) {
        verbose = 1;
        n = 1;
    }
    
    rc = p_process_cmd(&rst, cmdbn);

    if (n)
        verbose--;

    return rc;
}


static void mapfn_clean_pkg_color(struct pkg *pkg) 
{
    pkg_set_color(pkg, PKG_COLOR_WHITE);
}

#define PROCESS_DEPS       0 
#define PROCESS_ORPHANS    1

static int process_deps(struct pkgset *ps, tn_array *pkgs,
                        struct upgrade_s *upg, int how) 
{
    int i, j, strict, ndepadds = 0;
    tn_array *markarr[2], *tmparr;
    int nmarkarr = 0, nmarked, nloop;
    
    strict = ps->flags & PSVERIFY_MERCY ? 0 : 1;
    
    tmparr = pkgs;
    nmarked = n_array_size(tmparr);
    markarr[0] = n_array_new(64, NULL, NULL);
    markarr[1] = n_array_new(64, NULL, NULL);
    
    nloop = 0;
    
    while (nmarked > 0) {
        nmarked = 0;
        for (i=0; i<n_array_size(tmparr); i++) {
            struct pkg *pkg = n_array_nth(tmparr, i);

            if (!pkg_is_color(pkg, PKG_COLOR_WHITE))
                continue;
            
            pkg_set_color(pkg, PKG_COLOR_GRAY); /* processed */
            
            if (pkg->reqs == NULL)
                continue;
            
            msg_i(2, nloop, "%s\n", pkg_snprintf_s(pkg));

            for (j=0; j<n_array_size(pkg->reqs); j++) {
                struct capreq *req;
                struct pkg **suspkgs, pkgsbuf[1024], *tomark = NULL;
                int nsuspkgs = 0;
                char *reqname;
                int reqnover;
                
                req = n_array_nth(pkg->reqs, j);
                reqname = capreq_name(req);
                reqnover = capreq_has_ver(req) == 0;
            
                if (reqnover && n_hash_exists(upg->depcache, reqname)) {
                    msg_i(4, nloop, "in cache %s\n", reqname);
                    continue;
                }
                
                /* lookup in pkgset */
                if (psreq_lookup(ps, req, &suspkgs, (struct pkg **)pkgsbuf,
                                 &nsuspkgs)) {
                    int nmatched = 0;
                    struct pkg *matched[nsuspkgs];
                    
                    if (nsuspkgs == 0)
                        continue;
                    
                    if (psreq_match_pkgs(pkg, req, strict, suspkgs, nsuspkgs,
                                         (struct pkg **)matched, &nmatched)) {
                        
                        /* already marked for upgrade */
                        if (nmatched == 0 || one_is_marked(matched, nmatched)){
                            if (reqnover)
                                n_hash_insert(upg->depcache, reqname,(void*)1);
                            continue;
                            
                        } else { /* save candidate */
                            tomark = matched[0];
                        }
                    }
                } 
            
                if (pkgdb_match_req(upg->inst->db, req, strict,
                                    upg->install_rnos)) {
                    msg_i(2, nloop, " %s satisfied by db\n",
                          capreq_snprintf_s(req));
                    if (reqnover)
                        n_hash_insert(upg->depcache, reqname, (void*)1);
                
                } else if (tomark) {
                    msg_i(2, nloop, " %s marks %s (cap %s)\n",
                          pkg_snprintf_s(pkg), pkg_snprintf_s0(tomark),
                          capreq_snprintf_s(req));
                    pkg_dep_mark(tomark);
                    n_array_push(markarr[nmarkarr], tomark);
                    nmarked++;
                    upg->ndep++;
                    upg->ninstall++;
                    ndepadds++;
                    
                    if (reqnover)
                        n_hash_insert(upg->depcache, reqname, (void*)1);
                
                } else {
                    if (how == PROCESS_DEPS) 
                        log(LOGERR, " %s: req %s not found\n",
                            pkg_snprintf_s(pkg), capreq_snprintf_s(req));
                    else if (how == PROCESS_ORPHANS)
                        log(LOGERR, " %s is required by %s\n", 
                            capreq_snprintf_s(req), pkg_snprintf_s(pkg));
                    else
                        n_assert(0);
                    pkg_set_badreqs(pkg);
                    upg->dep_nerr++;
                }
            }
        }

        for (i=0; i<n_array_size(markarr[nmarkarr]); i++) 
            n_array_push(upg->install_pkgs, n_array_nth(markarr[nmarkarr], i));
        
        /* swap tables */
        tmparr = markarr[nmarkarr];
        nmarkarr++;
        nmarkarr %= 2;
        n_array_clean(markarr[nmarkarr]);
        nloop++;
    }

    n_array_free(markarr[0]);
    n_array_free(markarr[1]);
    return ndepadds;
}

/* add to orphans packages obsoleted by installed ones */
static
void add_obsoletes(struct pkgset *ps, struct upgrade_s *upg) 
{
    int i;
    
    for (i=0; i<n_array_size(upg->install_pkgs); i++) {
        struct pkg *pkg = n_array_nth(upg->install_pkgs, i);
        if (pkg_is_color(pkg, PKG_COLOR_GRAY)) {
            pkg_set_color(pkg, PKG_COLOR_BLACK);
            if (pkg->cnfls) {
                int j;
                for (j=0; j<n_array_size(pkg->cnfls); j++) {
                    struct capreq *cnfl = n_array_nth(pkg->cnfls, j);
                    if (cnfl_is_obsl(cnfl))
                        rpm_get_pkgs_requires_obsl_pkg(upg->inst->db->dbh,
                                                       cnfl,
                                                       upg->install_rnos,
                                                       upg->orphan_rnos,
                                                       upg->orphan_pkgs);
                    
                }
            }
        }
    }
}

    
/* process packages to install:
   - check dependencies
   - mark unresolved dependencies finded in pkgset
   - check conflicts
 */
static
int pkgset_do_install(struct pkgset *ps, struct upgrade_s *upg)
{
    int i, j, strict, cnfl_nerr = 0;
    
    strict = ps->flags & PSVERIFY_MERCY ? 0 : 1;
    
    msg(1, "$Processing dependencies...\n");

    n_array_map(ps->pkgs, (tn_fn_map1)mapfn_clean_pkg_color);

    while (process_deps(ps, upg->install_pkgs, upg, PROCESS_DEPS) ||
           process_deps(ps, upg->orphan_pkgs, upg, PROCESS_ORPHANS)) {

        add_obsoletes(ps, upg);
        
    }
    
    msg(1, "$Verifying conflicts...\n");
    cnfl_nerr = 0;
    for (i=0; i<n_array_size(upg->install_pkgs); i++) {
        struct pkg *pkg = n_array_nth(upg->install_pkgs, i);
        if (pkg->cnflpkgs) {
            int j;
            
            for (j=0; j<n_array_size(pkg->cnflpkgs); j++) {
                struct cnflpkg *cpkg = n_array_nth(pkg->cnflpkgs, j);
                if (pkg_is_marked(cpkg->pkg)) {
                    log(LOGERR, "Conflict between %s and %s\n",
                        pkg_snprintf_s(pkg), pkg_snprintf_s0(cpkg->pkg));
                    cnfl_nerr++;
                }
            }
        }
    }
    
    if (upg->inst->instflags & PKGINST_FORCE)
        cnfl_nerr = 0;

    if (cnfl_nerr && (upg->inst->instflags & PKGINST_TEST) == 0) {
        log(LOGERR, "Stop\n"); 
        return 0;
    }

    msg(1, "$There are %d package(s) to install, %d marked by dependencies:\n",
        upg->ninstall, upg->ndep);
    
    j = 0;
    for (i=0; i<n_array_size(upg->install_pkgs); i++) {
        struct pkg *pkg = n_array_nth(upg->install_pkgs, i);
        if (pkg_is_dep_marked(pkg)) 
            msg(1, "%d. %s\n", ++j, pkg_snprintf_s(pkg));
    }

    if (upg->inst->instflags & PKGINST_NODEPS)
        upg->dep_nerr = 0;

    if (upg->dep_nerr) {
        log(LOGERR, "There are %d unresolved dependencies.\n", upg->dep_nerr);
        if ((upg->inst->instflags & PKGINST_TEST) == 0)
            return 0;
    }

    pkgdb_closedb(upg->inst->db);
    return runrpm(ps, upg);
}


/* save in upg->install_pkgs if newer version finded */
static 
void mapfn_chk_pkg(Header h, off_t offs, void *upgptr) 
{
    struct upgrade_s *upg = upgptr;
    uint32_t *epoch;
    struct pkg *pkg, tmpkg;
    
    if (!rpmhdr_nevr(h, &tmpkg.name, &epoch, &tmpkg.ver, &tmpkg.rel)) {
        log(LOGERR, "db package header corrupted (!?)\n");
        upg->ndberrs++;
        
    } else {
        int i, cmprc;
        tmpkg.epoch = epoch ? *epoch : 0;
        
        i = n_array_bsearch_idx_ex(upg->avpkgs, &tmpkg,
                                   (tn_fn_cmp)pkg_cmp_name); 
        if (i < 0) {
            msg(3, "%-32s not found in repository\n", pkg_snprintf_s(&tmpkg));
            return;
        }

        pkg = n_array_nth(upg->avpkgs, i);
        cmprc = pkg_cmp_evr(pkg, &tmpkg);
        if (verbose) {
            if (cmprc == 0) 
                msg(3, "%-32s up to date\n", pkg_snprintf_s(&tmpkg));
                
            else if (cmprc < 0)
                msg(3, "%-32s newer than repository one\n",
                    pkg_snprintf_s(&tmpkg));
                
            else
                msg(1, "%-32s |-> %-30s\n", pkg_snprintf_s(&tmpkg),
                    pkg_snprintf_s0(pkg));
        }
        
        if (cmprc > 0) {
            pkg_hand_mark(pkg);
            n_array_push(upg->install_pkgs, pkg);
            n_array_push(upg->install_rnos, (void*)offs);
        }
    }
}


static
void mapfn_chk_pkg_orphan_deps(void *h, off_t offs __attribute__((unused)),
                               void *arg) 
{
    struct upgrade_s *upg = arg;
    rpm_get_pkgs_requires_pkgh(upg->inst->db->dbh, h, upg->install_rnos,
                               upg->orphan_rnos, upg->orphan_pkgs);
}


static void init_upgrade_s(struct upgrade_s *upg, struct pkgset *ps,
                           struct inst_s *inst)
{
    
    memset(upg, 0, sizeof(*upg));
    upg->avpkgs = ps->pkgs;
    upg->install_pkgs = n_array_new(128, NULL, NULL);
    upg->install_rnos = n_array_new(128, NULL, NULL);
    upg->orphan_pkgs = n_array_new(128, (tn_fn_free)pkg_free,
                                  (tn_fn_cmp)pkg_cmp_name_evr_rev);
    upg->orphan_rnos = n_array_new(128, NULL, NULL);
    upg->ndberrs = 0;
    upg->inst = inst;
    upg->depcache = n_hash_new(1003, NULL);
    n_hash_ctl(upg->depcache, TN_HASH_NOCPKEY);
}


static void destroy_upgrade_s(struct upgrade_s *upg)
{
    upg->avpkgs = NULL;
    n_array_free(upg->install_pkgs);
    n_array_free(upg->install_rnos);
    n_array_free(upg->orphan_pkgs);
    n_array_free(upg->orphan_rnos);
    upg->inst = NULL;
    n_hash_free(upg->depcache);
    memset(upg, 0, sizeof(*upg));
}


int pkgset_upgrade_dist(struct pkgset *ps, struct inst_s *inst) 
{
    int rc = 1;
    struct upgrade_s upg;

    init_upgrade_s(&upg, ps, inst);
    
    
    msg(1, "_Looking up packages for upgrade...\n");
    
    //set_capreq_allocfn(malloc, free, &capreq_alloc_fn, &capreq_free_fn);

    /* find packages to upgrade */
    pkgdb_map(inst->db, mapfn_chk_pkg, &upg);
    n_array_sort(upg.install_rnos);

    /* collect packages which requires upgraded packages */
    rpm_dbiterate(inst->db->dbh, upg.install_rnos,
                  mapfn_chk_pkg_orphan_deps, &upg);

    n_array_sort(upg.orphan_pkgs);
    n_array_uniq(upg.orphan_pkgs);
    // set_capreq_allocfn(capreq_alloc_fn, capreq_free_fn, NULL, NULL);

    if (upg.ndberrs) {
        log(LOGERR, "There are database errors (?), give up\n");
        n_array_free(upg.install_pkgs);
        return 0;
    }
    

    if (n_array_size(upg.install_pkgs) == 0) {
        msg(1, "All packages are up to date, nothing to do\n");
        
    } else {
        rc = pkgset_do_install(ps, &upg);
    }
    
    destroy_upgrade_s(&upg);
    return rc;
}

int pkgset_install(struct pkgset *ps, struct inst_s *inst)
{
    int i, rc = 1;
    struct upgrade_s upg;

    init_upgrade_s(&upg, ps, inst);

    for (i=0; i<n_array_size(ps->pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->pkgs, i);
        if (pkg_is_marked(pkg)) {
            n_array_push(upg.install_pkgs, pkg);
            upg.ninstall++;
        }
        
    }
    
    rc = pkgset_do_install(ps, &upg);
    destroy_upgrade_s(&upg);
    return rc;
}
