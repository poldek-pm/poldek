/* 
  Copyright (C) 2000 - 2002 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/
/*
  $Id$
*/
#define ENABLE_TRACE 0
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <obstack.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fnmatch.h>

#include <rpm/rpmlib.h>
#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>

#include <vfile/vfile.h>
#include <vfile/p_open.h>

#include "log.h"
#include "term.h"
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"
#include "rpmadds.h"
#include "rpmhdr.h"
#include "pkgset-req.h"
#include "dbpkg.h"
#include "dbpkgset.h"
#include "rpmdb_it.h"


#define INST_INSTALL  1
#define INST_UPGRADE  2

#define PROCESS_AS_NEW       (1 << 0)
#define PROCESS_AS_ORPHAN    (1 << 1)

struct upgrade_s {
    tn_array       *avpkgs;
    tn_array       *install_pkgs;     /* pkgs to install */
    
    tn_hash        *db_deps;          /* cache of resolved db dependencies */

    struct dbpkg_set *uninst_set;
    
    tn_array       *orphan_dbpkgs;    /* array of orphaned dbpkg*s */
    
    int            strict;
    int            ndberrs;
    int            ndep;
    int            ninstall;

    int            nerr_dep;
    int            nerr_cnfl;
    int            nerr_dbcnfl;
    int            nerr_fatal;
    
    struct inst_s  *inst;

    tn_hash        *db_pkgs;    /* used by mapfn_mark_newer_pkg() */
    int             nmarked;

    tn_array       *pkg_stack;  /* stack for current processed packages  */
    void           *pkgflmod_mark;
};

static
int process_pkg_conflicts(int indent, struct pkg *pkg,
                          struct pkgset *ps, struct upgrade_s *upg);

static
int process_pkg_deps(int indent, struct pkg *pkg,
                     struct pkgset *ps, struct upgrade_s *upg, int process_as);



#define DBDEP_FOREIGN          (1 << 3)
#define DBDEP_DBSATISFIED      (1 << 4)

struct db_dep {
    struct capreq *req;
    struct pkg    *spkg;       /* package which satisfies req */
    tn_array      *pkgs;       /* packages which requires req */
    unsigned      flags;
};


static
void db_dep_free(struct db_dep *db_dep) 
{
    n_array_free(db_dep->pkgs);
    db_dep->req = NULL;
    db_dep->pkgs = NULL;
    db_dep->spkg = NULL;
    free(db_dep);
}

static
tn_hash *db_deps_new(void) 
{
    tn_hash *h;
    h = n_hash_new(103, (tn_fn_free)db_dep_free);
    return h;
}

static 
void db_deps_add(tn_hash *db_deph, struct capreq *req, struct pkg *pkg,
                 struct pkg *spkg, unsigned flags) 
{
    struct db_dep *dep;

    
    DBGF_NULL("%s from %s stby %s [%s]\n", capreq_snprintf_s(req),
             pkg_snprintf_s(pkg), spkg ? pkg_snprintf_s0(spkg) : "NULL",
             (flags & DBDEP_FOREIGN) ? "foreign" :
             (flags & DBDEP_DBSATISFIED) ? "db" : "UNKNOWN");
    
    if ((dep = n_hash_get(db_deph, capreq_name(req)))) {
        n_array_push(dep->pkgs, pkg);
        dep->flags |= flags;
        
    } else {
        dep = malloc(sizeof(*dep));
        dep->req = req;
        dep->spkg = spkg;
        dep->pkgs = n_array_new(4, NULL, (tn_fn_cmp)pkg_cmp_name_evr);
        n_array_push(dep->pkgs, pkg);
        dep->flags = flags;
        n_hash_insert(db_deph, capreq_name(req), dep);
    }
}

static
void db_deps_remove_pkg(tn_hash *db_deph, struct pkg *pkg)
{
    int i;

    DBGF_NULL("%s\n", pkg_snprintf_s(pkg));
    
    if (pkg->reqs == NULL)
        return;
        
    for (i=0; i<n_array_size(pkg->reqs); i++) {
        struct db_dep *dep;
        struct capreq *req;

        req = n_array_nth(pkg->reqs, i);
        if ((dep = n_hash_get(db_deph, capreq_name(req)))) {
            DBGF_NULL("rm %s\n", capreq_snprintf_s(req));
            n_array_remove(dep->pkgs, pkg);
            if (n_array_size(dep->pkgs) == 0) 
                n_hash_remove(db_deph, capreq_name(req));
        }
    }
}

static
struct db_dep *db_deps_has(tn_hash *db_deph, struct capreq *req) 
{
    return n_hash_get(db_deph, capreq_name(req));
}

static
int db_deps_provides_req(tn_hash *db_deph, struct capreq *req, int flags) 
{
    struct db_dep *dep;

    if ((dep = n_hash_get(db_deph, capreq_name(req))))
        if ((dep->flags & flags) &&
            capreq_strcmp_evr(req, dep->req) == 0)
            return 1;
    
    return 0;
}

/* anyone of pkgs is marked? */
static inline int one_is_marked(struct pkg *pkgs[], int npkgs)
{
    int i;

    for (i=0; i < npkgs; i++) 
        if (pkg_is_marked(pkgs[i])) 
            return 1;
    
    return 0;
}

static
int is_installable(struct pkg *pkg, struct inst_s *inst, int is_hand_marked) 
{
    int cmprc = 0, npkgs, install = 1, freshen;

    
    freshen = (inst->flags & INSTS_FRESHEN);
    npkgs = rpm_is_pkg_installed(inst->db->dbh, pkg, &cmprc, NULL);
    
    if (npkgs < 0) 
        die();

    n_assert(npkgs >= 0);
    
    if (npkgs == 0) {
        if (is_hand_marked && freshen)
            install = 0;
        
    } else if (is_hand_marked && npkgs > 1) {
        log(LOGERR, "%s: multiple instances installed, give up\n", pkg->name);
        install = -1;
        
    } else {
        if (pkg_is_hold(pkg)) {
            log(LOGERR, "%s: refusing to upgrade held package\n",
                pkg_snprintf_s(pkg));
            install = 0;
            
        } else if (cmprc <= 0 && (inst->instflags & PKGINST_FORCE) == 0) {
            char *msg = "%s: %s version installed, %s\n";
            char *eqs = cmprc == 0 ? "equal" : "newer";
            char *skiped =  "skipped";
            char *giveup =  "give up";
            
            install = 0;
            
            if (!is_hand_marked) {
                log(LOGERR, msg, pkg_snprintf_s(pkg), eqs, giveup);
                install = -1;
                
            } else if (is_hand_marked && !freshen) { /* msg without "freshen" */
                msg(0, msg, pkg_snprintf_s(pkg), eqs, skiped);
            }
        }
    }

    return install;
}

#if 0
static struct pkg *find_pkg_old(tn_array *pkgs, const char *name) 
{
    struct pkg tmpkg;
    int i;
    
    tmpkg.name = (char*)name;

    n_array_sort(pkgs);
    i = n_array_bsearch_idx_ex(pkgs, &tmpkg, (tn_fn_cmp)pkg_cmp_name); 
    if (i < 0)
        return NULL;

    return n_array_nth(pkgs, i);
}
#endif

static
struct pkg *find_pkg(const char *name, tn_array *pkgs,
                     struct upgrade_s *upg)
{
    struct pkg tmpkg, *curr_pkg, *pkg, *selected_pkg;
    char prefix1[128], prefix2[128], *p;
    int i;
    
    tmpkg.name = (char*)name;

    n_array_sort(pkgs);
    i = n_array_bsearch_idx_ex(pkgs, &tmpkg, (tn_fn_cmp)pkg_cmp_name); 
    if (i < 0)
        return NULL;

    selected_pkg = NULL;
    pkg = n_array_nth(pkgs, i);
    curr_pkg = n_array_nth(upg->pkg_stack, n_array_size(upg->pkg_stack) - 1);
    

    snprintf(prefix1, sizeof(prefix1), "%s", name);
    if ((p = strchr(prefix1, '-')))
        *p = '\0';

    snprintf(prefix2, sizeof(prefix2), "%s", curr_pkg->name);
    if ((p = strchr(prefix2, '-')))
        *p = '\0';

    DBGF("current pkg %s, name = %s, p1, p2 = %s, %s\n", pkg_snprintf_s(curr_pkg), name,
           prefix1, prefix2);
    
    if (strcmp(prefix1, prefix2) != 0)
        return pkg;
    
    for (; i<n_array_size(pkgs); i++) {
        struct pkg *p = n_array_nth(pkgs, i);
        if (strcmp(p->name, name) != 0)
            break;
        
        if (pkg_cmp_evr(p, curr_pkg) == 0) {
            if (selected_pkg && pkg_cmp_evr(selected_pkg, curr_pkg) != 0)
                selected_pkg = NULL;
            
            if (selected_pkg == NULL)
                selected_pkg = p;
            DBGF("%s [yes]\n", pkg_snprintf_s(selected_pkg));
            break;
            
        } else if (selected_pkg == NULL && pkg_cmp_ver(p, curr_pkg) == 0) {
            selected_pkg = p;
            DBGF("%s [maybe]\n", pkg_snprintf_s(selected_pkg));
            
        } else {
            DBGF("%s [no]\n", pkg_snprintf_s(p));
        }
    }

    if (selected_pkg == NULL)
        selected_pkg = pkg;
    
    return selected_pkg;
}


static int select_best_pkg(struct pkg **candidates,  int npkgs) 
{
    int *ncnfls, i, i_min, cnfl_min;

    ncnfls = alloca(npkgs * sizeof(*ncnfls));
    for (i=0; i < npkgs; i++)
        ncnfls[i] = 0;
        
    for (i=0; i<npkgs; i++) {
        struct pkg *pkg = candidates[i];
        
        if (pkg->cnflpkgs == NULL)
            return 0;
        
        for (i = 0; i < n_array_size(pkg->cnflpkgs); i++) {
            struct cnflpkg *cpkg = n_array_nth(pkg->cnflpkgs, i);
            if (pkg_is_marked(cpkg->pkg))
                ncnfls[i]++;
        }
    }
    
    cnfl_min = INT_MAX;
    i_min = -1;
    for (i=0; i<npkgs; i++) {
        //DBGF("%d. %s %d\n", i, pkg_snprintf_s(candidates[i]), ncnfls[i]);
        if (cnfl_min > ncnfls[i]) {
            cnfl_min = ncnfls[i];
            i_min = i;
        }
    }
    
    if (i_min < 0)
        i_min = 0;
    return i_min;
}

/* lookup in pkgset */
static int find_req(const struct pkg *pkg, struct capreq *req, struct pkgset *ps,
                    struct pkg **mpkg)
{
    struct pkg **suspkgs, pkgsbuf[1024];
    int    nsuspkgs = 0, found = 0;

    
    *mpkg = NULL;
    found = psreq_lookup(ps, req, &suspkgs, (struct pkg **)pkgsbuf, &nsuspkgs);
        
    if (found && nsuspkgs) {
        struct pkg **matches;
        int nmatched = 0, strict;

        found = 0;
        matches = alloca(sizeof(*matches) * nsuspkgs);
        strict = ps->flags & PSVERIFY_MERCY ? 0 : 1;
        if (psreq_match_pkgs(pkg, req, strict, suspkgs,
                             nsuspkgs, matches, &nmatched)) {
            found = 1;
            
            /* already not marked for upgrade */
            if (nmatched > 0 && !one_is_marked(matches, nmatched))
                *mpkg = matches[select_best_pkg(matches, nmatched)];
        }
    }
    
    return found;
}


#define mark_package(a, b) do_mark_package(a, b, PKG_DIRMARK);

static int do_mark_package(struct pkg *pkg, struct upgrade_s *upg,
                           unsigned mark)
{
    int rc;
    
    n_assert(pkg_is_marked(pkg) == 0);
    if ((rc = is_installable(pkg, upg->inst, pkg_is_marked_i(pkg))) <= 0) {
        upg->nerr_fatal++; 
        return 0;
    }
    
    DBGF("%s, is_installable = %d\n", pkg_snprintf_s(pkg), rc);
    pkg_unmark_i(pkg);

    n_assert(!pkg_has_badreqs(pkg));

    if (rc > 0) {
        if (mark == PKG_DIRMARK) {
            pkg_hand_mark(pkg);
            
        } else {
            pkg_dep_mark(pkg);
            upg->ndep++;
        }
        n_array_push(upg->install_pkgs, pkg);
    }

    return rc >= 0;
}

static
void message_depmark(int indent, const struct pkg *marker,
                     const struct pkg *pkg, 
                     const struct capreq *marker_req, int process_as)
{
    const char *reqstr = "cap";
    const char *marker_prefix = "";

    
    if (process_as == PROCESS_AS_ORPHAN)
        marker_prefix = "orphaned ";

    if (capreq_is_cnfl(marker_req))
        reqstr = "cnfl";
            
    msg_i(1, indent, "%s%s marks %s (%s %s)\n", marker_prefix, 
          pkg_snprintf_s(marker), pkg_snprintf_s0(pkg),
          reqstr, capreq_snprintf_s(marker_req));
}


static
int dep_mark_package(struct pkg *pkg,
                     struct pkg *bypkg, struct capreq *byreq,
                     struct upgrade_s *upg)
{
    const struct pkg *ppkg;

    if (pkg_has_badreqs(pkg)) {
        log(LOGERR, "%s: skip follow %s cause it's dependency"
            "errors.\n", pkg_snprintf_s(bypkg), pkg_snprintf_s0(pkg),
            capreq_snprintf_s(byreq));
                
        pkg_set_badreqs(bypkg);
        upg->nerr_dep++;
        return 0;
    }


    if ((ppkg = dbpkg_set_provides(upg->uninst_set, byreq)) &&
        pkg_cmp_name_evr(ppkg, pkg) == 0) {
        
        log(LOGERR, "%s: dependency loop - "
            "package is marked for removal\n", pkg_snprintf_s(pkg));
        upg->nerr_fatal++; 
        return 0;
    }
    
    return do_mark_package(pkg, upg, PKG_INDIRMARK);
}

static
int do_greedymark(int indent, struct pkg *pkg, struct pkg *oldpkg,
                  struct capreq *unresolved_req,
                  struct pkgset *ps, struct upgrade_s *upg) 
{
    
    if (pkg_is_marked(pkg))
        n_assert(0);
    
    if (pkg_cmp_evr(pkg, oldpkg) == 0)
        return 0;
    
    msg_i(1, indent, "greedy upgrade %s to %s-%s "
          "(unresolved %s)\n", pkg_snprintf_s(oldpkg),
          pkg->ver, pkg->rel, capreq_snprintf_s(unresolved_req));

    if (dep_mark_package(pkg, NULL, unresolved_req, upg))
        process_pkg_deps(indent, pkg, ps, upg, PROCESS_AS_NEW);
    return 1;
}



/* add to upg->orphan_dbpkgs packages required by pkg */
static
int process_pkg_orphans(struct pkg *pkg, struct upgrade_s *upg) 
{
    unsigned ldflags = PKG_LDNEVR | PKG_LDREQS;
    int i, k, n = 0;
    rpmdb dbh;


    dbh = upg->inst->db->dbh;

    mem_info(1, "process_pkg_orphans:");
    n += rpm_get_pkgs_requires_capn(dbh, upg->orphan_dbpkgs, pkg->name,
                                    upg->uninst_set->dbpkgs, ldflags);
        
    if (pkg->caps)
        for (i=0; i < n_array_size(pkg->caps); i++) {
            struct capreq *cap = n_array_nth(pkg->caps, i);
            n += rpm_get_pkgs_requires_capn(dbh, upg->orphan_dbpkgs,
                                            capreq_name(cap),
                                            upg->uninst_set->dbpkgs, ldflags);
        }
    
    if (pkg->fl == NULL)
        return n;
    
    for (i=0; i < n_array_size(pkg->fl); i++) {
        struct pkgfl_ent *flent = n_array_nth(pkg->fl, i);
        char path[PATH_MAX], *endp;

        endp = path;
        if (*flent->dirname != '/')
            *endp++ = '/';
            
        endp = n_strncpy(endp, flent->dirname, sizeof(path));
            
        for (k=0; k<flent->items; k++) {
            struct flfile *file = flent->files[k];
            int path_left_size;
                
            if (*(endp - 1) != '/')
                *endp++ = '/';
                
            path_left_size = sizeof(path) - (endp - path);
            n_strncpy(endp, file->basename, path_left_size);
                
            rpm_get_pkgs_requires_capn(dbh, upg->orphan_dbpkgs, path,
                                       upg->uninst_set->dbpkgs, ldflags);
        }
    }
    
    return n;
}

static
int verify_unistalled_cap(int indent, struct capreq *cap, struct pkg *pkg,
                          struct pkgset *ps, struct upgrade_s *upg) 
{
    struct db_dep *db_dep;
    struct capreq *req;

    DBGF_NULL("%s: %s\n", pkg_snprintf_s(pkg), capreq_snprintf_s(cap));
    if ((db_dep = db_deps_has(upg->db_deps, cap)) == NULL) {
        //       printf("  [1] -> NO in db_deps\n");
        return 1;
    }

    if (db_dep->spkg && pkg_is_marked(db_dep->spkg)) {
        //printf("  [1] -> marked %s\n", pkg_snprintf_s(db_dep->spkg));
        return 1;
    }
    
    
    req = db_dep->req;

    // still satisfied by db? 
    if (pkgdb_match_req(upg->inst->db, req, upg->strict,
                        upg->uninst_set->dbpkgs)) {
        //printf("  [1] -> satisfied by db\n");
        return 1;
    }
    	

    if (db_dep->spkg) {
        message_depmark(indent, pkg, db_dep->spkg, req, PROCESS_AS_ORPHAN);
        if (!dep_mark_package(db_dep->spkg, pkg, req, upg))
            return 0;
        return process_pkg_deps(indent, db_dep->spkg, ps, upg, PROCESS_AS_NEW);
    }
    
    if (db_dep->flags & PROCESS_AS_NEW) {
        log(LOGERR, "%s: req %s not found\n", pkg_snprintf_s(pkg),
            capreq_snprintf_s(req));
        pkg_set_badreqs(pkg);
        upg->nerr_dep++;
            
                
    } else if (db_dep->flags & PROCESS_AS_ORPHAN) {
        int i;

        for (i=0; i<n_array_size(db_dep->pkgs); i++) {
            struct pkg *opkg = n_array_nth(db_dep->pkgs, i);
            struct pkg *p;
            int not_found = 1;

            
            if (pkg_cmp_name_evr(opkg, pkg) == 0) /* packages orphanes itself */
                continue;
                
            if ((p = find_pkg(opkg->name, ps->pkgs, upg))) {
                if (pkg_is_marked_i(p)) {
                    mark_package(p, upg);
                    process_pkg_deps(-2, p, ps, upg, PROCESS_AS_NEW);
                    not_found = 0;
                        
                } else if (upg->inst->flags & INSTS_GREEDY) {
                    if (!pkg_is_marked(p) && do_greedymark(indent, p, opkg, req,
                                                           ps, upg))
                        not_found = 0;
                }
            }
            
            if (not_found) {
                log(LOGERR, "%s (cap %s) is required by %s\n",
                    pkg_snprintf_s(pkg), capreq_snprintf_s(req),
                    pkg_snprintf_s0(opkg));
                
                
                pkg_set_badreqs(pkg);
                upg->nerr_dep++;
            }
        }
    }
    

    
    return 1;
}
    

static
void process_pkg_obsl(int indent, struct pkg *pkg, struct pkgset *ps,
                      struct upgrade_s *upg)
{
    int n, i;
    rpmdb dbh = upg->inst->db->dbh;
    
    DBGMSG_F("%s\n", pkg_snprintf_s(pkg));
    
    n = rpm_get_obsoletedby_pkg(dbh, upg->uninst_set->dbpkgs, pkg, PKG_LDWHOLE);
    if (n == 0)
        return;

    n = 0;
    for (i=0; i < n_array_size(upg->uninst_set->dbpkgs); i++) {
        struct dbpkg *dbpkg = n_array_nth(upg->uninst_set->dbpkgs, i);
        if ((dbpkg->flags & DBPKG_TOUCHED) == 0) {
            msg_i(1, indent, "%s obsoleted by %s\n", dbpkg_snprintf_s(dbpkg),
                  pkg_snprintf_s(pkg));
            pkg_mark_obsoleted(dbpkg->pkg);
            db_deps_remove_pkg(upg->db_deps, dbpkg->pkg);
            dbpkg->flags |= DBPKG_TOUCHED;

            if (dbpkg->pkg->caps) {
                int j;
                for (j=0; j < n_array_size(dbpkg->pkg->caps); j++) {
                    struct capreq *cap = n_array_nth(dbpkg->pkg->caps, j);
                    verify_unistalled_cap(indent, cap, dbpkg->pkg, ps, upg);
                }
            }

            if (pkg->fl) {
                struct capreq *cap;
                int j, k;

                
                cap = alloca(sizeof(cap) + PATH_MAX);
                memset(cap, 0, sizeof(*cap));
                cap->_buf[0] = '\0';
                
                for (j=0; j < n_array_size(dbpkg->pkg->fl); j++) {
                    struct pkgfl_ent *flent = n_array_nth(dbpkg->pkg->fl, j);
                    char *path, *endp;
                    int path_left_size;
                    
                    endp = path = &cap->_buf[1];
                    
                    if (n_array_bsearch(ps->depdirs, flent->dirname) == NULL)
                        continue;
                    
                    if (*flent->dirname != '/')
                        *endp++ = '/';
                            
                    endp = n_strncpy(endp, flent->dirname, PATH_MAX);
                    if (*(endp - 1) != '/')
                            *endp++ = '/';
                    
                    path_left_size = PATH_MAX - (endp - path);
                    
                    for (k=0; k < flent->items; k++) {
                        struct flfile *file = flent->files[k];
                        
                        n_strncpy(endp, file->basename, path_left_size);
                        verify_unistalled_cap(indent, cap, dbpkg->pkg, ps, upg);
                    }
                }
            }
            
            n += process_pkg_orphans(dbpkg->pkg, upg);
        }
    }

    if (n) 
        for (i=0; i<n_array_size(upg->orphan_dbpkgs); i++) {
            struct dbpkg *dbpkg = n_array_nth(upg->orphan_dbpkgs, i);
            int process_as;
            
            if (dbpkg->flags & DBPKG_DEPS_PROCESSED)
                continue;
            dbpkg->flags |= DBPKG_DEPS_PROCESSED;
#if 0
            if ((pkg = is_pkg_obsoletedby_installset(ps, dbpkg->pkg))) {
                process_as = PROCESS_AS_NEW;
                
            } else
#endif                
             {
                pkg = dbpkg->pkg;
                process_as = PROCESS_AS_ORPHAN;
             }
            
            process_pkg_deps(indent, pkg, ps, upg, process_as);
        }
}


static
int process_pkg_reqs(int indent, struct pkg *pkg, struct pkgset *ps,
                     struct upgrade_s *upg, int process_as) 
{
    int i;

    if (upg->nerr_fatal)
        return 0;

    if (pkg->reqs == NULL)
        return 1;
    
    for (i=0; i < n_array_size(pkg->reqs); i++) {
        struct capreq *req;
        struct pkg    *tomark = NULL;
        char          *reqname;
        
        req = n_array_nth(pkg->reqs, i);

        if (capreq_is_rpmlib(req)) 
            continue;
        
        reqname = capreq_name(req);
        if (capreq_has_ver(req)) {
            reqname = alloca(256);
            capreq_snprintf(reqname, 256, req);
        }

        //printf("REQ = %s\n", reqname);

        if (find_req(pkg, req, ps, &tomark)) {
            if (tomark == NULL) {
                msg_i(3, indent, "%s: satisfied by install set\n",
                      capreq_snprintf_s(req));
                continue;
            }
        }
        
        /* don't check foreign dependencies */
        if (process_as == PROCESS_AS_ORPHAN) {
            if (db_deps_provides_req(upg->db_deps, req, DBDEP_FOREIGN)) {
                msg_i(3, indent, "%s: %s skipped foreign req [cached]\n",
                      pkg_snprintf_s(pkg), reqname);
                continue;
                
            } else if (!dbpkg_set_provides(upg->uninst_set, req)) {
                msg_i(3, indent, "%s: %s skipped foreign req\n",
                      pkg_snprintf_s(pkg), reqname);

                db_deps_add(upg->db_deps, req, pkg, tomark,
                            process_as | DBDEP_FOREIGN);
                continue;
            }
        }
#if 0                           /* hungry update */
        /* in "to-install" set */
        if (0 && tomark && pkg_is_marked_i(tomark) && !pkg_has_badreqs(tomark) &&
            upg->nmarked < 10) { 
            msg_i(1, indent, "%s 'MARKS' => %s (cap %s)\n", pkg_snprintf_s(pkg),
                  pkg_snprintf_s0(tomark), capreq_snprintf_s(req));
            
            mark_package(tomark, upg);
            process_pkg_deps(tomark, ps, upg, PROCESS_AS_NEW, -1);
        }
        //else 
        
#endif

        /* cached */
        if (db_deps_provides_req(upg->db_deps, req, DBDEP_DBSATISFIED)) { 
            msg_i(3, indent, "%s: satisfied by db [cached]\n",
                  capreq_snprintf_s(req));
            
        } else if (pkgdb_match_req(upg->inst->db, req, upg->strict,
                                   upg->uninst_set->dbpkgs)) {
            msg_i(3, indent, "%s: satisfied by db\n", capreq_snprintf_s(req));
            db_deps_add(upg->db_deps, req, pkg, tomark, process_as | DBDEP_DBSATISFIED);
                    
        } else if (tomark && (upg->inst->flags & INSTS_FOLLOW)) {
            message_depmark(indent, pkg, tomark, req, process_as);
            if (dep_mark_package(tomark, pkg, req, upg)) 
                process_pkg_deps(indent, tomark, ps, upg, PROCESS_AS_NEW);
            
        } else {
            if (process_as == PROCESS_AS_NEW) {
                log(LOGERR, "%s: req %s not found\n",
                    pkg_snprintf_s(pkg), capreq_snprintf_s(req));
                pkg_set_badreqs(pkg);
                upg->nerr_dep++;
                
                
            } else if (process_as == PROCESS_AS_ORPHAN) {
                int not_found = 1;
                struct pkg *p;
                
                
                if ((p = find_pkg(pkg->name, ps->pkgs, upg))) {
                    if (pkg_is_marked_i(p)) 
                        mark_package(p, upg);
                        
                    if (pkg_is_marked(p)) {
                        process_pkg_deps(-2, p, ps, upg, PROCESS_AS_NEW);
                        not_found = 0;
                        
                    } else if ((upg->inst->flags & INSTS_GREEDY)) {
                        n_assert(!pkg_is_marked(p));
                        do_greedymark(indent, p, pkg, req, ps, upg);
                        not_found = 0;
                        return 1;
                    }
                }
                
                if (not_found) {
                    log(LOGERR, "%s is required by %s\n",
                        capreq_snprintf_s(req), pkg_snprintf_s(pkg));
                    pkg_set_badreqs(pkg);
                    upg->nerr_dep++;
                }
            }
        }
    }

    return 1;
}


static
int process_pkg_deps(int indent, struct pkg *pkg, struct pkgset *ps,
                     struct upgrade_s *upg, int process_as) 
{
    n_assert(process_as == PROCESS_AS_NEW || process_as == PROCESS_AS_ORPHAN);
    
    if (upg->nerr_fatal)
        return 0;

    indent += 2;
    if (!pkg_is_color(pkg, PKG_COLOR_WHITE)) { /* processed */
        //msg_i(1, indent, "CHECKED%s%s dependencies...\n",
        //      process_as == PROCESS_AS_ORPHAN ? " orphaned ":" ",
        //      pkg_snprintf_s(pkg));
        return 0;
    }

    if (process_as == PROCESS_AS_NEW)
        n_array_push(upg->pkg_stack, pkg);

    DBGF("PROCESSING [%d] %s as %s\n", indent, pkg_snprintf_s(pkg),
         process_as == PROCESS_AS_NEW ? "NEW" : "ORPHAN");
    msg_i(3, indent, "Checking%s%s dependencies...\n",
          process_as == PROCESS_AS_ORPHAN ? " orphaned ":" ",
          pkg_snprintf_s(pkg));

    pkg_set_color(pkg, PKG_COLOR_GRAY); /* dep processed */

    if (process_as == PROCESS_AS_NEW) 
        process_pkg_obsl(indent, pkg, ps, upg);

    if (pkg->reqs)
        process_pkg_reqs(indent, pkg, ps, upg, process_as);

    if (process_as == PROCESS_AS_NEW) {
        process_pkg_conflicts(indent, pkg, ps, upg);
        //process_pkg_obsl(pkg, ps, upg, indent);
    }

    DBGF("END PROCESSING [%d] %s as %s\n", indent, pkg_snprintf_s(pkg),
         process_as == PROCESS_AS_NEW ? "NEW" : "ORPHAN");
    
    if (process_as == PROCESS_AS_NEW)
        n_array_pop(upg->pkg_stack);
    return 1;
}

static
int mark_by_conflict(int indent,
                     struct pkg *pkg, const struct capreq *cnfl,
                     struct dbpkg *dbpkg, struct pkgset *ps,
                     struct upgrade_s *upg) 
{
    struct pkg *tomark = NULL;
    struct capreq *req = (struct capreq*)cnfl;
    int found = 0;

    if (!capreq_versioned(req))
        return 0;
#if 0    
    printf("B %s -> ", capreq_snprintf_s(req));
    capreq_revrel(req);
    printf("%s -> ", capreq_snprintf_s(req));
    capreq_revrel(req);
    printf("%s\n", capreq_snprintf_s(req));
#endif

    DBGF("find_req %s %s\n", pkg_snprintf_s(pkg), capreq_snprintf_s(req));
    capreq_revrel(req);
    DBGF("find_req %s %s\n", pkg_snprintf_s(pkg), capreq_snprintf_s(req));
    
    found = find_req(pkg, req, ps, &tomark);
    capreq_revrel(req);
    if (!found)
        return 0;
        
    
    if (tomark == NULL)
        return 0;

    found = 0;
    
    if (pkg_obsoletes_pkg(tomark, dbpkg->pkg)) {
        if (pkg_is_marked_i(tomark)) {
            //    msg_i(1, indent, "%s 'MARX' => %s (cnfl %s)\n",
            //      pkg_snprintf_s(pkg), pkg_snprintf_s0(tomark),
            //      capreq_snprintf_s(req));
            found = mark_package(tomark, upg);
            indent = -2;
                
        } else {
            message_depmark(indent, pkg, tomark, req, PROCESS_AS_NEW);
            found = dep_mark_package(tomark, pkg, req, upg);
        }
        
        if (found)
            process_pkg_deps(indent, tomark, ps, upg, PROCESS_AS_NEW);
    }
    
    return found;
}

/* rpmlib() detects conflicts internally, header*() API usage is too slow */
#undef ENABLE_FILES_CONFLICTS
//#define ENABLE_FILES_CONFLICTS
#ifdef ENABLE_FILES_CONFLICTS
static
int is_file_conflict(const struct pkg *pkg,
                     const char *dirname, const struct flfile *flfile,
                     struct dbpkg *dbpkg) 
{
    int i, j, is_cnfl = 0;
    
    for (i=0; i < n_array_size(dbpkg->pkg->fl); i++) {
        struct pkgfl_ent *flent = n_array_nth(dbpkg->pkg->fl, i);

        if (strcmp(flent->dirname, dirname) != 0)
            continue;

        for (j=0; j < flent->items; j++) {
            struct flfile *file = flent->files[j];
            if (strcmp(file->basename, flfile->basename) != 0)
                continue;
            printf("is_file_conflict %s %s\n", dirname, flfile->basename);
            if ((is_cnfl = flfile_cnfl(flfile, file, 0)))
                goto l_end;
        }

        
        
    }

 l_end:
    if (is_cnfl) {
        log(LOGERR, "%s: /%s/%s%s: conflicts with %s's one\n",
            pkg_snprintf_s(pkg),
            dirname, flfile->basename,
            S_ISDIR(flfile->mode) ? "/" : "", dbpkg_snprintf_s(dbpkg));
            
    } else if (verbose > 1) {
        msg(2, "/%s/%s%s: shared between %s and %s\n",
            dirname, flfile->basename,
            S_ISDIR(flfile->mode) ? "/" : "",
            pkg_snprintf_s(pkg), dbpkg_snprintf_s(dbpkg));
    }
    
    return is_cnfl;
}


static 
int find_file_conflicts(struct pkgfl_ent *flent, struct pkg *pkg,
                        struct pkgdb *db, tn_array *uninst_dbpkgs,
                        int strict) 
{
    int i, j, ncnfl = 0;
    
    for (i=0; i<flent->items; i++) {
        tn_array *cnfldbpkgs = NULL;
        char path[PATH_MAX];
        
        snprintf(path, sizeof(path), "/%s/%s", flent->dirname,
                 flent->files[i]->basename);


        //if (strcmp(path, "/usr/X11R6/lib/X11/XErrorDB") == 0)
          
        
        //flent->files[i]->basename
        cnfldbpkgs = rpm_get_file_conflicted_dbpkgs(db->dbh, flent->files[i]->basename,
                                                    cnfldbpkgs, 
                                                    uninst_dbpkgs, PKG_LDWHOLE);
        
        printf("** PATH = %s -> %d ", path, cnfldbpkgs ? n_array_size(cnfldbpkgs):0);
        
        if (cnfldbpkgs == NULL)
            cnfldbpkgs = rpm_get_file_conflicted_dbpkgs(db->dbh, path, cnfldbpkgs, 
                                                        uninst_dbpkgs, PKG_LDWHOLE);

        printf("-> %d\n", cnfldbpkgs ? n_array_size(cnfldbpkgs):0);
        if (cnfldbpkgs == NULL) 
            continue;
        
        for (j=0; j<n_array_size(cnfldbpkgs); j++) {
            struct dbpkg *dbpkg = n_array_nth(cnfldbpkgs, j);
            printf("CHECK = %s against %s\n", path, dbpkg_snprintf_s(dbpkg));
            ncnfl += is_file_conflict(pkg, flent->dirname, flent->files[i],
                                      n_array_nth(cnfldbpkgs, j));
        }
        
        n_array_free(cnfldbpkgs);
    }
    
    return ncnfl;
}



static
int find_db_files_conflicts(struct pkg *pkg, struct pkgdb *db, struct pkgset *ps, 
                            tn_array *uninst_dbpkgs, int strict)
{
    tn_array *fl;
    int i, ncnfl = 0;

    
    if (pkg->fl)
        for (i=0; i < n_array_size(pkg->fl); i++) {
            ncnfl += find_file_conflicts(n_array_nth(pkg->fl, i), pkg, db,
                                         uninst_dbpkgs, strict);
        }

    if (ncnfl)                  /* skip the rest conflicts test */
        return ncnfl;
    
    n_assert(pkg->pkgdir->vf->vf_stream != NULL);
    
    fseek(pkg->pkgdir->vf->vf_stream, pkg->other_files_offs, SEEK_SET);

    fl = pkgfl_restore_f(pkg->pkgdir->vf->vf_stream, ps->depdirs, 0);
    for (i=0; i < n_array_size(fl); i++) {
        ncnfl += find_file_conflicts(n_array_nth(fl, i), pkg, db,
                                     uninst_dbpkgs, strict);
    }
    
    n_array_free(fl);
    return ncnfl;
}
#endif /* ENABLE_FILES_CONFLICTS */


/* check if cnfl conflicts with db */
static
int find_db_conflicts_cnfl_w_db(int indent,
                                struct pkg *pkg,
                                const struct capreq *cnfl,
                                tn_array *dbpkgs,
                                struct pkgset *ps, struct upgrade_s *upg)
{
    int i, ncnfl = 0;
    
    for (i=0; i<n_array_size(dbpkgs); i++) {
        struct dbpkg *dbpkg = n_array_nth(dbpkgs, i);
        
        msg_i(6, indent, "%d. %s (%s) <-> %s ?\n", i, pkg_snprintf_s(pkg),
            capreq_snprintf_s(cnfl), pkg_snprintf_s0(dbpkg->pkg));
        
        if (pkg_match_req(dbpkg->pkg, cnfl, 1)) {
            if (!mark_by_conflict(indent, pkg, cnfl, dbpkg, ps, upg)) {
                log(LOGERR, "%s (cnfl %s) conflicts with installed %s\n",
                    pkg_snprintf_s(pkg), capreq_snprintf_s(cnfl),
                    pkg_snprintf_s0(dbpkg->pkg));
                ncnfl++;
            }
        }
    }
    
    return ncnfl;
}

/* check if db cnfl conflicts with cap */
static
int find_db_conflicts_dbcnfl_w_cap(int indent,
                                   struct pkg *pkg, const struct capreq *cap,
                                   tn_array *dbpkgs,
                                   struct pkgset *ps, struct upgrade_s *upg) 
{
    int i, j, ncnfl = 0;


    for (i = 0; i<n_array_size(dbpkgs); i++) {
        struct dbpkg *dbpkg = n_array_nth(dbpkgs, i);
        
        msg(6, "%s (%s) <-> %s ?\n", pkg_snprintf_s(pkg),
            capreq_snprintf_s(cap), pkg_snprintf_s0(dbpkg->pkg));
        
        for (j = 0; j < n_array_size(dbpkg->pkg->cnfls); j++) {
            struct capreq *cnfl = n_array_nth(dbpkg->pkg->cnfls, j);
            if (cap_match_req(cap, cnfl, 1)) {
                if (!mark_by_conflict(indent, pkg, cnfl, dbpkg, ps, upg)) {
                    log(LOGERR, "%s (cap %s) conflicts with installed %s (%s)\n",
                        pkg_snprintf_s(pkg), capreq_snprintf_s(cap), 
                        pkg_snprintf_s0(dbpkg->pkg), capreq_snprintf_s0(cnfl));
                    ncnfl++;
                }
            }
        }
    }
    
    return ncnfl;
}

static
int process_pkg_conflicts(int indent, struct pkg *pkg, struct pkgset *ps,
                          struct upgrade_s *upg)
{
    rpmdb dbh;
    int i, n, ncnfl;

    dbh = upg->inst->db->dbh;

    /* conflicts in install set */
    if (pkg->cnflpkgs != NULL)
        for (i = 0; i < n_array_size(pkg->cnflpkgs); i++) {
            struct cnflpkg *cpkg = n_array_nth(pkg->cnflpkgs, i);
            
            if (pkg_is_marked(cpkg->pkg)) {
                log(LOGERR, "%s conflicts with %s\n",
                    pkg_snprintf_s(pkg), pkg_snprintf_s0(cpkg->pkg));
                upg->nerr_cnfl++;
                ncnfl++;
            }
        }

    
    /* conflicts with db packages */

    for (i = 0; i < n_array_size(pkg->caps); i++) {
        struct capreq *cap = n_array_nth(pkg->caps, i);
        tn_array *dbpkgs;

        msg_i(3, indent, "cap %s\n", capreq_snprintf_s(cap));
        dbpkgs = rpm_get_conflicted_dbpkgs(dbh, cap,
                                           upg->uninst_set->dbpkgs,
                                           PKG_LDWHOLE);
        if (dbpkgs == NULL)
            continue;
            
            
        n = find_db_conflicts_dbcnfl_w_cap(indent, pkg, cap, dbpkgs, ps, upg);
        upg->nerr_cnfl += n;
        upg->nerr_dbcnfl += n;
        ncnfl += n;
        n_array_free(dbpkgs);
        if (n)
            pkg_set_badreqs(pkg);
    }
        
        
    if (pkg->cnfls != NULL)
        for (i = 0; i < n_array_size(pkg->cnfls); i++) {
            struct capreq *cnfl = n_array_nth(pkg->cnfls, i);
            tn_array *dbpkgs;
                
            if (cnfl_is_obsl(cnfl))
                continue;

            msg_i(3, indent, "cnfl %s\n", capreq_snprintf_s(cnfl));
                
            dbpkgs = rpm_get_provides_dbpkgs(dbh, cnfl,
                                             upg->uninst_set->dbpkgs,
                                             PKG_LDWHOLE);
            if (dbpkgs == NULL)
                continue;
                
            n = find_db_conflicts_cnfl_w_db(indent, pkg, cnfl, dbpkgs, ps, upg);
            upg->nerr_cnfl += n;
            upg->nerr_dbcnfl += n;
            ncnfl += n;
            n_array_free(dbpkgs);
                
            if (n)
                pkg_set_badreqs(pkg);
        }
        
#ifdef ENABLE_FILES_CONFLICTS  /* too slow, needs rpmlib API modifcations */
    msg(1, "%s's files...\n", pkg_snprintf_s(pkg));
    ncnfl += find_db_files_conflicts(pkg, upg->inst->db, ps, 
                                     upg->uninst_set->dbpkgs, upg->strict);
#endif        

        
    return ncnfl == 0;
}


#if 0
static
int find_conflicts(struct upgrade_s *upg, int *install_set_cnfl) 
{
    int i, j, ncnfl = 0, nisetcnfl = 0;
    rpmdb dbh;

    dbh = upg->inst->db->dbh;
    
    for (i=0; i<n_array_size(upg->install_pkgs); i++) {
        struct pkg *pkg = n_array_nth(upg->install_pkgs, i);
        
        msg(3, " checking %s\n", pkg_snprintf_s(pkg));
        
        if (pkg->cnflpkgs != NULL)
            for (j = 0; j < n_array_size(pkg->cnflpkgs); j++) {
                struct cnflpkg *cpkg = n_array_nth(pkg->cnflpkgs, j);
                
                if (pkg_is_marked(cpkg->pkg)) {
                    log(LOGERR, "%s conflicts with %s\n",
                        pkg_snprintf_s(pkg), pkg_snprintf_s0(cpkg->pkg));
                    ncnfl++;
                    nisetcnfl++;
                }
            }

        for (j = 0; j < n_array_size(pkg->caps); j++) {
            struct capreq *cap = n_array_nth(pkg->caps, j);
            tn_array *dbpkgs;

            msg_i(3, 3, "cap %s\n", capreq_snprintf_s(cap));
            dbpkgs = rpm_get_conflicted_dbpkgs(dbh, cap,
                                               upg->uninst_set->dbpkgs,
                                               PKG_LDWHOLE);
            if (dbpkgs == NULL)
                continue;
            
            
            ncnfl += find_db_conflicts2(pkg, cap, dbpkgs, 0);
            n_array_free(dbpkgs);
        }
        
        
        if (pkg->cnfls != NULL)
            for (j = 0; j < n_array_size(pkg->cnfls); j++) {
                struct capreq *cnfl = n_array_nth(pkg->cnfls, j);
                tn_array *dbpkgs;
                
                if (cnfl_is_obsl(cnfl))
                    continue;

                msg_i(3, 3, "cnfl %s\n", capreq_snprintf_s(cnfl));
                
                dbpkgs = rpm_get_provides_dbpkgs(dbh, cnfl,
                                                 upg->uninst_set->dbpkgs,
                                                 PKG_LDWHOLE);
                if (dbpkgs != NULL) {
                    ncnfl += find_db_conflicts(pkg, cnfl, dbpkgs, 1);
                    n_array_free(dbpkgs);
                }
            }
        
#ifdef ENABLE_FILES_CONFLICTS  /* too slow, needs rpmlib API modifcations */
        msg_i(1, 3, "files...\n");
        ncnfl += find_db_files_conflicts(pkg, upg->inst->db,
                                         upg->uninst_set->dbpkgs, upg->strict);
#endif        
    }
    
    *install_set_cnfl = nisetcnfl;
    return ncnfl;
}
#endif


static int valid_arch_os(tn_array *pkgs) 
{
    int i, nerr = 0;
    
    for (i=0; i<n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        if (pkg->arch && !rpmMachineScore(RPM_MACHTABLE_INSTARCH, pkg->arch)) {
            log(LOGERR, "%s: package is for a different architecture (%s)\n",
                pkg_snprintf_s(pkg), pkg->arch);
            nerr++;
        }
        
        if (pkg->os && !rpmMachineScore(RPM_MACHTABLE_INSTOS, pkg->os)) {
            log(LOGERR, "%s: package is for a different operating system (%s)\n",
                pkg_snprintf_s(pkg), pkg->os);
            nerr++;
        }
    }
    
    return nerr == 0;
}


static void print_install_summary(struct upgrade_s *upg) 
{
    int i, n;

    n = n_array_size(upg->install_pkgs);
    msg(1, "There are %d package%s to install", n, n > 1 ? "s":"");
    
    
    if (upg->ndep) 
        msg(1, "_ (%d marked by dependencies)", upg->ndep);

    if (n_array_size(upg->uninst_set->dbpkgs))
        msg(1, "_, %d to uninstall", n_array_size(upg->uninst_set->dbpkgs));
    msg(1, "_:\n");
    
    
    for (i=0; i<n_array_size(upg->install_pkgs); i++) {
        struct pkg *pkg = n_array_nth(upg->install_pkgs, i);
        msg(1, "%c %s\n", pkg_is_dep_marked(pkg) ? 'D' : 'I',
            pkg_snprintf_s(pkg));
    }

    for (i=0; i<n_array_size(upg->uninst_set->dbpkgs); i++) {
        struct dbpkg *dbpkg = n_array_nth(upg->uninst_set->dbpkgs, i);
        msg(1, "R %s\n", pkg_snprintf_s(dbpkg->pkg));
    }
    
}


static int check_holds(struct pkgset *ps, struct upgrade_s *upg)
{
    int i, j, rc = 1;

    if ((ps->flags & (PSMODE_UPGRADE | PSMODE_UPGRADE_DIST)) == 0)
        return 1;

    if (upg->inst->hold_pkgnames == NULL)
        return 1;

    for (i=0; i < n_array_size(upg->uninst_set->dbpkgs); i++) {
        struct dbpkg *dbpkg = n_array_nth(upg->uninst_set->dbpkgs, i);

        for (j=0; j<n_array_size(upg->inst->hold_pkgnames); j++) {
            const char *mask = n_array_nth(upg->inst->hold_pkgnames, j);

            if (fnmatch(mask, dbpkg->pkg->name, 0) == 0) {
                log(LOGERR, "%s: refusing to uninstall held package\n",
                    pkg_snprintf_s(dbpkg->pkg));
                rc = 0;
            }
        }
    }
    
    return rc;
}

static void mapfn_clean_pkg_color(struct pkg *pkg) 
{
    pkg_set_color(pkg, PKG_COLOR_WHITE);
}


/* process packages to install:
   - check dependencies
   - mark unresolved dependencies finded in pkgset
   - check conflicts
 */
static
int do_install(struct pkgset *ps, struct upgrade_s *upg)
{
    int rc, nerr = 0, any_err = 0;
    tn_array *pkgs;
    int i;

    msg(1, "Processing dependencies...\n");
    n_array_map(ps->pkgs, (tn_fn_map1)mapfn_clean_pkg_color);

    for (i = n_array_size(ps->ordered_pkgs) - 1; i > -1; i--) {
        struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
        if (pkg_is_hand_marked(pkg)) 
            process_pkg_deps(-2, pkg, ps, upg, PROCESS_AS_NEW);
    }

    pkgs = n_array_new(64, NULL, NULL);
    for (i = n_array_size(ps->ordered_pkgs) - 1; i > -1; i--) {
        struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
        if (pkg_is_marked(pkg))
            n_array_push(pkgs, pkg);
    }
    
    if (upg->nerr_fatal)
        return 0;

    print_install_summary(upg);
    pkgdb_closedb(upg->inst->db); /* close db as soon as possible */

    if (upg->nerr_dep || upg->nerr_cnfl) {
        char errmsg[256];
        int n = 0;
        
        any_err++;
        if (upg->nerr_dep) {
            n = snprintf(&errmsg[n], sizeof(errmsg) - n,
                         "%d unresolved dependencies", upg->nerr_dep);
            
            
            if (upg->inst->instflags & (PKGINST_NODEPS | PKGINST_TEST))
                upg->nerr_dep = 0;
            else
                nerr++;
        }
        
        if (upg->nerr_cnfl) {
            n = snprintf(&errmsg[n], sizeof(errmsg) - n,
                         "%s%d conflicts", n ? ", ":"", upg->nerr_cnfl);
            if (upg->inst->instflags & (PKGINST_FORCE | PKGINST_TEST)) 
                upg->nerr_cnfl = 0;
            else
                nerr++;
        }

        log(LOGERR, "There are errors: %s\n", errmsg);
    }
    
    
    rc = (any_err == 0);
    if (nerr)
        return 0;
    
    if ((upg->inst->flags & (INSTS_JUSTPRINT | INSTS_JUSTFETCH)) == 0)
        if (!valid_arch_os(upg->install_pkgs)) 
            return 0;
    
#if 0                           /* temporary commented out */
    if (upg->inst->ask_fn) {
        if (!upg->inst->ask_fn("Proceed?"))
            return 1;
    }
#endif

    if ((upg->inst->flags & INSTS_TEST) &&
        (upg->inst->instflags & PKGINST_TEST) == 0)
        return rc;
    
    if (upg->inst->flags & INSTS_JUSTPRINT) {
        rc = pkgset_dump_marked_fqpns(ps, upg->inst->dumpfile);
        
    } else if (upg->inst->flags & INSTS_JUSTFETCH) {
        n_assert(upg->inst->fetchdir);
        rc = packages_fetch(upg->install_pkgs, upg->inst->fetchdir, 1);
        
    } else if ((upg->inst->flags & INSTS_NOHOLD) || (rc=check_holds(ps, upg))) {
        rc = packages_rpminstall(pkgs, ps, upg->inst);
    }
    
    return rc;
}

static void init_upgrade_s(struct upgrade_s *upg, struct pkgset *ps,
                           struct inst_s *inst)
{
    upg->avpkgs = ps->pkgs;
    upg->install_pkgs = n_array_new(128, NULL, NULL);
    upg->db_deps = db_deps_new();
    upg->uninst_set = dbpkg_set_new();
    upg->orphan_dbpkgs = dbpkg_array_new(128);

    upg->strict = ps->flags & PSVERIFY_MERCY ? 0 : 1;
    upg->ndberrs = upg->ndep = upg->ninstall = upg->nmarked = 0;
    upg->nerr_dep = upg->nerr_cnfl = upg->nerr_dbcnfl = upg->nerr_fatal = 0;
    upg->inst = inst; 
    upg->pkgflmod_mark = pkgflmodule_allocator_push_mark();
    upg->pkg_stack = n_array_new(32, NULL, NULL);
}


static void destroy_upgrade_s(struct upgrade_s *upg)
{
    upg->avpkgs = NULL;
    n_array_free(upg->install_pkgs);
    n_hash_free(upg->db_deps);
    dbpkg_set_free(upg->uninst_set);
    n_array_free(upg->orphan_dbpkgs);
    upg->inst = NULL;
    
    pkgflmodule_allocator_pop_mark(upg->pkgflmod_mark);
    memset(upg, 0, sizeof(*upg));
}


static void reset_upgrade_s(struct upgrade_s *upg)
{

    n_array_clean(upg->install_pkgs);
    
    n_hash_clean(upg->db_deps);
    dbpkg_set_free(upg->uninst_set);
    upg->uninst_set = dbpkg_set_new();
    n_array_clean(upg->orphan_dbpkgs);
    
    upg->ndberrs = upg->ndep = upg->ninstall = upg->nmarked = 0;
    upg->nerr_dep = upg->nerr_cnfl = upg->nerr_dbcnfl = upg->nerr_fatal = 0;

    pkgflmodule_allocator_pop_mark(upg->pkgflmod_mark);
    upg->pkgflmod_mark = pkgflmodule_allocator_push_mark();
}


static 
void mapfn_mark_newer_pkg(unsigned recno, void *h, void *upgptr) 
{
    struct upgrade_s  *upg = upgptr;
    struct pkg        *pkg, tmpkg;
    uint32_t          *epoch;
    int               i, cmprc;

    
    recno = recno;
    
    if (!rpmhdr_nevr(h, &tmpkg.name, &epoch, &tmpkg.ver, &tmpkg.rel)) {
        log(LOGERR, "db package header corrupted (!?)\n");
        upg->ndberrs++;
        return;
    }

    tmpkg.epoch = epoch ? *epoch : 0;
    i = n_array_bsearch_idx_ex(upg->avpkgs, &tmpkg, (tn_fn_cmp)pkg_cmp_name); 
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
            msg(3, "%-32s newer than repository one\n", pkg_snprintf_s(&tmpkg));
        
        else 
            msg(2, "%-32s -> %-30s\n", pkg_snprintf_s(&tmpkg),
                pkg_snprintf_s0(pkg));
    }

    if ((pkg = n_hash_get(upg->db_pkgs, tmpkg.name))) {
        if (pkg_is_marked(pkg)) {
            upg->nmarked--;
            log(LOGWARN, "%s: multiple instances installed, skipped\n", tmpkg.name);
            pkg_unmark(pkg);        /* display above once */
        }

        return;
    }

    pkg = n_array_nth(upg->avpkgs, i);
    if (cmprc > 0) {
        if (pkg_is_hold(pkg)) {
            msg(0, "%s: skip held package\n", pkg_snprintf_s(pkg));
            
        } else {
            n_hash_insert(upg->db_pkgs, tmpkg.name, pkg);
            pkg_hand_mark(pkg);
            upg->nmarked++;
        }
    }
    
}

void mapfn_unmark_pkg(const char *key, void *pkgptr) 
{
    struct pkg *pkg = pkgptr;
    key = key;
    pkg_unmark(pkg);
}


int pkgset_upgrade_dist(struct pkgset *ps, struct inst_s *inst) 
{
    struct upgrade_s upg;

    
    init_upgrade_s(&upg, ps, inst);
    upg.db_pkgs = n_hash_new(103, NULL);
    
    msg(1, "Looking up packages for upgrade...\n");
    pkgdb_map(inst->db, mapfn_mark_newer_pkg, &upg);
    n_hash_free(upg.db_pkgs);
               
    if (upg.ndberrs) {
        log(LOGERR, "There are database errors (?), give up\n");
        destroy_upgrade_s(&upg);
        return 0;
    }

    destroy_upgrade_s(&upg);
    
    if (upg.nmarked > 0) {
        msg(0, "Nothing to do\n");
        return 1;
    } 
    
    return pkgset_install(ps, inst, NULL);
}


static
int install(struct pkgset *ps, struct upgrade_s *upg,
            tn_array *uninstalled_pkgs)
{
    int i, rc = 1, is_upgrade = 0;

    is_upgrade = ps->flags & PSMODE_UPGRADE;
    
    mem_info(1, "ENTER pkgset_install: install()");
    
    if ((rc = do_install(ps, upg)) > 0 && uninstalled_pkgs) {
        for (i=0; i<n_array_size(upg->uninst_set->dbpkgs); i++) {
            struct dbpkg *dbpkg = n_array_nth(upg->uninst_set->dbpkgs, i);
            struct pkg *pkg = dbpkg->pkg;
            n_array_push(uninstalled_pkgs,
                         pkg_new(pkg->name, pkg->epoch, pkg->ver, pkg->rel,
                                 pkg->arch, pkg->os, pkg->size, pkg->fsize,
                                 pkg->btime));
                    
        }
    }
    
    return rc;
}



static void mark_namegroup(tn_array *pkgs, struct pkg *pkg, struct upgrade_s *upg) 
{
    struct pkg tmpkg;
    int n, i, len;
    char *p, prefix[512];


    n_array_sort(pkgs);
    
    len = snprintf(prefix, sizeof(prefix), "%s", pkg->name);
    if ((p = strchr(prefix, '-')))
        *p = '\0';
    
    tmpkg.name = prefix;
    
    n = n_array_bsearch_idx_ex(pkgs, &tmpkg, (tn_fn_cmp)pkg_cmp_name); 
    if (n < 0 && p) {
        *p = '-';
        n = n_array_bsearch_idx_ex(pkgs, &tmpkg, (tn_fn_cmp)pkg_cmp_name);
    }

    if (n < 0)
        return;
    
    len = strlen(prefix);
    
    for (i = n; i < n_array_size(pkgs); i++) {
        struct pkg *p = n_array_nth(pkgs, i);
        int pkg_name_len;

        if ((pkg_name_len = strlen(pkg->name)) < len)
            break;
        
        if (strncmp(p->name, prefix, len) != 0) 
            break;

        if (!pkg_is_marked_i(p)) 
            continue;
        
        if (pkg->pkgdir != p->pkgdir)
            continue;

        if (!pkg_is_marked(p))
            mark_package(p, upg);
    }
}


int pkgset_install(struct pkgset *ps, struct inst_s *inst,
                   tn_array *uninstalled_pkgs)
{
    int i, is_upgrade = 0, nmarked = 0, nerr = 0, n;
    struct upgrade_s upg;

    is_upgrade = ps->flags & PSMODE_UPGRADE;
    
    mem_info(1, "ENTER pkgset_install:");
    init_upgrade_s(&upg, ps, inst);

    inst->instflags |= INSTS_PARTITIONED;
    
    
    /* tests make sense on whole set only  */
    if ((inst->flags & INSTS_TEST) || (inst->instflags & PKGINST_TEST))
        inst->instflags &= ~INSTS_PARTITIONED;

    for (i = 0; i < n_array_size(ps->ordered_pkgs); i++) {
        struct pkg    *pkg = n_array_nth(ps->ordered_pkgs, i);
        int           install;
        
        if (!pkg_is_marked(pkg))
            continue;
        
        install = is_installable(pkg, inst, 1);
        
        pkg_unmark(pkg);
        pkg_clr_badreqs(pkg);
        
        if (install > 0) {
            pkg_mark_i(pkg);
            nmarked++;
        }
    }
    
    if (nmarked == 0) 
        goto l_end;

    if (nmarked == 1)
        inst->instflags &= ~INSTS_PARTITIONED;
    
    n = 1;
    pkgset_mark(ps, PS_MARK_OFF_ALL);
    
    for (i = 0; i < n_array_size(ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);

        if (!pkg_is_marked_i(pkg)) 
            continue;

        if (inst->instflags & INSTS_PARTITIONED) {
            printf_c(PRCOLOR_YELLOW, "Installing set #%d\n", n++);
            pkgdb_reopendb(upg.inst->db);
        }
        
        mark_package(pkg, &upg);

        if (inst->instflags & INSTS_PARTITIONED) {
            mark_namegroup(pkg->pkgdir->pkgs, pkg, &upg);
                
            if (!install(ps, &upg, uninstalled_pkgs))
                nerr++;
            
            pkgset_mark(ps, PS_MARK_OFF_ALL);
            reset_upgrade_s(&upg);
        }
    }

    
    if ((inst->instflags & INSTS_PARTITIONED) == 0) 
        nerr = !install(ps, &upg, uninstalled_pkgs);

 l_end:
    destroy_upgrade_s(&upg);
    mem_info(1, "RETURN pkgset_install:");

    
    return nerr == 0;
}
