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

#include "ictx.h"

/* looks into Obsoletes only */
static
struct pkg *find_supersede_pkg(struct install_ctx *ictx, const struct pkg *pkg)
{
    struct pkg *bypkg = NULL;
    tn_array *pkgs;
    int i, best_i;

    if ((pkgs = pkgset_search(ictx->ps, PS_SEARCH_CAP, pkg->name)) == NULL)
        return NULL;
    
    best_i = in_select_best_pkg(ictx, pkg, pkgs);
    if (best_i == -1) {           /* can happens in multilib mode */
        n_array_free(pkgs);
        return NULL;
    }

    for (i=best_i; i < n_array_size(pkgs); i++) {
        struct pkg *p = n_array_nth(pkgs, i);
            
        if (strcmp(pkg->name, p->name) == 0)
            continue;
#if 0                           /* needless here */
        if (poldek_conf_MULTILIB) {
            if (!pkg_is_colored_like(p, pkg))
                continue;
        }
#endif            
        DBGF("found %s <- %s, %d, %d\n", pkg_id(pkg),
             pkg_id(p),
             pkg_caps_obsoletes_pkg_caps(p, pkg), 
             pkg_caps_obsoletes_pkg_caps(pkg, p));
        
        if (pkg_caps_obsoletes_pkg_caps(p, pkg) &&
            !pkg_caps_obsoletes_pkg_caps(pkg, p)) {
            
            bypkg = p;
            break;
        }
    }
    n_array_free(pkgs);
    
    DBGF("%s -> %s\n", pkg_id(pkg), bypkg ? pkg_id(bypkg) : "NONE");
    return bypkg;
}

static
struct pkg *select_successor(struct install_ctx *ictx, const struct pkg *pkg,
                             int *by_obsoletes)
{
    struct pkg *p = NULL;

    *by_obsoletes = 0;
    p = in_select_pkg(ictx, pkg, ictx->ps->pkgs);
    
    if (!ictx->ts->getop(ictx->ts, POLDEK_OP_OBSOLETES))
        return p;
    
    if ((p == NULL || pkg_cmp_evr(p, pkg) == 0)) {
        p = find_supersede_pkg(ictx, pkg);
        *by_obsoletes = 1;
    }
    
    return p;
}



struct successor {
    struct pkg *realpkg; /* not null if found */
    struct pkg *pkg;     /* not null if found and qualified as successor */
    int by_obsoletes;    /* true if found using Obsoletes */
    int ndragged;        /* number of packages dragged by */
    int searched;        /* true if successor have been searched */
};

static
int process_pkg_req(int indent, struct install_ctx *ictx,
                    struct pkg *pkg, struct capreq *req,
                    struct successor *successor,
                    int process_as) 
{
    struct pkg    *tomark = NULL;
    tn_array      *candidates = NULL;
    char          *reqname;
        
    reqname = capreq_name(req);
    if (capreq_has_ver(req)) {
        reqname = alloca(256);
        capreq_snprintf(reqname, 256, req);
    }

    DBGF("[%s] req %s\n", pkg_id(pkg), capreq_snprintf_s(req));

    if (in_is_user_askable(ictx->ts))
        candidates = pkgs_array_new(8);
    
    if (in_find_req(ictx, pkg, req, &tomark, candidates, IN_FIND_REQ_BEST)) {
        if (tomark == NULL) {
            msg_i(3, indent, "%s: satisfied by already installed set\n",
                  capreq_snprintf_s(req));
            goto l_end;
        }
    }
    
    DBGF("%s: TOMARK %s\n", pkg_id(pkg), tomark ? pkg_id(tomark) : "NULL");

    /* don't check foreign dependencies */
    if (process_as == PROCESS_AS_ORPHAN) {
#if 0   /* buggy,  TODO - unmark foreign on adding to uninst_set */
        if (db_deps_provides(ictx->db_deps, req, DBDEP_FOREIGN)) {
            
            msg_i(3, indent, "%s: %s skipped foreign req [cached]\n",
                  pkg_id(pkg), reqname);
            goto l_end_loop;
        }
#endif
        if (!dbpkg_set_provides(ictx->uninst_set, req)) {
            msg_i(3, indent, "%s: %s skipped foreign req\n",
                  pkg_id(pkg), reqname);
            
            db_deps_add(ictx->db_deps, req, pkg, tomark,
                        process_as | DBDEP_FOREIGN);
            goto l_end;
        }
    }
        
    /* cached?, TOFIX something is wrong with cache in multilib mode */
    if (!poldek_conf_MULTILIB && db_deps_provides(ictx->db_deps, req, DBDEP_DBSATISFIED)) {
        msg_i(3, indent, "%s: satisfied by db [cached]\n",
              capreq_snprintf_s(req));
        DBGF("%s: satisfied by db [cached]\n", capreq_snprintf_s(req));
        
    /* satisfied by db? */
    } else if (pkgdb_match_req(ictx->ts->db, req, ictx->strict,
                               ictx->uninst_set->dbpkgs)) {

        //pkgs_array_dump(ictx->uninst_set->dbpkgs, "UNINST");
        DBGF("%s: satisfied by dbY\n", capreq_snprintf_s(req));
        msg_i(3, indent, "%s: satisfied by db\n", capreq_snprintf_s(req));
        //dbpkg_set_dump(ictx->uninst_set);
        db_deps_add(ictx->db_deps, req, pkg, tomark,
                    process_as | DBDEP_DBSATISFIED);
            
    /* no successor selected and to mark candidates */
    } else if (successor->pkg == NULL && tomark &&
               ictx->ts->getop(ictx->ts, POLDEK_OP_FOLLOW)) {
        
        struct pkg *real_tomark = tomark;
        if (candidates && n_array_size(candidates) > 1) {
            real_tomark = in_choose_equiv(ictx->ts, req, candidates, tomark);
            n_array_cfree(&candidates);
            
            if (real_tomark == NULL) { /* user abort */
                ictx->nerr_fatal++;
                return 0;
            }
        }
            
        if (in_is_marked_for_removal_by_req(ictx, real_tomark, req)) {
            logn(LOGERR, _("%s (cap %s) is required by %s%s"),
                 pkg_id(real_tomark), capreq_snprintf_s(req),
                 (pkg->flags & PKG_DBPKG) ? "" : " already marked", 
                 pkg_id(pkg));
            ictx->nerr_dep++;
                
        } else {
            if (in_dep_mark_package(indent, ictx, real_tomark, pkg, req, process_as)) 
                in_process_package(indent, ictx, real_tomark, PROCESS_AS_NEW);
            }
            
    /* unresolved req */    
    } else { 
        if (process_as == PROCESS_AS_NEW) {
            logn(LOGERR, _("%s: req %s not found"),
                 pkg_id(pkg), capreq_snprintf_s(req));
            pkg_set_unmetdeps(ictx->unmetpms, pkg);
            ictx->nerr_dep++;
                
                
        } else if (process_as == PROCESS_AS_ORPHAN) {
            int not_found = 1, by_obsoletes = 0;
            struct pkg *p;
            
            if (successor->searched) {
                if (successor->realpkg)
                    msg_i(3, indent, "aggresive upgrade %s to %s\n",
                          pkg_id(pkg), pkg_id(successor->realpkg));
                p = successor->realpkg;
                by_obsoletes = successor->by_obsoletes;
                    
            } else {
                p = select_successor(ictx, pkg, &by_obsoletes);
            }
                
            if (p != NULL) {
                if (pkg_is_marked_i(ictx->ts->pms, p) ||
                    (by_obsoletes && !pkg_is_marked(ictx->ts->pms, p)))
                    in_mark_package(ictx, p);
                
                if (pkg_is_marked(ictx->ts->pms, p)) {
                    in_process_package(-2, ictx, p, PROCESS_AS_NEW);
                    not_found = 0;
                    
                } else if (ictx->ts->getop(ictx->ts, POLDEK_OP_GREEDY)) {
                    n_assert(!pkg_is_marked(ictx->ts->pms, p));
                    if (in_greedy_mark_package(indent, ictx, p, pkg, req)) {
                        in_process_package(indent, ictx, p, PROCESS_AS_NEW);
                        not_found = 0;
                    }
                }
            }

            if (not_found) {
                logn(LOGERR, _("%s is required by %s"),
                     capreq_snprintf_s(req), pkg_id(pkg));
                pkg_set_unmetdeps(ictx->unmetpms, pkg);
                ictx->nerr_dep++;
            }
        }
    }
 l_end:
    n_array_cfree(&candidates);

    return 1;
}

/* just append sugs to reqs if user wants to */
static tn_array *process_suggets(struct pkg *pkg, struct poldek_ts *ts) 
{
    tn_array *reqs;
    tn_buf *nbuf;
    char message[2048], *confirmation;
    
    reqs = pkg->reqs;
    
    if (pkg->sugs == NULL || !in_is_user_askable(ts))
        return reqs;
    
    if (!ts->getop(ts, POLDEK_OP_SUGGESTS))
        return reqs;
    
    nbuf = capreq_arr_join(pkg->sugs, NULL, NULL);

    n_snprintf(message, sizeof(message), _("%s suggests installation of: %s"),
               pkg_id(pkg), n_buf_ptr(nbuf));
    n_buf_free(nbuf);
        
    confirmation = ngettext("Try to install it? [N/y]",
                            "Try to install them? [N/y]",
                            n_array_size(pkg->sugs));
            
    if (ts->ask_fn(0, "%s\n%s", message, confirmation)) {
        int i;
        
        reqs = capreq_arr_new(n_array_size(pkg->reqs) + n_array_size(pkg->sugs));
            
        for (i=0; i < n_array_size(pkg->reqs); i++)
            n_array_push(reqs, n_array_nth(pkg->reqs, i));
        
        for (i=0; i < n_array_size(pkg->sugs); i++)
            n_array_push(reqs, n_array_nth(pkg->sugs, i));

        n_array_ctl_set_freefn(reqs, NULL);
    }

    return reqs;
}


int in_process_pkg_requirements(int indent, struct install_ctx *ictx,
                                struct pkg *pkg, int process_as)
{
    struct successor successor;
    tn_array *reqs;
    int i;

    if (sigint_reached())
        return 0;

    if (ictx->nerr_fatal)
        return 0;

    if (pkg->reqs == NULL)
        return 1;

    DBGF("%s, greedy %d\n", pkg_id(pkg),
         ictx->ts->getop(ictx->ts, POLDEK_OP_GREEDY));

    memset(&successor, 0, sizeof(successor));
    if (process_as == PROCESS_AS_ORPHAN &&
        ictx->ts->getop(ictx->ts, POLDEK_OP_AGGREEDY)) {
        int pkg_ndragged;
	
        pkg_ndragged = in_pkg_drags(ictx, pkg);
        if (pkg_ndragged == 0 || 1) { /* XXX: see note in pkgset-install.c */
            struct pkg *p;
            int is_marked = 0, ndragged = 0, by_obsoletes = 0;

            p = select_successor(ictx, pkg, &by_obsoletes);
            if (p && (pkg_is_marked(ictx->ts->pms, p) ||
                      pkg_is_marked_i(ictx->ts->pms, p)))
                is_marked = 1;

            successor.realpkg = p;
            successor.by_obsoletes = by_obsoletes;
            
            /* do not follow successor if it drags more than its predecessor 
               and successor itself is not marked */
            if (p && (ndragged = in_pkg_drags(ictx, p)) > pkg_ndragged && is_marked == 0) {
                DBGF("OMIT select_successor %s -> %s (%d)\n",
                     pkg_id(pkg), pkg_id(p), ndragged);
                p = NULL;
            }
            successor.pkg = p;
            successor.searched = 1;
            successor.ndragged = ndragged;
            
            DBGF("successor of %s is %s, ndragged=%d, marked=%d\n",
                 pkg_id(pkg),
                 successor.realpkg != NULL ? pkg_id(successor.realpkg) : "(null)",
                 ndragged, is_marked);
        }
    }

    reqs = pkg->reqs;
    if (process_as == PROCESS_AS_NEW)
        reqs = process_suggets(pkg, ictx->ts);
    
    for (i=0; i < n_array_size(reqs); i++) {
        struct capreq *req = n_array_nth(reqs, i);

        if (capreq_is_rpmlib(req)) {
            if (process_as == PROCESS_AS_NEW && !capreq_is_satisfied(req)) {
                logn(LOGERR, _("%s: rpmcap %s not found, upgrade rpm."),
                     pkg_id(pkg), capreq_snprintf_s(req));
                pkg_set_unmetdeps(ictx->unmetpms, pkg);
                ictx->nerr_dep++;
            }
            continue;
        }

        /* obsoleted by greedy mark */
        if (process_as == PROCESS_AS_ORPHAN &&
            in_is_marked_for_removal(ictx, pkg)) {
            
            DBGF("%s: obsoleted, return\n", pkg_id(pkg));
            db_deps_remove_pkg(ictx->db_deps, pkg);
            return 1;
        }

        process_pkg_req(indent, ictx, pkg, req, &successor, process_as);
    }
    
    if (reqs != pkg->reqs)      /* suggests was processed */
        n_array_free(reqs);
    
    return 1;
}
