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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h> 
#include <unistd.h>

#include <trurl/narray.h>
#include <trurl/nassert.h>

#include "i18n.h"
#include "log.h"
#include "pkgset.h"
#include "usrset.h"
#include "misc.h"
#include "poldek.h"

static
int mkdbdir(const char *rootdir) 
{
    char path[PATH_MAX];

    snprintf(path, sizeof(path), "%s%s", rootdir, "/var");
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        logn(LOGERR, "mkdir %s: %m", path);
        return 0;
    }
    
    snprintf(path, sizeof(path), "%s%s", rootdir, "/var/lib");
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        logn(LOGERR, "mkdir %s: %m", path);
        return 0;
    }

    snprintf(path, sizeof(path), "%s%s", rootdir, "/var/lib/rpm");
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        logn(LOGERR, "mkdir %s: %m", path);
        return 0;
    }

    return 1;
}

static int chk_params(struct inst_s *inst) 
{

    if (inst->rootdir == NULL)
        inst->rootdir = n_strdup("/");
    

    if (inst->flags & INSTS_RPMTEST) {
        if (verbose < 1)
            verbose += 1;
        
    } else if ((inst->flags & (INSTS_JUSTFETCH | INSTS_JUSTPRINTS))) {
        
    } else {
        if (!is_rwxdir(inst->rootdir)) {
            logn(LOGERR, "%s: %m", inst->rootdir);
            return 0;
        }

        if (inst->flags & INSTS_MKDBDIR) {
            if (!mkdbdir(inst->rootdir))
                return 0;
        }
    }

    return 1;
}

int poldek_install_dist(struct poldek_ctx *ctx) 
{
    int rc;

    n_assert(ctx->inst->flags & (INSTS_INSTALL | INSTS_UPGRADE));
    
    if (!chk_params(ctx->inst))
        return 0;
    
    if ((ctx->inst->flags & INSTS_RPMTEST)) 
        ctx->inst->db = pkgdb_open(ctx->inst->rootdir, NULL, O_RDONLY);
    else 
        ctx->inst->db = pkgdb_creat(ctx->inst->rootdir, NULL);
    
    if (ctx->inst->db == NULL) 
        return 0;
    
    rc = pkgset_install_dist(ctx->ps, ctx->inst);
    pkgdb_free(ctx->inst->db);
    ctx->inst->db = NULL;
    return rc;
}

int poldek_upgrade_dist(struct poldek_ctx *ctx) 
{
    int rc;
    
    if (!chk_params(ctx->inst))
        return 0;
    
    ctx->inst->db = pkgdb_open(ctx->inst->rootdir, NULL, O_RDONLY);
    if (ctx->inst->db == NULL) 
        return 0;
    
    rc = pkgset_upgrade_dist(ctx->ps, ctx->inst);
    pkgdb_free(ctx->inst->db);
    ctx->inst->db = NULL;
    return rc;
}


int poldek_install(struct poldek_ctx *ctx, struct install_info *iinf) 
{
    int rc;
    

    n_assert(ctx->inst->flags & (INSTS_INSTALL | INSTS_UPGRADE));
    
    if (!chk_params(ctx->inst))
        return 0;

    ctx->inst->db = pkgdb_open(ctx->inst->rootdir, NULL, O_RDONLY);
    if (ctx->inst->db == NULL) 
        return 0;
    
    rc = pkgset_install(ctx->ps, ctx->inst, iinf);
    pkgdb_free(ctx->inst->db);
    ctx->inst->db = NULL;
    return rc;
}
