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

#ifndef HAVE_RPMPKGREAD          /* rpm 5.x */
# error "not rpm 5.x"
#endif

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>              /* rpm5 headers needs FILE */
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <rpm/rpmcb.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmio.h>
#include <rpm/rpmurl.h>
#include <rpm/rpmmacro.h>

#include <rpm/rpmts.h>
#include <rpm/rpmps.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmcli.h>
#include <rpm/pkgio.h>

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


static int rpm_signatures(const char *path, unsigned *signature_flags, FD_t *fd) 
{
    unsigned        flags;
    FD_t            fdt;
    Header          sign = NULL;
    HE_t            he;
    HeaderIterator  it;
    const char      *rpmsg;
    rpmRC           rc;

    *signature_flags = 0;
    
    fdt = Fopen(path, "r.ufdio");
    if (fdt == NULL || Ferror(fdt)) {
        if (fdt)
            Fclose(fdt);
        return 0;
    }
    
    rpmsg = NULL;
    rc = rpmpkgRead("Lead", fdt, NULL, &rpmsg);
    if (rc != RPMRC_OK) {
        logn(LOGERR, "%s: read lead failed (%s)", n_basenam(path), rpmsg);
        Fclose(fdt);
        return 0;
    }
    
    rc = rpmpkgRead("Signature", fdt, &sign, &rpmsg);
    if (rc != RPMRC_OK) {
        logn(LOGERR, "%s: read signature failed (%s)", n_basenam(path), rpmsg);
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
    it = headerInit(sign);
    he = memset(alloca(sizeof(*he)), 0, sizeof(*he));
    while (headerNext(it, he, 0)) {
        n_assert(he->p.ptr);
        free(he->p.ptr);
        
        switch (he->tag) {
            case RPMSIGTAG_RSA:
            case RPMSIGTAG_PGP5:	/* XXX legacy */
            case RPMSIGTAG_PGP:
                flags |= VRFYSIG_SIGNPGP;
                break;

            case RPMSIGTAG_DSA:
            case RPMSIGTAG_GPG:
                flags |= VRFYSIG_SIGNGPG;
                break;
                
            case RPMSIGTAG_LEMD5_2:
            case RPMSIGTAG_LEMD5_1:
            case RPMSIGTAG_MD5:
            case RPMSIGTAG_SHA1:
                flags |= VRFYSIG_DGST;
                break;
                
            default:
                continue;
                break;
        }
    }
    
    headerFini(it);
    headerFree(sign);
    *signature_flags = flags;
    return 1;
}

static int do_verify_signature(const char *path, unsigned flags) 
{
    unsigned                  presented_signs = 0;
    QVA_t                     qva;
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
    
    qva = memset(alloca(sizeof(*qva)), '\0', sizeof(*qva));
    qva->qva_flags = flags;

    rc = -1;
    fdt = Fopen(path, "r.ufdio");
    
    if (fdt != NULL && Ferror(fdt) == 0) {
        ts = rpmtsCreate();
        rc = rpmVerifySignatures(qva, ts, fdt, n_basenam(path));
        rpmtsFree(ts);
        
        DBGF("rpmVerifySignatures[md%d, sign%d] %s %s\n",
             flags & VRFYSIG_DGST ? 1:0, flags & VRFYSIG_SIGN ? 1:0, 
             n_basenam(path), rc == 0 ? "OK" : "BAD");
    }
    
    if (fdt)
        Fclose(fdt);
    
    return rc == 0;
}

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
