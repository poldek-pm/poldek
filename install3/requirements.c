/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

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

/* detect which package capability has "replaces" meaning, if any */
static const char *get_replacemeant_capname(const struct pkg *pkg) 
{
    int i;
    
    for (i=0; i < n_array_size(pkg->cnfls); i++) {
        struct capreq *cnfl = n_array_nth(pkg->cnfls, i);

        if (capreq_versioned(cnfl) || !capreq_is_obsl(cnfl))
            continue;
        
        if (pkg_caps_match_req(pkg, cnfl, 0)) /* self satisfied? */
            return capreq_name(cnfl);
    }
    
    return NULL;
}

/* PLD uses Obsoletes/Provides pairs as Replaces: capability */
static const char *are_equivalents(const struct pkg *p1, const struct pkg *p2)
{
    const char *cap1, *cap2 = NULL;
    
    /* no Obsoletes? */
    if (p1->cnfls == NULL || p2->cnfls == NULL)
        return NULL;

    if ((cap1 = get_replacemeant_capname(p1)) == NULL)
        return NULL;

    if ((cap2 = get_replacemeant_capname(p2)) == NULL)
        return NULL;

    DBGF("%s %s -> %s\n", pkg_id(p1), pkg_id(p2), cap1);

    if (n_str_ne(cap1, cap2))
        return NULL;
    
    return cap1;
}

static
struct pkg *find_successor_by(int indent, struct i3ctx *ictx,
                              const struct pkg *pkg, enum pkgset_search_tag tag)
{
    struct pkg *bypkg = NULL;
    tn_array *pkgs, *tmp;
    int i, best_i;

    n_assert(tag == PS_SEARCH_OBSL || tag == PS_SEARCH_CAP);
    if ((pkgs = pkgset_search(ictx->ps, tag, pkg->name)) == NULL) {
        tracef(indent, "%s: successor not found", pkg_id(pkg));
        return NULL;
    }

    tracef(indent, "%s: found %d package(s)", pkg_id(pkg), n_array_size(pkgs));
    indent += 1;
    
    /* filter out equivalents */
    tmp = n_array_clone(pkgs);
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *p = n_array_nth(pkgs, i);
        
        if (pkg_cmp_name(pkg, p) == 0) /* same packages */
            continue;
        
        if (are_equivalents(pkg, p)) {
            trace(indent, "- skipped equivalent %s", pkg_id(p));
            continue;
        }

        n_array_push(tmp, pkg_link(p));
        trace(indent, "- trying %s", pkg_id(p));
    }
    n_array_free(pkgs);
    pkgs = tmp;

    if (n_array_size(pkgs) == 0) {
        n_array_free(pkgs);
        tracef(indent, "%s: successor not found", pkg_id(pkg));
        return NULL;
    }

    trace(indent, "testing %d package(s)", n_array_size(pkgs));
    if ((best_i = i3_select_best_pkg(indent + 2, ictx, pkg, pkgs)) == -1) {
       /* can be in multilib mode */
        n_array_free(pkgs);
        return NULL;
    }

    for (i=best_i; i < n_array_size(pkgs); i++) {
        struct pkg *p = n_array_nth(pkgs, i);
            
        if (strcmp(pkg->name, p->name) == 0) /* same packages */
            continue;
#if 0                           /* needless here */
        if (poldek_conf_MULTILIB) {
            if (!pkg_is_colored_like(p, pkg))
                continue;
        }
#endif            
        bypkg = p;
    }
    n_array_free(pkgs);
    
    DBGF("%s -> %s\n", pkg_id(pkg), bypkg ? pkg_id(bypkg) : "NONE");
    return bypkg;
}

struct successor {
    struct pkg *realpkg; /* not null if found */
    struct pkg *pkg;     /* not null if found and qualified as successor */
    int8_t by_obsoletes;    /* true if found using Obsoletes */
};

static struct pkg *find_successor(int indent, struct i3ctx *ictx,
                                  const struct pkg *pkg, struct successor *succ) 
{
    
    struct pkg *p;
    int is_marked = 0, by_obsoletes = 0;
    
    memset(succ, 0, sizeof(*succ));

    if ((p = i3_select_successor(indent, ictx, pkg)) == NULL) {
        if (ictx->ts->getop(ictx->ts, POLDEK_OP_OBSOLETES)) {
            /* anybody provides, or obsoletes me? */
            p = find_successor_by(indent, ictx, pkg, PS_SEARCH_CAP);
            if (p == NULL)
                p = find_successor_by(indent, ictx, pkg, PS_SEARCH_OBSL);
            
            by_obsoletes = 1;
        }
    }
    
    if (p == NULL)
        return NULL;
    
    if (i3_is_marked(ictx, p) || pkg_is_marked_i(ictx->ts->pms, p))
        is_marked = 1;

    succ->realpkg = p;
    succ->by_obsoletes = by_obsoletes;
    succ->pkg = p;
    
    tracef(indent, "successor of %s is %s, marked=%s",
           pkg_id(pkg),
           succ->realpkg != NULL ? pkg_id(succ->realpkg) : "(null)",
           is_marked ? "yes": "no");

    return p;
}


static int try_to_upgrade_orphan(int indent, struct i3ctx *ictx,
                                 struct pkg *pkg, struct capreq *req)
{
    struct successor succ;
    struct pkg *p;
    int install = 0;

    tracef(indent, "%s req: %s\n", pkg_id(pkg), capreq_snprintf_s(req));

    if ((p = find_successor(indent, ictx, pkg, &succ)) == NULL)
        return 0;

    if (pkg_is_marked_i(ictx->ts->pms, p)) /* internally marked */
        install = 1;
    
    else if (succ.by_obsoletes && !i3_is_marked(ictx, p))
        install = 1;
    
    else if (ictx->ts->getop(ictx->ts, POLDEK_OP_GREEDY)) {
        n_assert(!i3_is_marked(ictx, p));
        install = 1;
    }
    
    if (install) {
        struct i3pkg *i3tomark = i3pkg_new(p, 0, pkg, req, I3PKGBY_GREEDY);
        i3_process_package(indent, ictx, i3tomark);
    }

    return install;
}


static int process_orphan_req(int indent, struct i3ctx *ictx,
                               struct pkg *pkg, struct capreq *req)
{
    struct poldek_ts *ts = ictx->ts; /* just for short */
    struct pkg       *tomark = NULL;
    tn_array         *candidates = NULL;
    char             *strreq;
    int              rc = 1, indentt = indent + 1;

    capreq_stra(req, &strreq);
    tracef(indent, "%s, req: %s", pkg_id(pkg), strreq);

    /* skip foreign (not provided by uninstalled) dependencies */
    if (!iset_provides(ictx->unset, req)) {
        logn(LOGERR, "%s: %s skipped foreign requirement "
             "(internal, non-critical, please report it)",
             pkg_id(pkg), strreq);
        goto l_end;
    }
    
    //if (i3_is_user_choosable_equiv(ts))
    candidates = pkgs_array_new(8);
    if (i3_find_req(indent, ictx, pkg, req, &tomark, candidates)) {
        if (tomark == NULL) {
            trace(indentt, "- satisfied by being installed set");
            msgn_i(3, indent, "%s: satisfied by already installed set", strreq);
            goto l_end;
        }
    }

    /* satisfied by db? */
    if (i3_pkgdb_match_req(ictx, req)) {
        trace(indentt, "- satisfied by db");
        msgn_i(3, indent, "%s: satisfied by db", strreq);
        goto l_end;
    }

    /* try upgrade orphan */
    if (ts->getop(ts, POLDEK_OP_GREEDY)) {
        if (try_to_upgrade_orphan(indentt, ictx, pkg, req))
            goto l_end;
    }

    if (n_array_size(candidates) == 0)
        n_array_cfree(&candidates);
    else /* if they exists, must be more than one */
        n_assert(n_array_size(candidates) > 1);
    
    trace(indentt, "- %s: %s candidate is %s", pkg_id(pkg), strreq,
          tomark ? pkg_id(tomark) : "(null)");
    
    /* to-mark candidates */
    if (tomark && ts->getop(ts, POLDEK_OP_FOLLOW)) {
        struct pkg *real_tomark = tomark;
        struct i3pkg *i3tomark;
        
        if (i3_is_user_choosable_equiv(ts) && candidates) {
            real_tomark = i3_choose_equiv(ts, req, candidates, tomark);
            
            if (real_tomark == NULL) { /* user abort */
                ictx->abort = 1;
                rc = 0;
                goto l_end;
            }
        }

        i3tomark = i3pkg_new(real_tomark, 0, pkg, req, I3PKGBY_ORPHAN);
        i3_process_package(indent, ictx, i3tomark);
        goto l_end;
    }
            
    /* unresolved req */
    i3_error(ictx, pkg, I3ERR_REQUIREDBY,
             _("%s is required by %s"), strreq, pkg_id(pkg));
    
 l_end:
    n_array_cfree(&candidates);

    return 1;
}


int i3_process_orphan_requirements(int indent, struct i3ctx *ictx,
                                   struct pkg *pkg, tn_array *reqs)
{
    int i;

    if (sigint_reached() || ictx->abort)
        return 0;
    
    n_assert(pkg);
    n_assert(reqs);
    n_assert(pkg->reqs);
    
    tracef(indent, "%s as ORPHAN", pkg_id(pkg));

    for (i=0; i < n_array_size(reqs); i++) {
        struct capreq *req = n_array_nth(reqs, i);

        if (capreq_is_prereq_un(req)) /* Requires(un), TODO: req_iter */
            continue;

        if (capreq_is_rpmlib(req))
            continue;
        
        /* obsoleted by greedy mark */
        if (i3_is_marked_for_removal(ictx, pkg)) {
            trace(indent, "%s: obsoleted, return", pkg_id(pkg));
            return 1;  /* no n_array_free(reqs) needed -> ORPHAN */
        }
        
        process_orphan_req(indent, ictx, pkg, req);
    }

    return 1;
}

static int number_of_non_blacks(struct i3ctx *ictx, tn_array *pkgs) 
{
    int i, n = 0;
    
    for (i=0; i < n_array_size(pkgs); i++) {
        if (!pkg_isset_mf(ictx->processed, n_array_nth(pkgs, i),
                          PKGMARK_BLACK))
            n++;
    }
    
    return n;
    
}

static int process_req(int indent, struct i3ctx *ictx,
                       struct i3pkg *i3pkg, const struct capreq *req)
{
    struct poldek_ts *ts = ictx->ts; /* just for short */
    struct pkg       *pkg, *tomark = NULL;
    tn_array         *candidates = NULL;
    char             *strreq, *errfmt;
    int              rc = 1, indentt = indent + 1;
    
    pkg = i3pkg->pkg;
    capreq_stra(req, &strreq);
    
    tracef(indent, "%s, req: %s", pkg_id(pkg), strreq);
    
    if (i3_pkgdb_match_req(ictx, req)) {
        trace(indentt, "- satisfied by db");
        msgn_i(3, indent, "%s: satisfied by db", strreq);
        goto l_end;
    }
    
    //if (i3_is_user_choosable_equiv(ts))
    candidates = pkgs_array_new(8);
    if (i3_find_req(indent, ictx, pkg, req, &tomark, candidates)) {
        if (tomark == NULL) {
            trace(indentt, "- satisfied by being installed set");
            msgn_i(3, indent, "%s: satisfied by already installed set", strreq);
            goto l_end;
        }
    }

    if (n_array_size(candidates) == 0)
        n_array_cfree(&candidates);
    else /* if they exists, must be more than one */
        n_assert(n_array_size(candidates) > 1);
    
    trace(indentt, "- %s: %s candidate is %s", pkg_id(pkg), strreq,
          tomark ? pkg_id(tomark) : "(null)");
    
    /* to-mark candidates */
    if (tomark && ts->getop(ts, POLDEK_OP_FOLLOW)) {
        struct pkg      *real_tomark = tomark;
        struct i3pkg    *i3tomark = NULL;
        enum i3_byflag  byflag = I3PKGBY_REQ;
        int             i3pkg_flag = 0;
        
        if (i3_is_user_choosable_equiv(ts) && candidates) {
            real_tomark = i3_choose_equiv(ts, req, candidates, tomark);
            
            if (real_tomark == NULL) { /* user abort */
                ictx->abort = 1;
                rc = 0;
                goto l_end;
            }
        }
        
        i3pkg->flags &= ~I3PKG_CROSSROAD;
        if (candidates) {
            n_assert(n_array_size(candidates) > 1);
            if (number_of_non_blacks(ictx, candidates) > 1) {
                /* mark current package as crossroad and propagate mark down */
                i3pkg->flags |= I3PKG_CROSSROAD; 
                //i3pkg_flag |= I3PKG_CROSSROAD_INDIR;
                trace(indentt, "%s is a crossroad", pkg_id(pkg));
            }
#if ENABLE_TRACE            
            DBGF_F("number_of_non_blacks %d\n", number_of_non_blacks(ictx, candidates));
            pkgs_array_dump(candidates, "candidates");
#endif            
        }
        
        if (i3pkg->flags & I3PKG_BACKTRACKABLE) {
            DBGF("%s INDIRECT\n", pkg_id(pkg));
            i3pkg_flag |= I3PKG_CROSSROAD_INDIR;
        }

        
        i3tomark = i3pkg_new(real_tomark, i3pkg_flag, pkg, req, byflag);
        rc = i3_process_package(indent, ictx, i3tomark);
        goto l_end;
    }
    
    /* unresolved req */
    if (capreq_is_rpmlib(req))
        errfmt = _("%s: req %s not found, upgrade rpm");
    else
        errfmt = _("%s: req %s not found");
    
    i3_error(ictx, pkg, I3ERR_NOTFOUND, errfmt, pkg_id(pkg), strreq);
    rc = 0;
    
 l_end:
    n_array_cfree(&candidates);

    return rc;
}

static tn_array *with_suggests(struct i3ctx *ictx, struct pkg *pkg) 
{
    char *confirmation, message[2048];
    tn_array *suggests = capreq_arr_new(4);
    tn_buf *nbuf;
    int n, i, autochoice = 0;

    if (pkg->sugs == NULL)
        return NULL;

    /* tests automation */
    if (poldek__is_in_testing_mode()) {
        const char *choice = getenv("POLDEK_TESTING_WITH_SUGGESTS");
        if (choice) {
            if (n_str_eq(choice, "all"))
                autochoice = -1;
            
            else if (sscanf(choice, "%d", &autochoice) != 1)
                autochoice = 0;
            DBGF("autochoice = %d\n", autochoice);
        }
    }
    
    if (!autochoice && !i3_is_user_choosable_equiv(ictx->ts))
        return NULL;

    n_array_ctl_set_freefn(suggests, NULL); /* 'weak' ref */
    for (i=0; i < n_array_size(pkg->sugs); i++) {
        struct capreq *req = n_array_nth(pkg->sugs, i);
        
        if (i3_pkgdb_match_req(ictx, req) || iset_provides(ictx->inset, req))
            continue;

        if (autochoice > 0 && i != autochoice - 1)
            continue;
        n_array_push(suggests, req);
    }
    
    if (n_array_size(suggests) == 0) {
        n_array_free(suggests);
        return NULL;
    }

    if (autochoice)
        return suggests;
    

    nbuf = capreq_arr_join(suggests, NULL, NULL);
    n = n_snprintf(message, sizeof(message), _("%s suggests installation of: %s"),
                   pkg_id(pkg), n_buf_ptr(nbuf));
    n_buf_free(nbuf);
    
    confirmation = ngettext("Try to install it?", "Try to install them?",
                            n_array_size(suggests));    
    n_snprintf(&message[n], sizeof(message) - n, "\n%s", confirmation);
    
    if (poldek__confirm(ictx->ts, 0, message)) 
        return suggests;
    
    n_array_free(suggests);
    return NULL;
}

static int suggests_contains(tn_array *suggests, const struct capreq *req) 
{
    int i;
    
    i = capreq_arr_find(suggests, capreq_name(req));
    if (i >= 0 && capreq_cmp_name_evr(n_array_nth(suggests, i), req) == 0)
        return 1;
    return 0;
}

int i3_process_pkg_requirements(int indent, struct i3ctx *ictx,
                                struct i3pkg *i3pkg)
{
    struct poldek_ts    *ts = ictx->ts;
    struct pkg          *pkg = i3pkg->pkg;
    struct pkg_req_iter *it = NULL;
    const struct capreq *req = NULL;
    tn_array            *suggests = NULL;
    unsigned            itflags = PKG_ITER_REQIN;
    int                 nerrors = 0, backtrack = 0;
    
    
    pkg = i3pkg->pkg;
    n_assert(pkg);
    n_assert(pkg->reqs);
    //n_assert(i3pkg->bypkg);

    if (sigint_reached() || ictx->abort)
        return 0;
    
    tracef(indent, "%s as NEW", pkg_id(pkg));

    if (ts->getop(ts, POLDEK_OP_SUGGESTS)) {
        suggests = with_suggests(ictx, pkg);
        if (suggests)
            itflags |= PKG_ITER_REQSUG;
    }
    
    it = pkg_req_iter_new(pkg, itflags);
    while ((req = pkg_req_iter_get(it))) {
        unsigned t;
        int rc;

        /* install only reasonable/choosen suggests */
        t = pkg_req_iter_current_req_type(it);
        if (t == PKG_ITER_REQSUG && !suggests_contains(suggests, req))
            continue;
        
        if ((rc = process_req(indent, ictx, i3pkg, req)) <= 0) {
            nerrors++;
            if (rc < 0)
                backtrack = 1;
        }
    }

    pkg_req_iter_free(it);
    n_array_cfree(&suggests);
    
    if (backtrack && (i3pkg->flags & I3PKG_CROSSROAD)) {
        logn(LOGNOTICE, "Retrying to process %s", pkg_id(i3pkg->pkg));
        
        return i3_process_pkg_requirements(indent, ictx, i3pkg);
    }
    
    return nerrors == 0;
}
