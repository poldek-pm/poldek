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

#include <rpm/rpmlib.h>
#include <rpm/rpmio.h>
#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>

#ifdef HAVE_RPM_4_1
# include <rpm/rpmts.h>
# include <rpm/rpmps.h>
# include <rpm/rpmdb.h>
# include <rpm/rpmcli.h>
#endif

#include <vfile/vfile.h>

#include "i18n.h"

#include "misc.h"
#include "log.h"
#include "pkg.h"

#include "rpm.h"
#include "rpmhdr.h"
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

#ifdef HAVE_RPM_4_1
static void *install_cb(const void *h __attribute__((unused)),
                        const rpmCallbackType op, 
                        const unsigned long amount, 
                        const unsigned long total,
                        const void *pkgpath,
                        void *data __attribute__((unused)))
{
    void *r = NULL;
    static FD_t fd = NULL;
 
    switch (op) {
        case RPMCALLBACK_INST_OPEN_FILE:
            fd = Fopen(pkgpath, "r.ufdio");
            if (fd == NULL || Ferror(fd)) {
                logn(LOGERR, "%s: %s", (const char*)pkgpath, Fstrerror(fd));
                if (fd)
                    Fclose(fd);
                
            } else {
                int oldfl;
                fd = fdLink(fd, NULL);
                oldfl = Fcntl(fd, F_GETFD, 0);
                if (oldfl >= 0) {
                    oldfl |= FD_CLOEXEC; /* scripts shouldn't inherit
                                            rpm file descriptor */
                    Fcntl(fd, F_SETFD, (void*)oldfl); /* what for is void*? */
                }
            }
            return fd;
            
        case RPMCALLBACK_INST_CLOSE_FILE:
            fd = fdFree(fd, NULL);
            if (fd) {
                Fclose(fd);
                fd = NULL;
            }
            break;

        case RPMCALLBACK_INST_START:
            msgn(0, _("Installing %s"), n_basenam(pkgpath));
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

#else  /* callback for < 4.1 series */

static void *install_cb(const void *h __attribute__((unused)),
                        const rpmCallbackType op, 
                        const unsigned long amount, 
                        const unsigned long total,
                        const void *pkgpath,
                        void *data __attribute__((unused)))
{
    void *rc = NULL;
    
 
    switch (op) {
        case RPMCALLBACK_INST_OPEN_FILE:
        case RPMCALLBACK_INST_CLOSE_FILE:
            n_assert(0);
            break;

        case RPMCALLBACK_INST_START:
            msgn(0, _("Installing %s"), n_basenam(pkgpath));
            progress(amount, total);
            break;

        case RPMCALLBACK_INST_PROGRESS:
            progress(amount, total);
            break;

        default:
            break;                 /* do nothing */
    }
    
    return rc;
}
#endif /* HAVE_RPM_4_1 */


#ifdef HAVE_RPM_4_1
# define printdepProblems(file, conflicts, numConflicts) rpmpsPrint(file, conflicts)
# define printProblems(file,probs) rpmpsPrint(file, probs)
# define freeConflicts(conflicts, numConflicts) conflicts = rpmpsFree(conflicts)
# define freeProblems(probs) rpmpsFree(probs)
# define freeTS(ts) rpmtsFree(ts)
#else
# define printdepProblems(file, conflicts, numConflicts) printDepProblems(file, conflicts, numConflicts)
# define printProblems(file,probs) rpmProblemSetPrint(file, probs)
# define freeConflicts(conflicts, numConflicts) rpmdepFreeConflicts(conflicts, numConflicts)
# define freeProblems(probs) rpmProblemSetFree(probs)
# define freeTS(ts) rpmtransFree(ts)
#endif

int rpm_install(rpmdb db, const char *rootdir, const char *path,
                unsigned filterflags, unsigned transflags, unsigned instflags)
{
#ifdef HAVE_RPM_4_1
    rpmts ts = NULL;
    rpmps probs = NULL;
#else
    rpmTransactionSet ts = NULL;
    rpmProblemSet probs = NULL;
#endif
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

    if (!rpmhdr_loadfdt(fdt, &h, path)) {
        goto l_err;
        
    } else if (rpmhdr_issource(h)) {
        logn(LOGERR, _("%s: source packages are not supported"), path);
        goto l_err;
    }

    
#ifdef HAVE_RPM_4_1
    db = db;   /* avoid gcc's warn */
	ts = rpmtsCreate();
	rpmtsSetRootDir(ts, rootdir);
	rpmtsOpenDB(ts, O_RDWR);
    rc = rpmtsAddInstallElement(ts, h, vfile_localpath(vf),
                                (instflags & INSTALL_UPGRADE) != 0, NULL);
#else
    ts = rpmtransCreateSet(db, rootdir);
    rc = rpmtransAddPackage(ts, h, fdt, path, 
                            (instflags & INSTALL_UPGRADE) != 0, NULL);
#endif
    
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
#ifdef HAVE_RPM_4_1
	    rpmps conflicts = NULL;
#else
# ifdef HAVE_RPM_4_0_4
        rpmDependencyConflict conflicts = NULL;
# else /* rpm 3.x */
        struct rpmDependencyConflict *conflicts = NULL;
# endif
#endif
        int numConflicts = 0;

#ifdef HAVE_RPM_4_1
	    if (rpmtsCheck(ts) != 0) {
            logn(LOGERR, "%s: rpmtsCheck() failed", path);
            goto l_err;
	    }
	    conflicts = rpmtsProblems(ts);
        numConflicts = rpmpsNumProblems(conflicts);
#else
        if (rpmdepCheck(ts, &conflicts, &numConflicts) != 0) {
            logn(LOGERR, "%s: rpmdepCheck() failed", path);
            goto l_err;
        }            
#endif
                
        if (conflicts) {
            FILE *fstream;
                
            logn(LOGERR, _("%s: failed dependencies:"), path);
                

            printdepProblems(log_stream(), conflicts, numConflicts);
            if ((fstream = log_file_stream()))
                printdepProblems(fstream, conflicts, numConflicts);
            freeConflicts(conflicts, numConflicts);
            goto l_err;
        }
    }

#ifdef HAVE_RPM_4_1
    rpmtsSetFlags(ts, transflags);
    rpmtsSetNotifyCallback(ts, install_cb, NULL);
	rc = rpmtsRun(ts, NULL, (rpmprobFilterFlags) filterflags);
#else
	rc = rpmRunTransactions(ts, install_cb,
                            (void *) ((long)instflags), 
                            NULL, &probs, transflags, filterflags);
#endif

    if (rc != 0) {
        if (rc > 0) {
            FILE *fstream;
#ifdef HAVE_RPM_4_1
            probs = rpmtsProblems(ts);
#endif
            logn(LOGERR, _("%s: installation failed:"), path);
            printProblems(log_stream(), probs);
            if ((fstream = log_file_stream()))
                printProblems(fstream, probs);
            goto l_err;
            
        } else {
            logn(LOGERR, _("%s: installation failed (hgw why)"), path);
        }
    }
    
    
    vfile_close(vf);
    if (probs)
        freeProblems(probs);
    
#ifdef HAVE_RPM_4_1             
    rpmtsCloseDB(ts); /* not sure that it's really necessary */
#endif    
    freeTS(ts);
    return 1;
    
 l_err:
    if (fdt) 
        Fclose(fdt);
    
    if (vf)
        vfile_close(vf);

    if (probs)
        freeProblems(probs);
    
    if (ts)
        freeTS(ts);
    
    if (h)
        headerFree(h);
    
    return 0;
}

