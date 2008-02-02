/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

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
#include <rpm/rpmurl.h>
#include <rpm/rpmmacro.h>

#ifdef HAVE_RPM_4_1
# include <rpm/rpmts.h>
# include <rpm/rpmps.h>
# include <rpm/rpmdb.h>
# include <rpm/rpmcli.h>
#endif

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>


#include "i18n.h"
#include "depdirs.h"
#include "misc.h"
#include "log.h"
#include "pkg.h"
#include "capreq.h"
#include "pkgmisc.h"
#include "pm_rpm.h"

#ifdef HAVE_RPM_4_2
/* from internal lib/signature.h, no public prototype (at least in 4.3 snaps) */
typedef enum sigType_e {
    RPMSIGTYPE_HEADERSIG= 5     /*!< Header style signature */
} sigType;
rpmRC rpmReadSignature(FD_t fd, Header *sighp, sigType sig_type, const char ** msg);
/* lib/rpmlead.h */
rpmRC readLead(FD_t fd, /*@out@*/ struct rpmlead *lead);
#endif



/* seems foolish, just beacuse historical reasons */
static int rpm_read_signature(FD_t fd, Header *sighp, int sig_type)
{
#ifdef HAVE_RPM_4_2
    return rpmReadSignature(fd, sighp, sig_type, NULL) == 0;
#elif HAVE_RPM_4_1
    return rpmReadSignature(fd, sighp, sig_type, NULL) == 0;
#elif HAVE_RPM_4_0
    return rpmReadSignature(fd, sighp, sig_type, NULL) == 0;
#endif
    return 0;
}
    
/* rpmlib's rpmCheckSig reports success when GPG signature is missing,
   so it is useless for real sig verification */
#if !defined HAVE_RPM_4_0
static int rpm_signatures(const char *path, unsigned *signature_flags, FD_t *fd)
{
    *signature_flags = VRFYSIG_DGST;
    path = path;
    return 1;
}

#else  /* 4.x series  */
static int rpm_signatures(const char *path, unsigned *signature_flags, FD_t *fd) 
{
    unsigned        flags;
    FD_t            fdt;
    struct rpmlead  lead;
    Header          sign = NULL;
    int32_t         tag, type, cnt;
    const void      *ptr;
    HeaderIterator  it;

    *signature_flags = 0;
    
    fdt = Fopen(path, "r.ufdio");
    if (fdt == NULL || Ferror(fdt)) {
        if (fdt)
            Fclose(fdt);
        return 0;
    }

    if (readLead(fdt, &lead) != 0) {
        logn(LOGERR, "%s: read package lead failed", n_basenam(path));
        Fclose(fdt);
        return 0;
    }

    if (!rpm_read_signature(fdt, &sign, lead.signature_type)) {
        logn(LOGERR, "%s: read package signature failed", n_basenam(path));
        Fclose(fdt);
        return 0;
    }
    
    if (sign == NULL) {
        logn(LOGERR, "%s: no signatures available", n_basenam(path));
        Fclose(fdt);
        return 0;
    }

    if (fd) {
        Fseek(fdt, 0, SEEK_SET);
        *fd = fdt;              /* fd to the caller */
        
    } else {
        Fclose(fdt);
    }
    

    flags = 0;
    it = headerInitIterator(sign);
    
    while (headerNextIterator(it, &tag, &type, &ptr, &cnt)) {
        switch (tag) {
#ifdef HAVE_RPM_4_1
            case RPMSIGTAG_RSA:
#endif                
            case RPMSIGTAG_PGP5:	/* XXX legacy */
            case RPMSIGTAG_PGP:
                flags |= VRFYSIG_SIGNPGP;
                break;

#ifdef HAVE_RPM_4_1
            case RPMSIGTAG_DSA:
#endif                
            case RPMSIGTAG_GPG:
                flags |= VRFYSIG_SIGNGPG;
                break;
                
            case RPMSIGTAG_LEMD5_2:
            case RPMSIGTAG_LEMD5_1:
            case RPMSIGTAG_MD5:
                flags |= VRFYSIG_DGST;
                break;
                
            default:
                continue;
                break;
        }
        ptr = headerFreeData(ptr, type);
    }

    headerFreeIterator(it);
    rpmFreeSignature(sign);
    *signature_flags = flags;
    return 1;
}
#endif 




#ifdef HAVE_RPMCHECKSIG         /* 4.0.x series */

#ifdef HAVE_RPM_4_1
# error "shouldn't happen"      /* 4.1 hasnt't rpmCheckSig */
#endif
static
int do_verify_signature(const char *path, unsigned flags) 
{
    const char *argv[2];
    unsigned presented_signs;

    n_assert(flags & (CHECKSIG_MD5 | CHECKSIG_GPG | CHECKSIG_PGP));

    if ((flags & (CHECKSIG_GPG | CHECKSIG_PGP))) {
        presented_signs = 0;
        
        if (!rpm_signatures(path, &presented_signs, NULL)) {
            logn(LOGERR, "unknown error");
            return 0;
        }
        	
        
        if ((presented_signs & flags) == 0) {
            char signam[255];
            int n = 0;
            
            if (flags & CHECKSIG_MD5)
                n += n_snprintf(&signam[n], sizeof(signam) - n, "md5/");
            
            if (flags & CHECKSIG_GPG)
                n += n_snprintf(&signam[n], sizeof(signam) - n, "gpg/");
            
            if (flags & CHECKSIG_PGP)
                n += n_snprintf(&signam[n], sizeof(signam) - n, "pgp/");
            
            n_assert(n > 0);
            signam[n - 1] = '\0';   /* eat last '/' */
            logn(LOGWARN, _("%s: %s signature not found"), n_basenam(path),
                 signam);
            return 0;
        }
    }
    	
    

    argv[0] = path;
    argv[1] = NULL;

    return rpmCheckSig(flags, argv) == 0;
}

#else  /* rpm 4.1 */
static
int do_verify_signature(const char *path, unsigned flags) 
{
    unsigned                  presented_signs = 0;
    struct rpmQVKArguments_s  qva; /* poor RPM API... */
    rpmts                     ts;
    FD_t                      fdt = NULL;
    int                       rc;

    
    n_assert(flags & (VRFYSIG_DGST | VRFYSIG_SIGN));
    
    if (!rpm_signatures(path, &presented_signs, NULL))
        return 0;
    
    if ((presented_signs & flags) == 0) {
        char signam[255];
        int n = 0;
            
        if (flags & VRFYSIG_DGST)
            n += n_snprintf(&signam[n], sizeof(signam) - n, "digest/");
            
        if (flags & VRFYSIG_SIGNGPG)
            n += n_snprintf(&signam[n], sizeof(signam) - n, "gpg/");
            
        if (flags & VRFYSIG_SIGNPGP)
            n += n_snprintf(&signam[n], sizeof(signam) - n, "pgp/");
            
        n_assert(n > 0);
        signam[n - 1] = '\0';   /* eat last '/' */
        logn(LOGWARN, _("%s: %s signature not found"), n_basenam(path),
             signam);
        return 0;
    }
    
    memset(&qva, '\0', sizeof(qva));
    qva.qva_flags = flags;
    

    rc = -1;
    fdt = Fopen(path, "r.ufdio");

    if (fdt != NULL && Ferror(fdt) == 0) {
        ts = rpmtsCreate();
        rc = rpmVerifySignatures(&qva, ts, fdt, n_basenam(path));
        rpmtsFree(ts);
        
        DBGF("rpmVerifySignatures[md%d, sign%d] %s %s\n",
             flags & VRFYSIG_DGST ? 1:0, flags & VRFYSIG_SIGN ? 1:0, 
             n_basenam(path), rc == 0 ? "OK" : "BAD");
    }
    
    if (fdt)
        Fclose(fdt);
    
    return rc == 0;
}

#endif


#ifdef HAVE_RPM_4_0
static
int do_pm_rpm_verify_signature(void *pm_rpm, const char *path, unsigned flags) 
{
    unsigned rpmflags = 0;

    pm_rpm = pm_rpm;
    if (access(path, R_OK) != 0) {
        logn(LOGERR, "%s: verify signature failed: %m", path);
        return 0;
    }
    
    if (flags & PKGVERIFY_GPG)
        rpmflags |= VRFYSIG_SIGNGPG;

    if (flags & PKGVERIFY_PGP)
        rpmflags |= VRFYSIG_SIGNPGP;
    
    if (flags & PKGVERIFY_MD)
        rpmflags |= VRFYSIG_DGST;

    return do_verify_signature(path, rpmflags);
}

#else  /* HAVE_RPMCHECKSIG */
extern int pm_rpm_execrpm(const char *cmd, char *const argv[],
                          int ontty, int verbose_level);
static
int do_pm_rpm_verify_signature(void *pm_rpm, const char *path, unsigned flags) 
{
    struct pm_rpm *pm = pm_rpm;
    char **argv;
    char *cmd;
    int i, n, nopts = 0;

    pm_rpm_setup_commands(pm);
    
    n = 32;
    argv = alloca((n + 1) * sizeof(*argv));
    argv[n] = NULL;
    n = 0;
    
    cmd = pm->rpm;
    argv[n++] = n_basenam(pm->rpm);
    argv[n++] = "-K";

    nopts = n;
    
    if ((flags & PKGVERIFY_GPG) == 0)
        argv[n++] = "--nogpg";

    if ((flags & PKGVERIFY_PGP) == 0)
        argv[n++] = "--nopgp";
    
    
    if ((flags & PKGVERIFY_MD) == 0) {
        argv[n++] = "--nomd5";
    }
    n_assert(n > nopts);        /* any PKGVERIFY_* given? */
    
    argv[n++] = (char*)path;
    nopts = n;
    argv[n++] = NULL;
    
    if (verbose > 1) {
        char buf[1024], *p;
        p = buf;
        
        for (i=0; i < nopts; i++) 
            p += n_snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", argv[i]);
        *p = '\0';
        msgn(1, _("Executing%s..."), buf);
    }

    return pm_rpm_execrpm(cmd, argv, 0, 4) == 0;
}

#endif /* HAVE_RPMCHECKSIG */

extern int pm_rpm_verbose;
int pm_rpm_verify_signature(void *pm_rpm, const char *path, unsigned flags) 
{
    int v, rv = pm_rpm_verbose, rc;
    
    pm_rpm_verbose = 1;
    v = poldek_set_verbose(pm_rpm_verbose);
    
    rc = do_pm_rpm_verify_signature(pm_rpm, path, flags);
    
    pm_rpm_verbose = rv;
    poldek_set_verbose(v);
    return rc;
}
