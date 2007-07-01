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

int in_process_package(int indent, struct install_ctx *ictx,
                       struct pkg *pkg, int process_as) 
{
    
    n_assert(process_as == PROCESS_AS_NEW || process_as == PROCESS_AS_ORPHAN);
    
    if (ictx->nerr_fatal || sigint_reached())
        return 0;

    indent += 2;
    if (pkg_isset_mf(ictx->deppms, pkg, PKGMARK_GRAY)) { /* processed */
        //msg_i(1, indent, "CHECKED%s%s dependencies...\n",
        //      process_as == PROCESS_AS_ORPHAN ? " orphaned ":" ",
        //      pkg_id(pkg));
        return 0;
    }

#if 0
    {
        
        static struct pkg *rpm = NULL;
        
        if (strcmp(pkg->name, "rpm") == 0 && strcmp(pkg->ver, "4.1") == 0) {
            rpm = pkg;
        }
        if (rpm)
            log(LOGNOTICE, "DD %s(%p): %d\n", pkg_id(rpm), rpm, pkg_is_marked(rpm));
    }
    
#endif    

    if (process_as == PROCESS_AS_NEW)
        n_array_push(ictx->pkg_stack, pkg);

    DBGF("PROCESSING [%d] %s as %s\n", indent, pkg_id(pkg),
         process_as == PROCESS_AS_NEW ? "NEW" : "ORPHAN");
    
    msg_i(3, indent, "Checking%s%s dependencies...\n",
          process_as == PROCESS_AS_ORPHAN ? " orphaned ":" ",
          pkg_id(pkg));

    pkg_set_mf(ictx->deppms, pkg, PKGMARK_GRAY); /* dep processed */

    if (process_as == PROCESS_AS_NEW) 
        in_process_pkg_obsoletes(indent, ictx, pkg);

    if (pkg->reqs)
        in_process_pkg_requirements(indent, ictx, pkg, process_as);
    
    if (process_as == PROCESS_AS_NEW)
        in_process_pkg_conflicts(indent, ictx, pkg);
            
    DBGF("END PROCESSING [%d] %s as %s\n", indent, pkg_id(pkg),
         process_as == PROCESS_AS_NEW ? "NEW" : "ORPHAN");
    
    if (process_as == PROCESS_AS_NEW)
        n_array_pop(ictx->pkg_stack);
    
    return 1;
}
