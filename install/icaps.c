/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

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

static
int prepare_icap(struct poldek_ts *ts, const char *capname, tn_array *pkgs)
{
    int i, found = 0;
    tn_array *dbpkgs;
    struct capreq *cap;

    capreq_new_name_a(capname, cap);
    dbpkgs = pkgdb_get_provides_dbpkgs(ts->db, cap, NULL, 0);

    if (dbpkgs == NULL) {
        struct pkg *pkg = NULL;
        
        if (ts->getop(ts, POLDEK_OP_FRESHEN))
            return 0;

        if (in_is_user_choosable_equiv(ts) && n_array_size(pkgs) > 1) {
            pkg = in_choose_equiv(ts, cap, pkgs, NULL);
            if (pkg == NULL) { /* user aborts */
                found = -1;
                goto l_end;
            }
        }
        
        if (pkg == NULL)
            pkg = n_array_nth(pkgs, 0);
        
        pkg_hand_mark(ts->pms, pkg);
        return 1;
    }
    
    n_array_sort_ex(pkgs, (tn_fn_cmp)pkg_cmp_name_evr_rev);
    for (i=0; i < n_array_size(dbpkgs); i++) {
        struct pkg *dbpkg = n_array_nth(dbpkgs, i);
        int n = n_array_bsearch_idx_ex(pkgs, dbpkg,
                                       (tn_fn_cmp)pkg_cmp_name);

        DBGF("%s: %s\n", capname, pkg_id(dbpkg));
        
        if (n < 0)
            continue;
    
        for (; n < n_array_size(pkgs); n++) {
            struct pkg *pkg = n_array_nth(pkgs, n);
            int cmprc, mark = 0;

            DBGF("%s: %s cmp %s\n", capname, pkg_id(pkg),
                 pkg_id(dbpkg));
            if (pkg_cmp_name(pkg, dbpkg) != 0)
                break;
            
            cmprc = pkg_cmp_name_evr(pkg, dbpkg);
            if (cmprc > 0)
                mark = 1;
                
            else if (cmprc == 0 && poldek_ts_issetf(ts, POLDEK_TS_REINSTALL))
                mark = 1;
                
            else if (cmprc < 0 && poldek_ts_issetf(ts, POLDEK_TS_DOWNGRADE))
                mark = 1;
            
            if (mark) {
                found = 1;
                msgn(1, _("%s: marked as %s's provider"), pkg_id(pkg),
                     capname);
                
                pkg_hand_mark(ts->pms, pkg);
                goto l_end;
                
            } else if (cmprc <= 0) {
                char *eqs = cmprc == 0 ? "equal" : "newer";
                msgn(1, _("%s: %s version of %s is installed (%s), skipped"),
                     capname, eqs, pkg_id(dbpkg),
                     pkg_id(pkg));
                
            } else {
                n_assert(0);
            }
        }
    }
    
l_end:
    if (dbpkgs)
        n_array_free(dbpkgs);
    
    return found;
}

/* handles  --caplookup */
int in_prepare_icaps(struct poldek_ts *ts) 
{
    tn_array *keys;
    tn_hash *icaps;
    int i, rc = 1;
    
    icaps = arg_packages_get_resolved_caps(ts->aps);
    keys = n_hash_keys_cp(icaps);
    for (i=0; i < n_array_size(keys); i++) {
        const char *cap = n_array_nth(keys, i);
        tn_array *pkgs = n_hash_get(icaps, cap);
        
        if (prepare_icap(ts, cap, pkgs) == -1) {
            rc = -1;
            break;
        }
    }
    
    n_array_free(keys);
    n_hash_free(icaps);
    return rc;
}
