/* 
  Copyright (C) 2000, 2001 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_STRSIGNAL
# define __USE_GNU 1
#endif

#include <string.h>

#ifdef HAVE_STRSIGNAL
# undef __USE_GNU
#endif


#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <obstack.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fnmatch.h>

#include <rpm/rpmlib.h>
#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>

#include <vfile/vfile.h>
#include <vfile/p_open.h>

#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"

#define EXEC_RPM 1

int exec_rpm(const char *cmd, char *const argv[])
{
    int rc, st, n;
    pid_t pid;

    msg_f(1, "Executing %s ", cmd);
    n = 0;
    while (argv[n])
        msg_f(1, "_%s ", argv[n++]);
    msg_f(1, "\n");
    if (access(cmd, X_OK) != 0) {
        log(LOGERR, "%s: no such file", cmd);
        return -1;
    }
    
    if ((pid = fork()) == 0) {
        execv(cmd, argv);
	exit(EXIT_FAILURE);
        
    } else if (pid < 0) {
        log(LOGERR, "%s: no such file", cmd);
        return -1;
    }

    rc = 0;
    while (wait(&st) > 0) {
        if (WIFEXITED(st)) {
            rc = WEXITSTATUS(st);
            if (rc != 0)
                log(LOGERR, "%s exited with %d\n", cmd, WEXITSTATUS(st));
            else 
                msg_f(1, "%s exited with %d\n", cmd, rc);
            
        } else if (WIFSIGNALED(st)) {
#ifdef HAVE_STRSIGNAL
            log(LOGERR, "%s terminated by signal %s\n", cmd,
                strsignal(WTERMSIG(st)));
#else
            log(LOGERR, "%s terminated by signal %d\n", cmd,
                WTERMSIG(st));
#endif        
            rc = -1;
        } else {
            log(LOGERR, "%s died under inscrutable circumstances\n", cmd);
            rc = -1;
        }
    }
    
    return rc;
}

#ifndef EXEC_RPM
static void process_rpm_output(struct p_open_st *st) 
{
    int c;
    
    //while ((c = fgetc(st->stream)) != EOF) {
    //  printf("%c", c);
        //msg(1, "_%c", c);
    //}
    /*while ((c = fgetc(st->stream)) != EOF)*/
    while (read(st->fd, &c, 1) == 1)
        msg(1, "_%c", c);
}

static void reaper (int sig)
{
    pid_t pid;

    sig = sig;
    while ((pid = waitpid (-1, NULL, WNOHANG)) > 0) {
	msg(0, "SIGCHLD from %d\n", pid);
    }
    
    signal (SIGCHLD, reaper);
}

#endif /* EXEC_RPM */


int packages_rpminstall(tn_array *pkgs, struct pkgset *ps, struct inst_s *inst) 
{
#ifndef EXEC_RPM    
    struct p_open_st pst;
#endif
    char **argv;
    char *cmd;
    int i, n, nopts = 0, ec;
    int nv = verbose;
    
    n = 128 + n_array_size(pkgs);
    argv = alloca((n + 1) * sizeof(*argv));
    argv[n] = NULL;
    n = 0;

    
    if (!packages_fetch(pkgs, inst->cachedir, 0))
        return 0;
    
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
    
    if (ps->flags & PSMODE_INSTALL)
        argv[n++] = "--install";
    else if (ps->flags & PSMODE_UPGRADE)
        argv[n++] = "--upgrade";
    else {
        n_assert(0);
        die();
    }

    if (nv > 0) {
        argv[n++] = "-vh";
        nv--;
    }

    if (nv > 0)
        nv--;
    
    while (nv-- > 0) 
        argv[n++] = "-v";
    
    if (inst->instflags & PKGINST_TEST)
        argv[n++] = "--test";
    
    if (inst->instflags & PKGINST_JUSTDB)
        argv[n++] = "--justdb";
        
    if (inst->instflags & PKGINST_FORCE)
        argv[n++] = "--force";
    
    if (inst->instflags & PKGINST_NODEPS)
        argv[n++] = "--nodeps";
	
    if (inst->rootdir) {
    	argv[n++] = "--root";
	argv[n++] = (char*)inst->rootdir;
    }

    argv[n++] = "--noorder";    /* packages always ordered */

    if (inst->rpmacros) 
        for (i=0; i<n_array_size(inst->rpmacros); i++) {
            argv[n++] = "--define";
            argv[n++] = n_array_nth(inst->rpmacros, i);
        }
    
    if (inst->rpmopts) 
        for (i=0; i<n_array_size(inst->rpmopts); i++)
            argv[n++] = n_array_nth(inst->rpmopts, i);
    
    nopts = n;
    for (i=0; i < n_array_size(ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
        if (pkg_is_marked(pkg)) {
            char path[PATH_MAX], *s, *name;
            char *pkgpath = pkg->pkgdir->path;
            int len;
            
            
            name = pkg_filename_s(pkg);
            
            if (vfile_url_type(pkgpath) == VFURL_PATH) {
                len = snprintf(path, sizeof(path), "%s/%s", pkgpath, name);
            
            } else {
                char buf[1024];
                
                vfile_url_as_dirpath(buf, sizeof(buf), pkgpath);
                len = snprintf(path, sizeof(path), "%s/%s/%s", inst->cachedir,
                               buf, name);
            }

            s = alloca(len + 1);
            memcpy(s, path, len);
            s[len] = '\0';
            argv[n++] = s;
        }
    }
    
        
    n_assert(n > nopts); 
    argv[n++] = NULL;

    if (verbose > 0) {
        char buf[1024], *p;
        p = buf;
        
        for (i=0; i<nopts; i++) 
            p += snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", argv[i]);
        *p = '\0';
        msg(1, "Executing%s...\n", buf);
    }
    

#if EXEC_RPM    
    ec = exec_rpm(cmd, argv);
    
#else  /* p_open() doesn't works propely with rpm, I don't now why */
    signal(SIGCHLD, reaper);
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
    
    if (n)
        verbose--;
#endif /* EXEC_RPM */

    if (ec == 0 && (inst->instflags & PKGINST_TEST) == 0 &&
        (inst->flags & INSTS_KEEP_DOWNLOADS) == 0) {
        
        n = nopts;
        for (i=0; i < n_array_size(ps->ordered_pkgs); i++) {
            struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
            int url_type;
            
            if (!pkg_is_marked(pkg))
                continue;
            
            url_type = vfile_url_type(pkg->pkgdir->path);
            if ((url_type & (VFURL_PATH | VFURL_UNKNOWN | VFURL_CDROM)) == 0) {
                DBG("unlink %s\n", argv[n]); 
                unlink(argv[n]);
            }
            n++;
        }
    }
        

    return ec == 0;
}

