/* $Id */
#ifndef POLDEK_INSTALL_ICTX_H
#define POLDEK_INSTALL_ICTX_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/param.h>          /* for PATH_MAX */
#include <trurl/trurl.h>
#include <sigint/sigint.h>

#define ENABLE_TRACE 0
#include "i18n.h"
#include "log.h"
#include "misc.h"

#include "poldek.h"
#include "poldek_intern.h"
#include "pkg.h"
#include "pkgmisc.h"
#include "pkgset.h"
#include "pkgset-req.h"
#include "arg_packages.h"
#include "dbpkgset.h"
#include "dbdep.h"
#include "pm/pm.h"
#include "poldek_term.h"
#include "pkgfl.h"

struct poldek_ts;
struct pkgmark_set;
struct pkgdb_set;
struct poldek_iinf;

struct install_ctx {
    tn_array       *avpkgs;
    tn_array       *install_pkgs;     /* pkgs to install */
    
    tn_hash        *db_deps;          /* cache of resolved db dependencies */

    struct dbpkg_set   *uninst_set;
    
    struct pkgmark_set *dbpms;
    struct pkgmark_set *unmetpms;   /* to mark pkgs with unmet dependencies */
    struct pkgmark_set *deppms;     /* to mark pkg processing path */
    
    tn_array  *orphan_dbpkgs;    /* array of orphaned dbpkg*s */
    
    int            strict;
    int            ndberrs;
    int            ndep;
    int            ninstall;

    int            nerr_dep;
    int            nerr_cnfl;
    int            nerr_dbcnfl;
    int            nerr_fatal;
    
    struct poldek_ts  *ts;
    struct pkgset     *ps;      /* ts->ps alias, for short */

//    tn_hash        *db_pkgs;    /* used by mapfn_mark_newer_pkg() */
//    int             nmarked;

    tn_array       *pkg_stack;  /* stack for current processed packages  */
};

void install_ctx_init(struct install_ctx *ictx, struct poldek_ts *ts);
void install_ctx_reset(struct install_ctx *ictx);
void install_ctx_destroy(struct install_ctx *ictx);

extern int poldek_conf_MULTILIB;

/* icaps.c */
int in_prepare_icaps(struct poldek_ts *ts);

/* mark.c */
int in_mark_package(struct install_ctx *ictx, struct pkg *pkg);

int in_dep_mark_package(int indent, struct install_ctx *ictx, struct pkg *pkg,
                        struct pkg *bypkg, struct capreq *byreq,
                        int process_as);

int in_greedy_mark_package(int indent, struct install_ctx *ictx,
                           struct pkg *pkg, struct pkg *oldpkg,
                           struct capreq *unresolved_req);
int in_mark_namegroup(struct install_ctx *ictx,
                      struct pkg *pkg, tn_array *pkgs);

int in_is_marked_for_removal(struct install_ctx *ictx, struct pkg *pkg);
int in_is_marked_for_removal_by_req(struct install_ctx *ictx,
                                    struct pkg *pkg, const struct capreq *req);

int in_is_other_version_marked(struct install_ctx *ictx, struct pkg *pkg,
                               struct capreq *req);

/* misc.c */

int in_is_pkg_installed(struct install_ctx *ictx, struct pkg *pkg, int *cmprc);
int in_is_pkg_installable(struct install_ctx *ictx, struct pkg *pkg,
                          int is_hand_marked);

struct pkg *in_select_pkg(struct install_ctx *ictx, const struct pkg *apkg,
                          tn_array *pkgs);

int in_select_best_pkg(struct install_ctx *ictx, const struct pkg *marker,
                       tn_array *candidates);

int in_pkg_drags(struct install_ctx *ictx, struct pkg *pkg);


#define IN_FIND_REQ_NIL     0
#define IN_FIND_REQ_BEST    1
int in_find_req(struct install_ctx *ictx,
                const struct pkg *pkg, struct capreq *req,
                struct pkg **best_pkg, struct pkg ***candidates, int flag);

/* conflicts.c */
int in_resolve_conflict(int indent, struct install_ctx *ictx,
                        struct pkg *pkg, const struct capreq *cnfl,
                        struct pkg *dbpkg);


int in_process_pkg_conflicts(int indent, struct install_ctx *ictx,
                             struct pkg *pkg);

/* obsoletes.c */
int in_process_pkg_obsoletes(int indent, struct install_ctx *ictx,
                             struct pkg *pkg);

int in_process_pkg_requirements(int indent, struct install_ctx *ictx,
                                struct pkg *pkg, int process_as);

#define PROCESS_AS_NEW        (1 << 0)
#define PROCESS_AS_ORPHAN     (1 << 1)

int in_process_package(int indent, struct install_ctx *ictx,
                       struct pkg *pkg, int process_as);


struct pkg *in_choose_equiv(struct poldek_ts *ts, struct capreq *cap,
                            struct pkg **candidates, struct pkg *defaultpkg);
#endif
