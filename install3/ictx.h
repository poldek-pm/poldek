/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_INSTALL3_ICTX_H
#define POLDEK_INSTALL3_ICTX_H

#include <sys/param.h>          /* for PATH_MAX */
#include <trurl/trurl.h>
#include <sigint/sigint.h>

#include "i18n.h"
#include "log.h"
#include "misc.h"

#include "capreq.h"
#include "poldek.h"
#include "poldek_intern.h"
#include "pkg.h"
#include "pkgmisc.h"
#include "pkgset.h"
#include "arg_packages.h"
#include "pm/pm.h"
#include "poldek_term.h"
#include "pkgfl.h"
#include "iset.h"

struct poldek_ts;
struct pkgmark_set;
struct poldek_iinf;

#define I3ERR_CLASS_DEP      (1 << 0)
#define I3ERR_CLASS_CNFL     (1 << 1)
#define I3ERR_NOTFOUND       (1 << 5) | I3ERR_CLASS_DEP
#define I3ERR_REQUIREDBY     (1 << 6) | I3ERR_CLASS_DEP
#define I3ERR_CONFLICT       (1 << 7) | I3ERR_CLASS_CNFL
#define I3ERR_DBCONFLICT     (1 << 8) | I3ERR_CLASS_CNFL
#define I3ERR_FATAL          (1 << 10)
struct i3ctx;

void i3_error(struct i3ctx *ictx, struct pkg *pkg,
              unsigned errcode, const char *fmt, ...);
void i3_forget_error(struct i3ctx *ictx, const struct pkg *pkg);
int i3_get_nerrors(struct i3ctx *ictx, unsigned errcodeclass);
int i3_log_errors(struct i3ctx *ictx);


/* i3pkg wraps package itself and dependency resolving related data */
 /* markedby flag  */
enum i3_byflag {
    I3PKGBY_HAND    = 1,  /* directly marked by user */
    I3PKGBY_REQ     = 2,  /* foo requires bar */
    I3PKGBY_ORPHAN  = 3,  /* orphaned foo marks bar */
    I3PKGBY_GREEDY  = 4   /* greedy upgrade foo-X to foo-Y */
};

#define I3PKG_CROSSROAD       (1 << 0) /* "choose" point */
#define I3PKG_CROSSROAD_INDIR (1 << 1)
#define I3PKG_BACKTRACKABLE   (I3PKG_CROSSROAD | I3PKG_CROSSROAD_INDIR)

struct i3pkg {
    uint32_t       flags;
    struct pkg     *pkg;

    struct pkg           *bypkg; /* marked by */
    const struct capreq  *byreq; /*  and as req provider */
    enum i3_byflag       byflag; /*  reason details  */

    tn_array       *obsoletedby;  /* packages obsoleted by */
    tn_array       *markedby;     /* packages marked by */

    /* unused */
    //tn_hash        *candidates; /* str(req) => pkg[] pairs */

    uint32_t       _refcnt;
};

struct i3pkg *i3pkg_new(struct pkg *pkg, unsigned flags,
                        struct pkg *bypkg, const struct capreq *byreq,
                        enum i3_byflag byflag);
struct i3pkg *i3pkg_link(struct i3pkg *i3pkg);
void i3pkg_free(struct i3pkg *i3pkg);

struct i3ctx {
    tn_hash           *errors;       /* pkg_id => i3_error[] pairs */
    tn_array          *i3pkg_stack;  /* i3pkg stack */

    struct poldek_ts  *ts;
    struct pkgset     *ps;          /* available packages, ts->ps alias, for short */

    struct iset       *inset;       /* packages to install */
    struct iset       *unset;       /* packages to remove */

    struct pkgmark_set *processed;  /* to mark pkg processed path */

    tn_hash           *multi_obsoleted; /* pkg_id => real obsoleted packages (muli-instances upgrade) */

    unsigned           ma_flags;    /* match flags (POLDEK_MA_*) */
    int                abort;       /* abort processing? */
};


void i3ctx_init(struct i3ctx *ictx, struct poldek_ts *ts);
void i3ctx_reset(struct i3ctx *ictx);
void i3ctx_destroy(struct i3ctx *ictx);

extern int poldek_conf_MULTILIB;

/* set stop flag */
int i3_stop_processing(struct i3ctx *ictx, int stop);

#define i3_return_zero_if_stoppped(i3ctx)        \
    do { if (sigint_reached() || i3ctx->abort)   \
            return 0;                            \
    } while(0)

/* mark.c */
int i3_mark_package(struct i3ctx *ictx, struct pkg *pkg, uint32_t mark);
int i3_unmark_package(struct i3ctx *ictx, struct pkg *pkg);

#define i3_is_marked(iictx, p) iset_has_pkg(iictx->inset, p)
#define i3_is_hand_marked(iictx, p) iset_ismarkedf(iictx->inset, p, PKGMARK_MARK)
#define i3_is_dep_marked(iictx, p) iset_ismarkedf(iictx->inset, p, PKGMARK_DEP)

#define i3_is_marked_for_removal(iictx, p) iset_has_pkg(iictx->unset, p)

int i3_is_other_version_marked(struct i3ctx *ictx, const struct pkg *pkg,
                               struct capreq *req);

/* marks with PKGMARK_MARK all pkgs with same name prefix */
int i3_mark_namegroup(struct i3ctx *ictx,
                      struct pkg *pkg, tn_array *pkgs);

/* misc.c */
int i3_pkgdb_match_req(struct i3ctx *ictx, const struct capreq *req);

int i3_is_pkg_installed(struct poldek_ts *ts, const struct pkg *pkg, int *cmprc);
int i3_is_pkg_installable(struct poldek_ts *ts, const struct pkg *pkg,
                          int is_hand_marked);

int i3_select_best_pkg(int indent, struct i3ctx *ictx,
                       const struct pkg *marker, tn_array *candidates);

int i3_find_req(int indent, struct i3ctx *ictx,
                 const struct pkg *pkg, const struct capreq *req,
                 struct pkg **best_pkg, tn_array *candidates);

/* pkg_req_iter wrapper */
#include "booldep.h"
struct i3req {
    const struct capreq *origin;
    const struct capreq *req;
};

struct i3_req_iter {
    struct pkg_req_iter *it;
    struct i3req        i3req;

    /* booldep */
    const struct capreq     *borigin;
    tn_array                *breqs;
    struct booldep_eval_ctx bctx;
};

void i3_req_iter_init(struct i3_req_iter *it, int indent, struct i3ctx *ictx,
                      struct pkg *pkg, unsigned itflags);

void i3_req_iter_destroy(struct i3_req_iter *it);


const struct i3req *i3_req_iter_get(struct i3_req_iter *it);

/* conflicts.c */
int i3_resolve_conflict(int indent, struct i3ctx *ictx,
                        struct pkg *pkg, const struct capreq *cnfl,
                        struct pkg *dbpkg);


int i3_process_pkg_conflicts(int indent, struct i3ctx *ictx,
                             struct i3pkg *i3pkg);

tn_array *i3_get_package_conflicted_pkgs(int indent, struct i3ctx *ictx,
                                         const struct pkg *pkg);

/* obsoletes.c */
struct orphan {
    struct pkg *pkg;
    tn_array   *reqs;
};

int i3_process_pkg_obsoletes(int indent, struct i3ctx *ictx,
                             struct i3pkg *i3pkg);

int i3_process_pkg_requirements(int indent, struct i3ctx *ictx,
                                struct i3pkg *i3pkg);

int i3_process_package(int indent, struct i3ctx *ictx, struct i3pkg *i3pkg);
int i3_install_package(struct i3ctx *ictx, struct pkg *pkg);


/* orphans */
int i3_process_orphan_requirements(int indent, struct i3ctx *ictx,
                                   struct pkg *pkg, tn_array *reqs);

int i3_process_orphan(int indent, struct i3ctx *ictx, struct orphan *o);


int i3_is_user_choosable_equiv(struct poldek_ts *ts);

struct pkg *i3_choose_equiv(struct poldek_ts *ts,
                            const struct pkg *pkg, const struct capreq *cap,
                            tn_array *pkgs, struct pkg *hint);

#endif
