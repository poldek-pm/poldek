/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include <signal.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <fnmatch.h>

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
            char  buf[4096], *fmt = "%s";
            int   n, i;

            if ((n = read(st->fd, buf, sizeof(buf) - 1)) <= 0)
                break;
            buf[n] = '\0';
            msg_tty(verbose_level, fmt, buf);

            /* logged to file? -> prefix lines with 'rpm: ' */
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
    int vsaved = -999, ec;
    unsigned p_open_flags = P_OPEN_KEEPSTDIN;

    
    p_st_init(&pst);
    if (ontty)
        p_open_flags |= P_OPEN_OUTPTYS;
    
    if (p_open(&pst, p_open_flags, cmd, argv) == NULL) {
        if (pst.errmsg) {
            if (ontty == 0)     /* if not try without P_OPEN_OUTPTYS */
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

    vsaved = -999;
    if (poldek_VERBOSE == 0)
        vsaved = poldek_set_verbose(1);
    
    rpmr_process_output(&pst, verbose_level);
    if ((ec = p_close(&pst) != 0) && pst.errmsg != NULL)
        logn(LOGERR, "%s", pst.errmsg);

    p_st_destroy(&pst);

    if (vsaved != -999)
        poldek_set_verbose(vsaved);
    
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
        printf("fork\n");
        
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

static void setup_command(char **cmdp, const char *defaultcmd) 
{
    char path[PATH_MAX];
    
    if (*cmdp == NULL) {
        if (vf_find_external_command(path, sizeof(path), defaultcmd, NULL))
            *cmdp = n_strdup(path);
        
    } else if (access(*cmdp, R_OK) != 0 && **cmdp != '/') {
        if (vf_find_external_command(path, sizeof(path), *cmdp, NULL)) {
            n_cfree(cmdp);
            *cmdp = n_strdup(path);
        } /* try to run *cmdp anyway */
    }
}
    

void pm_rpm_setup_commands(struct pm_rpm *pm)
{
    if (pm->flags & PM_RPM_CMDSETUP_DONE)
        return;
    
    setup_command(&pm->rpm, "rpm");
    setup_command(&pm->sudo, "sudo");

    pm->flags |= PM_RPM_CMDSETUP_DONE;
}

/* colors equal? there are repository types without color info, so catch them */
static int colors_eq(const struct pkg *pkg, const char *path)
{
    Header h;
    int color = -1;
    
    if (pm_rpmhdr_loadfile(path, &h)) {
#ifdef HAVE_RPM_HGETCOLOR
        color = hGetColor(h);
#endif
        headerFree(h);
    }
    
    if (color > 0 && (unsigned)color == pkg->color)
        return 1;

    if (color == 0 && pkg->color == 0)
        return 1;
    
    if (color == -1 && pkg->color > 0)
        logn(LOGERR, "%s: package has color (%d), "
             "but rpm without multilib support is used", pkg_id(pkg), pkg->color);
    
    else if (pkg->color != (unsigned)color)
        logn(LOGERR, "%s package color (%d) is not equal to %s's one (%d)",
             pkg_id(pkg), pkg->color, n_basenam(path), color);

    return 0;
}


int pm_rpm_packages_install(struct pkgdb *db, const tn_array *pkgs,
                            const tn_array *pkgs_toremove,
                            struct poldek_ts *ts) 
{
    struct pm_rpm *pm = db->_ctx->modh;
    char **argv;
    char *cmd;
    int i, nargs, nopts = 0, ec, nsignerr = 0, ncolorerr = 0;
    int nverbose = poldek_VERBOSE;

    pkgs_toremove = pkgs_toremove;

    pm_rpm_setup_commands(pm);
    if (pm->rpm == NULL) {
        logn(LOGERR, _("%s: command not found"), "rpm");
        return 0;
    }
    
    DBGF("rpm = %s\n", pm->rpm);
    nargs = 128 + n_array_size(pkgs);
    argv = alloca((nargs + 1) * sizeof(*argv));
    argv[nargs] = NULL;
    nargs = 0;
    
    if (ts->getop(ts, POLDEK_OP_RPMTEST)) {
        cmd = pm->rpm;
        argv[nargs++] = n_basenam(pm->rpm);
        
    } else if (ts->getop(ts, POLDEK_OP_USESUDO) && getuid() != 0) {
        if (!pm->sudo) {
            logn(LOGERR, _("%s: command not found"), "sudo");
            return 0;
        }

        cmd = pm->sudo;
        argv[nargs++] = n_basenam(pm->sudo);
        argv[nargs++] = pm->rpm;
        
    } else {
        cmd = pm->rpm;
        argv[nargs++] = n_basenam(pm->rpm);
    }
    
    if (poldek_ts_issetf(ts, POLDEK_TS_UPGRADE | POLDEK_TS_REINSTALL |
                         POLDEK_TS_DOWNGRADE))
        argv[nargs++] = "--upgrade";
    else
        argv[nargs++] = "--install";
    
    if (poldek_ts_issetf(ts, POLDEK_TS_REINSTALL)) {
        argv[nargs++] = "--replacefiles";
        argv[nargs++] = "--replacepkgs";
    }
        
    if (poldek_ts_issetf(ts, POLDEK_TS_DOWNGRADE)) {
        argv[nargs++] = "--oldpackage";
    }
        
    if (nverbose > 0) {
        argv[nargs++] = "-vh";
        nverbose--;
    }

    if (nverbose > 0)
        nverbose--;
    
    while (nverbose-- > 0) 
        argv[nargs++] = "-v";
    
    if (ts->getop(ts, POLDEK_OP_RPMTEST))
        argv[nargs++] = "--test";

    if (ts->getop(ts, POLDEK_OP_JUSTDB))
        argv[nargs++] = "--justdb";
        
    if (ts->getop(ts, POLDEK_OP_FORCE))
        argv[nargs++] = "--force";
    
    if (ts->getop(ts, POLDEK_OP_NODEPS))
        argv[nargs++] = "--nodeps";
	
    if (ts->rootdir) {
    	argv[nargs++] = "--root";
        argv[nargs++] = (char*)ts->rootdir;
    }

#if 0				    /* commented out because of rpm errors */
    argv[nargs++] = "--noorder";    /* packages always ordered by me */
#endif

    if (ts->rpmacros) 
        for (i=0; i<n_array_size(ts->rpmacros); i++) {
            argv[nargs++] = "--define";
            argv[nargs++] = n_array_nth(ts->rpmacros, i);
        }
    
    if (ts->rpmopts) 
        for (i=0; i < n_array_size(ts->rpmopts); i++)
            argv[nargs++] = n_array_nth(ts->rpmopts, i);
    
    
    nsignerr = 0;
    nopts = nargs;
    for (i=0; i < n_array_size(pkgs); i++) {
        char path[PATH_MAX], *s, name[1024], *pkgpath;
        unsigned vrfyflags;
        struct pkg *pkg;
        int len;

        pkg = n_array_nth(pkgs, i);
        pkgpath = pkg->pkgdir->path;
        
        pkg_filename(pkg, name, sizeof(name));
        if (vf_url_type(pkgpath) == VFURL_PATH) {
            len = n_snprintf(path, sizeof(path), "%s/%s", pkgpath, name);
            
        } else {
            char buf[1024];
                
            vf_url_as_dirpath(buf, sizeof(buf), pkgpath);
            len = n_snprintf(path, sizeof(path), "%s/%s/%s", ts->cachedir,
                             buf, n_basenam(name));
        }

        if ((vrfyflags = pkg_get_verify_signflags(pkg))) {
            if (!pm_rpm_verify_signature(pm, path, vrfyflags)) {
                logn(LOGERR, _("%s: signature verification failed"),
                     pkg_snprintf_s(pkg));
                nsignerr++;
            }
        }

        if (ts->getop(ts, POLDEK_OP_MULTILIB) && !colors_eq(pkg, path))
            ncolorerr++;
        
        s = alloca(len + 1);
        memcpy(s, path, len);
        s[len] = '\0';
        argv[nargs++] = s;
    }

    
    if (!ts->getop(ts, POLDEK_OP_RPMTEST) && (nsignerr || ncolorerr)) {
        int can_ask = poldek_ts_is_interactive_on(ts);

        if (nsignerr) {
            if (!can_ask || !poldek__confirm(ts, 0,
                                            _("There were signature verification errors. "
                                              "Proceed?")))
                goto l_err_end;
        }
        

        if (ncolorerr) {
            if (!can_ask || !poldek__confirm(ts, 0,
                                             _("There were package coloring mismatches. "
                                               "Proceed?")))
                goto l_err_end;
        }
    }
    
    n_assert(nargs > nopts); 
    argv[nargs] = NULL;

    if (poldek_VERBOSE) {
        char buf[8192], *p;
        p = buf;
        
        for (i=0; i < nopts; i++) 
            p += n_snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", argv[i]);

        if (poldek_VERBOSE > 1) {
            for (i=nopts; i < nargs; i++) 
                p += n_snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", n_basenam(argv[i]));
        }

        *p = '\0';
        msgn(1, _("Executing%s..."), buf);
    }
    
    ec = pm_rpm_execrpm(cmd, argv, 1, 1);
    return ec == 0;
    
 l_err_end:
    return 0;
}

int pm_rpm_packages_uninstall(struct pkgdb *db, const tn_array *pkgs,
                              struct poldek_ts *ts)
{
    struct pm_rpm *pm = db->_ctx->modh;
    char **argv;
    char *cmd;
    int i, nargs, nopts = 0;

    pm_rpm_setup_commands(pm);
    if (!pm->rpm) {
        logn(LOGERR, _("%s: command not found"), "rpm");
        return 0;
    }
    
    nargs = 128 + n_array_size(pkgs);
    argv = alloca((nargs + 1) * sizeof(*argv));
    argv[nargs] = NULL;
    
    nargs = 0;
    
    if (ts->getop(ts, POLDEK_OP_RPMTEST)) {
        cmd = pm->rpm;
        argv[nargs++] = n_basenam(pm->rpm);
        
    } else if (ts->getop(ts, POLDEK_OP_USESUDO)) {
        if (!pm->sudo) {
            logn(LOGWARN, _("%s: command not found"), "sudo");
            return 0;
        }
        cmd = pm->sudo;
        argv[nargs++] = n_basenam(pm->sudo);
        argv[nargs++] = pm->rpm;
        
    } else {
        cmd = pm->rpm;
        argv[nargs++] = n_basenam(pm->rpm);
    }
    
    argv[nargs++] = "--erase";

    for (i=1; i < poldek_VERBOSE; i++)
        argv[nargs++] = "-v";

    if (ts->getop(ts, POLDEK_OP_RPMTEST))
        argv[nargs++] = "--test";

    if (ts->getop(ts, POLDEK_OP_JUSTDB))
        argv[nargs++] = "--justdb";
    
    if (ts->getop(ts, POLDEK_OP_FORCE))
        argv[nargs++] = "--force";
    
    if (ts->getop(ts, POLDEK_OP_NODEPS))
        argv[nargs++] = "--nodeps";

    if (ts->rootdir) {
    	argv[nargs++] = "--root";
        argv[nargs++] = (char*)ts->rootdir;
    }
    
    argv[nargs++] = "--noorder";
    
    if (ts->rpmopts) 
        for (i=0; i<n_array_size(ts->rpmopts); i++)
            argv[nargs++] = n_array_nth(ts->rpmopts, i);
    
    nopts = nargs;
    
    /* rpm -e removes packages in reverse order  */
    for (i = n_array_size(pkgs) - 1; i >= 0; i--) {
        const char *id;
        int idlen;

        id = pkg_id(n_array_nth(pkgs, i));
        idlen = strlen(id);
        
        argv[nargs] = alloca(idlen + 1);
        memcpy(argv[nargs], id, idlen + 1);
        nargs++;
    }
    
    n_assert(nargs > nopts); 
    argv[nargs] = NULL;
    
    if (poldek_VERBOSE > 0) {
        char buf[8192], *p;
        p = buf;
        
        for (i=0; i < nopts; i++) 
            p += n_snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", argv[i]);

        if (poldek_VERBOSE > 1) {
            for (i=nopts; i < nargs; i++) 
                p += n_snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", argv[i]);
        }

        *p = '\0';
        msgn(1, _("Running%s..."), buf);
    }

    return pm_rpm_execrpm(cmd, argv, 1, 0) == 0;
}
