/* $Id$ */

#ifndef  POLDEK_INSTALL_DBDEP_H
#define  POLDEK_INSTALL_DBDEP_H

#define DBDEP_FOREIGN          (1 << 3)
#define DBDEP_DBSATISFIED      (1 << 4)

#include <trurl/narray.h>
#include <trurl/nhash.h>

#include "pkg.h"
#include "capreq.h"

struct db_dep {
    struct capreq *req;        /* requirement */
    struct pkg    *spkg;       /* package which satisfies req */
    tn_array      *pkgs;       /* packages which requires req */
    unsigned      flags;       /* DBDEP_* */
};

tn_hash *db_deps_new(void);

void db_deps_add(tn_hash *db_deph, struct capreq *req, struct pkg *pkg,
                 struct pkg *spkg, unsigned flags);

void db_deps_remove_pkg(tn_hash *db_deph, struct pkg *pkg);
void db_deps_remove_pkg_caps(tn_hash *db_deph, struct pkg *pkg, int load_full_fl);

struct db_dep *db_deps_provides(tn_hash *db_deph, struct capreq *cap,
                                unsigned flags);
struct db_dep *db_deps_contains(tn_hash *db_deph, struct capreq *cap,
                                unsigned flags);

#endif
