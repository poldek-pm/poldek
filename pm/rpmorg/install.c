/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

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

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>

#include <sys/time.h>           /* rpm5 needs timeval */
#include <stdio.h>              /* rpm5 headers needs FILE* */

#include <vfile/vfile.h>

#include "i18n.h"
#include "misc.h"
#include "log.h"
#include "pkg.h"

#include "pm_rpm.h"

static void progress(const unsigned long amount, const unsigned long total)
{
    static int last_v = 0;

    if (amount == 0) {     /* first notification */
        last_v = 0;

    } else {
        char   line[256], outline[256], fmt[40];
        float  frac, percent;
        int    barwidth = 75, n;


        frac = (float) amount / (float) total;
        percent = frac * 100.0f;

        barwidth -= 7;
        n = (int) (((float)barwidth) * frac);

        if (n <= last_v)
            return;

        n_assert(last_v < 100);

        memset(line, '.', n);
        line[n] = '\0';
        snprintf(fmt, sizeof(fmt), "%%-%ds %%5.1f%%%%", barwidth);
        snprintf(outline, sizeof(outline), fmt, line, percent);

        if (amount && amount == total) { /* last notification */
            msg_tty(0, "\r%s\n", outline);
        } else {
            msg_tty(0, "\r%s", outline);
        }
    }
}

static void *install_cb(const void *h __attribute__((unused)),
                        const enum rpmCallbackType_e op,
                        const long unsigned int amount,
                        const long unsigned int total,
                        const void *pkgpath,
                        void *data __attribute__((unused)))
{
    void *r = NULL;
    static FD_t fd = NULL;

    switch (op) {
        case RPMCALLBACK_INST_OPEN_FILE:
            if (pkgpath == NULL || *(char*)pkgpath == '\0')
                return NULL;

            fd = Fopen(pkgpath, "r.ufdio");
            if (fd == NULL || Ferror(fd)) {
                logn(LOGERR, "%s: %s", (const char*)pkgpath, Fstrerror(fd));
                if (fd)
                    Fclose(fd);

            } else {
                int oldfl;
                fd = fdLink(fd);
                oldfl = Fcntl(fd, F_GETFD, 0);
                if (oldfl >= 0) {
                    oldfl |= FD_CLOEXEC; /* scripts shouldn't inherit
                                            rpm file descriptor */
                    fcntl(Fileno(fd), F_SETFD, oldfl);
                }
            }
            return fd;

        case RPMCALLBACK_INST_CLOSE_FILE:
            fd = fdFree(fd);
            if (fd) {
                Fclose(fd);
                fd = NULL;
            }
            break;

        case RPMCALLBACK_INST_START:
            progress(amount, total);
            break;

        case RPMCALLBACK_INST_PROGRESS:
            progress(amount, total);
            break;

        default:
            break;                 /* do nothing */
    }

    return r;
}

static int do_dbinstall(rpmdb db, const char *rootdir, const char *path,
                        unsigned filterflags, unsigned transflags,
                        unsigned instflags)
{
    rpmts ts = NULL;
    rpmps probs = NULL;

    struct vfile *vf = NULL;
    int rc;
    Header h = NULL;
    FD_t fdt = NULL;

    if (rootdir == NULL)
        rootdir = "/";

    if ((vf = vfile_open(path, VFT_IO, VFM_RO | VFM_STBRN)) == NULL)
        return 0;

    fdt = fdDup(vf->vf_fd);
    if (fdt == NULL || Ferror(fdt)) {
        const char *err = "unknown error";
        if (fdt)
            err = Fstrerror(fdt);

        logn(LOGERR, "rpmio's fdDup failed: %s", err);
        goto l_err;
    }

    if (!pm_rpmhdr_loadfdt(fdt, &h, path)) {
        goto l_err;

    } else if (pm_rpmhdr_issource(h)) {
        logn(LOGERR, _("%s: source packages are not supported"), path);
        goto l_err;
    }


    db = db;   /* avoid gcc's warn */
    ts = rpmtsCreate();
    rpmtsSetRootDir(ts, rootdir);
    rpmtsOpenDB(ts, O_RDWR);
    rc = rpmtsAddInstallElement(ts, h, vfile_localpath(vf),
                                (instflags & INSTALL_UPGRADE) != 0, NULL);
    headerFree(h);
    h = NULL;

    switch(rc) {
        case 0:
            break;

        case 1:
            logn(LOGERR, _("%s: rpm read error"), path);
            goto l_err;
            break;


        case 2:
            logn(LOGERR, _("%s requires a newer version of RPM"), path);
            goto l_err;
            break;

        default:
            logn(LOGERR, "%s: rpmtransAddPackage() failed", path);
            goto l_err;
            break;
    }

    if ((instflags & INSTALL_NODEPS) == 0) {
        rpmps conflicts = NULL;
        int numConflicts = 0;

        if (rpmtsCheck(ts) != 0) {
            logn(LOGERR, "%s: rpmtsCheck() failed", path);
            goto l_err;
        }
        conflicts = rpmtsProblems(ts);
        numConflicts = rpmpsNumProblems(conflicts);

        if (numConflicts) {
            logn(LOGERR, _("%s: failed dependencies:"), n_basenam(path));
            rpmpsPrint(stderr, conflicts);
            conflicts = rpmpsFree(conflicts);
            goto l_err;
        }
    }

    msgn(0, _("Installing %s..."), n_basenam(path));

    rpmtsSetFlags(ts, transflags);
    rpmtsSetNotifyCallback(ts, install_cb, NULL);
    rc = rpmtsRun(ts, NULL, (rpmprobFilterFlags) filterflags);

    if (rc != 0) {
        if (rc > 0) {
            probs = rpmtsProblems(ts);
            logn(LOGERR, _("%s: installation failed:"), path);
            rpmpsPrint(stderr, probs); /* XXX: rpm logging API... */
            goto l_err;

        } else {
            logn(LOGERR, _("%s: installation failed (retcode=%d)"), path, rc);
        }
    }


    vfile_close(vf);
    if (probs)
        probs = rpmpsFree(probs);

    rpmtsCloseDB(ts); /* XXX not sure that it's really necessary */
    rpmtsFree(ts);

    return 1;

 l_err:
    if (fdt)
        Fclose(fdt);

    if (vf)
        vfile_close(vf);

    if (probs)
        rpmpsFree(probs);

    if (ts)
        rpmtsFree(ts);

    if (h)
        headerFree(h);

    return 0;
}


int pm_rpm_install_package(struct pkgdb *db, const char *path,
                           const struct poldek_ts *ts)
{
    unsigned instflags = 0, filterflags = 0, transflags = 0;

    n_assert(db->dbh);


    if (ts->getop(ts, POLDEK_OP_NODEPS))
        instflags |= INSTALL_NODEPS;

    if (ts->getop(ts, POLDEK_OP_JUSTDB))
        transflags |= RPMTRANS_FLAG_JUSTDB;

    if (ts->getop(ts, POLDEK_OP_TEST))
        transflags |= RPMTRANS_FLAG_TEST;

    if (ts->getop(ts, POLDEK_OP_FORCE))
        filterflags |= RPMPROB_FILTER_REPLACEPKG |
            RPMPROB_FILTER_REPLACEOLDFILES |
            RPMPROB_FILTER_REPLACENEWFILES |
            RPMPROB_FILTER_OLDPACKAGE;

    filterflags |= RPMPROB_FILTER_DISKSPACE;
    instflags |= INSTALL_NOORDER | INSTALL_UPGRADE;


    return do_dbinstall(db->dbh, db->rootdir, path,
                        filterflags, transflags, instflags);
}
