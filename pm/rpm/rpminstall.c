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

#if HAVE_STRSIGNAL
# define __USE_GNU 1
#endif

#include <string.h>

//#if HAVE_STRSIGNAL
//# undef __USE_GNU
//#endif


#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <obstack.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <fnmatch.h>

#include <rpm/rpmlib.h>
#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>
#include <trurl/n_snprintf.h>

#include <vfile/vfile.h>
#include <vfile/p_open.h>

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgmisc.h"
#include "pkgdir/pkgdir.h"
#include "pkgset.h"
#include "poldek.h"
#include "poldek_intern.h"
#include "misc.h"
#include "pm_rpm.h"

#ifdef HAVE_OPENPTY

static void rpmr_process_output(struct p_open_st *st, int verbose_level) 
{
    int endl = 1;

    while (1) {
        struct timeval to = { 0, 200000 };
        fd_set fdset;
        int rc;
        
        if (p_wait(st)) {
            int yes = 1;
            ioctl(st->fd, FIONBIO, &yes);
        }
        
        FD_ZERO(&fdset);
        FD_SET(st->fd, &fdset);
        if ((rc = select(st->fd + 1, &fdset, NULL, NULL, &to)) < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            
            break;
            
        } else if (rc == 0) {
            if (p_wait(st))
                break;

        } else if (rc > 0) {
            char  buf[4096];
            int   n, i;

            if ((n = read(st->fd, buf, sizeof(buf) - 1)) <= 0)
                break;
            
            buf[n] = '\0';
            msg_tty(verbose_level, "_%s", buf);
            if (!log_enabled_filelog())
                continue;
                
            for (i=0; i < n; i++) {
                int c = buf[i];
                
                if (c == '\r')
                    continue;
                
                if (c == '\n')
                    endl = 1;
                    
                if (endl) {
                    endl = 0;
                    msg_f(0, "_\n");
                    msg_f(0, "rpm: %c", c);
                    continue;
                }
                msg_f(0, "_%c", c);
            }
        }
    }
    
    return;
}

int pm_rpm_execrpm(const char *cmd, char *const argv[], int ontty, int verbose_level)
{
    struct p_open_st pst;
    int n, ec;
    unsigned p_open_flags = P_OPEN_KEEPSTDIN;

    
    p_st_init(&pst);
    if (ontty)
        p_open_flags |= P_OPEN_OUTPTYS;
    
    if (p_open(&pst, p_open_flags, cmd, argv) == NULL) {
        if (pst.errmsg) {
            logn(LOGERR, "%s", pst.errmsg);
            p_st_destroy(&pst);
        }

        if (ontty == 0)
            return 0;

        p_open_flags &= ~P_OPEN_OUTPTYS;
        p_st_init(&pst);
        if (p_open(&pst, p_open_flags, cmd, argv) == NULL) {
            if (pst.errmsg)
                logn(LOGERR, "%s", pst.errmsg);
            p_st_destroy(&pst);
            return 0;
        }
    }
    
    
    n = 0;
    if (verbose == 0) {
        verbose = 1;
        n = 1;
    }

    rpmr_process_output(&pst, verbose_level);
    if ((ec = p_close(&pst) != 0) && pst.errmsg != NULL)
        logn(LOGERR, "%s", pst.errmsg);

    p_st_destroy(&pst);
    
    if (n)
        verbose--;
    
    return ec;
}

#else  /* HAVE_OPENPTY */
int pm_rpm_execrpm(const char *cmd, char *const argv[], int ontty, int verbose_level)
{
    int rc, st, n;
    pid_t pid;

    ontty = ontty;               /* unused */
    
    msg_f(1, _("Executing %s "), cmd);
    n = 0;
    while (argv[n])
        msg_f(1, "_%s ", argv[n++]);
    msg_f(1, "\n");
    if (access(cmd, X_OK) != 0) {
        logn(LOGERR, _("%s: no such file"), cmd);
        return -1;
    }
    
    if ((pid = fork()) == 0) {
        execv(cmd, argv);
        exit(EXIT_FAILURE);
        
    } else if (pid < 0) {
        logn(LOGERR, _("%s: no such file"), cmd);
        return -1;
    }

    rc = 0;
    while (wait(&st) > 0) {
        if (WIFEXITED(st)) {
            const char *errmsg_exited = N_("%s exited with %d");
            rc = WEXITSTATUS(st);
            if (rc != 0)
                logn(LOGERR, _(errmsg_exited), cmd, WEXITSTATUS(st));
            else 
                msgn_f(1, _(errmsg_exited), cmd, rc);
            
        } else if (WIFSIGNALED(st)) {
#if HAVE_STRSIGNAL
            logn(LOGERR, _("%s terminated by signal %s"), cmd,
                 strsignal(WTERMSIG(st)));
#else
            logn(LOGERR, _("%s terminated by signal %d"), cmd,
                 WTERMSIG(st));
#endif        
            rc = -1;
        } else {
            logn(LOGERR, _("%s died under inscrutable circumstances"), cmd);
            rc = -1;
        }
    }
    
    return rc;
}

#endif /* HAVE_FORKPTY */

static void find_commands(struct pm_rpm *pm)
{
    char path[PATH_MAX];
    
    if (!pm->rpm)
        if (vf_find_external_command(path, sizeof(path), "rpm", NULL))
            pm->rpm = n_strdup(path);

    if (!pm->sudo)
        if (vf_find_external_command(path, sizeof(path), "sudo", NULL))
            pm->sudo = n_strdup(path);
}

int pm_rpm_packages_install(struct pkgdb *db,
                            tn_array *pkgs, tn_array *pkgs_toremove,
                            struct poldek_ts *ts) 
{
    struct pm_rpm *pm = db->_ctx->modh;
    char **argv;
    char *cmd;
    int i, n, nopts = 0, ec, nsignerr = 0;
    int nv = verbose;

    pkgs_toremove = pkgs_toremove;

    find_commands(pm);
    if (!pm->rpm) {
        logn(LOGWARN, _("%s: command not found"), "rpm");
        return 0;
    }
    
    n = 128 + n_array_size(pkgs);
    argv = alloca((n + 1) * sizeof(*argv));
    argv[n] = NULL;
    n = 0;
    
    if (ts->getop(ts, POLDEK_OP_RPMTEST)) {
        cmd = pm->rpm;
        argv[n++] = "rpm";
        
    } else if (ts->getop(ts, POLDEK_OP_USESUDO) && getuid() != 0) {
        if (!pm->sudo) {
            logn(LOGWARN, _("%s: command not found"), "sudo");
            return 0;
        }

        cmd = pm->sudo;
        argv[n++] = "sudo";
        argv[n++] = pm->rpm;
        
    } else {
        cmd = pm->rpm;
        argv[n++] = "rpm";
    }
    
    if (poldek_ts_issetf(ts, POLDEK_TS_UPGRADE | POLDEK_TS_REINSTALL |
                         POLDEK_TS_DOWNGRADE))
        argv[n++] = "--upgrade";
    else
        argv[n++] = "--install";
    
    if (poldek_ts_issetf(ts, POLDEK_TS_REINSTALL)) {
        argv[n++] = "--replacefiles";
        argv[n++] = "--replacepkgs";
    }
        
    if (poldek_ts_issetf(ts, POLDEK_TS_DOWNGRADE)) {
        argv[n++] = "--oldpackage";
    }
        
    if (nv > 0) {
        argv[n++] = "-vh";
        nv--;
    }

    if (nv > 0)
        nv--;
    
    while (nv-- > 0) 
        argv[n++] = "-v";
    
    if (ts->getop(ts, POLDEK_OP_RPMTEST))
        argv[n++] = "--test";
    
    if (ts->getop(ts, POLDEK_OP_JUSTDB))
        argv[n++] = "--justdb";
        
    if (ts->getop(ts, POLDEK_OP_FORCE))
        argv[n++] = "--force";
    
    if (ts->getop(ts, POLDEK_OP_NODEPS))
        argv[n++] = "--nodeps";
	
    if (ts->rootdir) {
    	argv[n++] = "--root";
        argv[n++] = (char*)ts->rootdir;
    }

    argv[n++] = "--noorder";    /* packages always ordered by me */

    if (ts->rpmacros) 
        for (i=0; i<n_array_size(ts->rpmacros); i++) {
            argv[n++] = "--define";
            argv[n++] = n_array_nth(ts->rpmacros, i);
        }
    
    if (ts->rpmopts) 
        for (i=0; i < n_array_size(ts->rpmopts); i++)
            argv[n++] = n_array_nth(ts->rpmopts, i);
    
    
    nsignerr = 0;
    nopts = n;
    for (i=0; i < n_array_size(ts->ctx->ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ts->ctx->ps->ordered_pkgs, i);
        if (pkg_is_marked(ts->pms, pkg)) {
            char path[PATH_MAX], *s, *name;
            char *pkgpath = pkg->pkgdir->path;
            unsigned vrfyflags;
            int len;
            
            
            name = pkg_filename_s(pkg);
            if (vf_url_type(pkgpath) == VFURL_PATH) {
                len = n_snprintf(path, sizeof(path), "%s/%s", pkgpath, name);
            
            } else {
                char buf[1024];
                
                vf_url_as_dirpath(buf, sizeof(buf), pkgpath);
                len = n_snprintf(path, sizeof(path), "%s/%s/%s", ts->cachedir,
                                 buf, name);
            }

            if ((vrfyflags = pkg_get_verify_signflags(pkg))) {
                if (!pm_rpm_verify_signature(pm, path, vrfyflags)) {
                    logn(LOGERR, _("%s: signature verification failed"),
                         pkg_snprintf_s(pkg));
                    nsignerr++;
                }
            }
            
            
            s = alloca(len + 1);
            memcpy(s, path, len);
            s[len] = '\0';
            argv[n++] = s;
        }
    }

    if (nsignerr) {
        if (poldek_ts_is_interactive_on(ts) && ts->ask_fn) {
            if (!ts->ask_fn(0,
                              _("There were signature verification errors. "
                                "Proceed? [y/N]")))
                goto l_err_end;
            
        } else {
            goto l_err_end;
        }
    }
        
    n_assert(n > nopts); 
    argv[n++] = NULL;

    if (verbose > 0) {
        char buf[1024], *p;
        p = buf;
        
        for (i=0; i < nopts; i++) 
            p += n_snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", argv[i]);
        *p = '\0';
        msgn(1, _("Executing%s..."), buf);
    }
    
    
    ec = pm_rpm_execrpm(cmd, argv, 1, 1);
#if 0                           /* moved to packages_fetch_remove() */
    if (ec == 0 && !ts->getop(ts, POLDEK_OP_RPMTEST) &&
        !ts->getop(ts, POLDEK_OP_KEEP_DOWNLOADS)) {
        
        n = nopts;
        for (i=0; i < n_array_size(ts->ctx->ps->ordered_pkgs); i++) {
            struct pkg *pkg = n_array_nth(ts->ctx->ps->ordered_pkgs, i);
            int url_type;
            
            if (!pkg_is_marked(ts->pms, pkg))
                continue;
            
            url_type = vf_url_type(pkg->pkgdir->path);
            if ((url_type & (VFURL_PATH | VFURL_UNKNOWN)) == 0) {
                DBG("unlink %s\n", argv[n]); 
                unlink(argv[n]);
            }
            n++;
        }
    }
#endif
    return ec == 0;
    
 l_err_end:
    
    return 0;
}

int pm_rpm_packages_uninstall(struct pkgdb *db, 
                              tn_array *pkgs, struct poldek_ts *ts)
{
    struct pm_rpm *pm = db->_ctx->modh;
    char **argv;
    char *cmd;
    int i, n, nopts = 0;

    find_commands(pm);
    if (!pm->rpm) {
        logn(LOGWARN, _("%s: command not found"), "rpm");
        return 0;
    }
    
    n = 128 + n_array_size(pkgs);
    argv = alloca((n + 1) * sizeof(*argv));
    argv[n] = NULL;
    
    n = 0;
    
    if (ts->getop(ts, POLDEK_OP_RPMTEST)) {
        cmd = pm->rpm;
        argv[n++] = "rpm";
        
    } else if (ts->getop(ts, POLDEK_OP_USESUDO)) {
        if (!pm->sudo) {
            logn(LOGWARN, _("%s: command not found"), "sudo");
            return 0;
        }
        cmd = pm->sudo;
        argv[n++] = "sudo";
        argv[n++] = pm->rpm;
        
    } else {
        cmd = pm->rpm;
        argv[n++] = "rpm";
    }
    
    argv[n++] = "--erase";

    for (i=1; i<verbose; i++)
        argv[n++] = "-v";

    if (ts->getop(ts, POLDEK_OP_RPMTEST))
        argv[n++] = "--test";
    
    if (ts->getop(ts, POLDEK_OP_FORCE))
        argv[n++] = "--force";
    
    if (ts->getop(ts, POLDEK_OP_NODEPS))
        argv[n++] = "--nodeps";

    if (ts->rootdir) {
    	argv[n++] = "--root";
        argv[n++] = (char*)ts->rootdir;
    }

    if (ts->rpmopts) 
        for (i=0; i<n_array_size(ts->rpmopts); i++)
            argv[n++] = n_array_nth(ts->rpmopts, i);
    
    nopts = n;
    for (i=0; i < n_array_size(pkgs); i++) {
        char nevr[256];
        int len;
        
        len = pkg_snprintf(nevr, sizeof(nevr), n_array_nth(pkgs, i));
        argv[n] = alloca(len + 1);
        memcpy(argv[n], nevr, len + 1);
        n++;
    }
    
    n_assert(n > nopts); 
    argv[n++] = NULL;
    
    if (verbose > 0) {
        char buf[1024], *p;
        p = buf;
        
        for (i=0; i<nopts; i++) 
            p += n_snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", argv[i]);
        *p = '\0';
        msgn(1, _("Running%s..."), buf);
        
    }

    return pm_rpm_execrpm(cmd, argv, 0, 0) == 0;
}
