/* 
  Copyright (C) 2001 Pawel A. Gajda (mis@k2.net.pl)
 
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

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"
#include "shell.h"


static error_t parse_opt(int key, char *arg, struct argp_state *state);
static int uninstall(struct cmdarg *cmdarg);


#define OPT_UNINST_NODEPS  2
#define OPT_UNINST_FORCE   3

static struct argp_option options[] = {
{"force", OPT_UNINST_FORCE, 0, 0, "Be unconcerned", 1 },
{"test", 't', 0, 0, "Don't uninstall, but tell if it would work or not", 1 },
{"nodeps", OPT_UNINST_NODEPS, 0, 0,
 "Ignore broken dependencies", 1 },
{0,  'v', 0, 0, "Be more (and more) verbose.", 1 },
{NULL, 'h', 0, OPTION_HIDDEN, "", 1 },
{ 0, 0, 0, 0, 0, 0 },
};


struct command command_uninstall = {
    COMMAND_HASVERBOSE | COMMAND_MODIFIESDB, 

    "uninstall", "PACKAGE...", "Uninstall packages", 
    
    options, parse_opt,
    
    NULL, uninstall, NULL, NULL, NULL, NULL
};



static
int uninstall_pkgs(tn_array *pkgnevrs, struct inst_s *inst) 
{
    char **argv;
    char *cmd;
    int i, n, nopts = 0;

    for (i=0; i<n_array_size(pkgnevrs); i++) 
        msg(1, "R %s\n", (char*)n_array_nth(pkgnevrs, i));
    
    msgn(1, _("Uninstalling %d package%s"), n_array_size(pkgnevrs),
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

    for (i=1; i<verbose; i++)
        argv[n++] = "-v";

    if (inst->instflags & PKGINST_TEST)
        argv[n++] = "--test";
    
    if (inst->instflags & PKGINST_FORCE)
        argv[n++] = "--force";
    
    if (inst->instflags & PKGINST_NODEPS)
        argv[n++] = "--nodeps";

    if (inst->rootdir) {
    	argv[n++] = "--root";
	argv[n++] = (char*)inst->rootdir;
    }

#if 0    
    if (inst->rpmacros) 
        for (i=0; i<n_array_size(inst->rpmacros); i++) {
            argv[n++] = "--define";
            argv[n++] = n_array_nth(inst->rpmacros, i);
        }
#endif
    
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
        //logv(1, LOGTTY|LOG, "Running%s...\n", buf);

        for (i=0; i<nopts; i++) 
            p += snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", argv[i]);
        *p = '\0';
        msgn(1, _("Running%s..."), buf);
        
    }

    return rpmr_exec(cmd, argv, 1, 0) == 0;
}


static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdarg *cmdarg = state->input;

    arg = arg;
    
    switch (key) {
        case OPT_UNINST_NODEPS:
            cmdarg->sh_s->inst->instflags  |= PKGINST_NODEPS;
            break;
            
        case OPT_UNINST_FORCE:
            cmdarg->sh_s->inst->instflags |= PKGINST_FORCE;
            break;
            
        case 't':
            cmdarg->sh_s->inst->instflags |= PKGINST_TEST;
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static
int shpkg_cmp_rm_uninstalled(struct shpkg *p1, struct shpkg *p2) 
{
    p2 = p2;
    
    if (p1->flags & SHPKG_UNINSTALL) 
        return 0;
    
    return -1;
}

static
void shpkg_clean_uninstall_flag(struct shpkg *shpkg)
{
    shpkg->flags &= ~SHPKG_UNINSTALL;
}


static int uninstall(struct cmdarg *cmdarg) 
{
    tn_array *shpkgs = NULL, *pkgnevrs = NULL;
    int i, err = 0;

    if (cmdarg->sh_s->instpkgs == NULL) {
        log(LOGERR, "uninstall: installed packages not loaded, "
            "type \"reload\" to load them\n");
        return 0;
    }
    
    sh_resolve_packages(cmdarg->pkgnames, cmdarg->sh_s->instpkgs, &shpkgs, 1);
    if (shpkgs == NULL || n_array_size(shpkgs) == 0) {
        err++;
        goto l_end;
    }
    
    
    if (shpkgs == cmdarg->sh_s->instpkgs) {
        logn(LOGERR, _("uninstall: better do \"rm -rf /\""));
        return 0;
    }
    
    if (err) 
        goto l_end;

    
    pkgnevrs = n_array_new(n_array_size(shpkgs), NULL, (tn_fn_cmp)strcmp);

    for (i=0; i<n_array_size(shpkgs); i++) {
        struct shpkg *shpkg = n_array_nth(shpkgs, i);
        
        shpkg->flags |= SHPKG_UNINSTALL;
        n_array_push(pkgnevrs, shpkg->nevr);
    }
    
    if (!uninstall_pkgs(pkgnevrs, cmdarg->sh_s->inst))
        err++;
    
    if (err || cmdarg->sh_s->inst->instflags & PKGINST_TEST) {
        n_array_map(shpkgs, (tn_fn_map1)shpkg_clean_uninstall_flag);
        
    } else {
        int n = n_array_size(cmdarg->sh_s->instpkgs);
        n_array_remove_ex(cmdarg->sh_s->instpkgs, NULL,
                          (tn_fn_cmp)shpkg_cmp_rm_uninstalled);
        if (n != n_array_size(cmdarg->sh_s->instpkgs))
            cmdarg->sh_s->ts_instpkgs = time(0);
    }
    
    
 l_end:
    if (pkgnevrs != NULL)
        n_array_free(pkgnevrs);

    if (shpkgs && shpkgs != cmdarg->sh_s->instpkgs) 
        n_array_free(shpkgs);
    
    return err == 0;
}
