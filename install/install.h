/* $Id */
#ifndef POLDEK_INSTALL_H
#define POLDEK_INSTALL_H

#include <trurl/trurl.h>

struct poldek_ts;
struct pkgmark_set;
struct pkgdb_set;
struct poldek_iinf;

//int do_poldek_ts_install_dist(struct poldek_ts *ts);
//int do_poldek_ts_upgrade_dist(struct poldek_ts *ts);
//int do_poldek_ts_install(struct poldek_ts *ts, struct poldek_iinf *iinf);
//int do_poldek_ts_uninstall(struct poldek_ts *ts, struct poldek_iinf *iinf);

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

#endif
