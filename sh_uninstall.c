/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>

#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"

int shell_uninstall_pkgs(tn_array *pkgnevrs, struct inst_s *inst) 
{
    char **argv;
    char *cmd;
    int i, n, nopts = 0, ec;
    int nv = verbose;

    for (i=0; i<n_array_size(pkgnevrs); i++) 
        msg(1, "U %s\n", n_array_nth(pkgnevrs, i));
    
    msg(1, "Uninstalling %d package%s\n", n_array_size(pkgnevrs),
        n_array_size(pkgnevrs) > 1 ? "s" : "");
    
    n = 128 + n_array_size(pkgnevrs);
    
    argv = alloca((n + 1) * sizeof(*argv));
    argv[n] = NULL;
    
    n = 0;
    
    if (inst->instflags & PKGINST_TEST) {
        cmd = "/bin/rpm";
        argv[n++] = "rpm";
        
    } else if (inst->flags & INSTS_USESUDO) {
        cmd = "/usr/bin/sudo";
        argv[n++] = "sudo";
        argv[n++] = "/bin/rpm";
        
    } else {
        cmd = "/bin/rpm";
        argv[n++] = "rpm";
    }
    
    argv[n++] = "-e";

    if (nv > 0) {
        argv[n++] = "-v";
        nv--;
    }
    
    if (nv > 0)
        nv--;

    while (nv-- > 0) 
        argv[n++] = "-v";
    
    if (inst->instflags & PKGINST_TEST)
        argv[n++] = "--test";
    
    if (inst->instflags & PKGINST_FORCE)
        argv[n++] = "--force";
    
    if (inst->instflags & PKGINST_NODEPS)
        argv[n++] = "--nodeps";

    if (inst->rpmacros) 
        for (i=0; i<n_array_size(inst->rpmacros); i++) {
            argv[n++] = "--define";
            argv[n++] = n_array_nth(inst->rpmacros, i);
        }
    
    if (inst->rpmopts) 
        for (i=0; i<n_array_size(inst->rpmopts); i++)
            argv[n++] = n_array_nth(inst->rpmopts, i);
    
    nopts = n;
    for (i=0; i<n_array_size(pkgnevrs); i++) 
        argv[n++] = n_array_nth(pkgnevrs, i);
        
    n_assert(n > nopts); 
    argv[n++] = NULL;

    if (verbose > 0) {
        char buf[1024], *p;
        p = buf;
        
        for (i=0; i<nopts; i++) 
            p += snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", argv[i]);
        *p = '\0';
        msg(1, "Running%s...\n", buf);
    }
    

#if ! USE_P_OPEN    
    ec = exec_rpm(cmd, argv);
    
#else 
    p_st_init(&pst);
    if (p_open(&pst, cmd, argv) == NULL) 
        return 0;
    
    n = 0;
    if (verbose == 0) {
        verbose = 1;
        n = 1;
    }

    process_rpm_output(&pst);
    if ((ec = p_close(&pst) != 0))
        log(LOGERR, "%s", pst.errmsg);

    p_st_destroy(&pst);
#endif
    if (n)
        verbose--;

    return ec == 0;
}
