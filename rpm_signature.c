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
#include <rpm/rpmurl.h>
#include <rpm/rpmmacro.h>
#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>

#ifdef HAVE_RPM_4_1
# include <rpm/rpmts.h>
# include <rpm/rpmps.h>
# include <rpm/rpmdb.h>
# include <rpm/rpmcli.h>
#endif

#include "i18n.h"
#include "rpm.h"
#include "rpmadds.h"
#include "depdirs.h"
#include "misc.h"
#include "log.h"
#include "pkg.h"
#include "dbpkg.h"
#include "capreq.h"
#include "rpmdb_it.h"



static int rpm_read_signature(FD_t fd, Header *sighp, int sig_type)
{
#ifdef HAVE_RPM_4_1
    return rpmReadSignature(fd, sighp, sig_type) == 0;
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
        logn(LOGERR, "%s: read package lead failed", path);
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
# error "dupa"
#endif
int rpm_verify_signature(const char *path, unsigned flags) 
{
    const char *argv[2];
    unsigned presented_signs;

    n_assert(flags & (CHECKSIG_MD5 | CHECKSIG_GPG | CHECKSIG_PGP));

    if ((flags & (CHECKSIG_GPG | CHECKSIG_PGP))) {
        presented_signs = 0;
        
        if (!rpm_signatures(path, &presented_signs, NULL)) {
            logn(LOGERR, "dupa\n");
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

#else

int rpm_verify_signature(const char *path, unsigned flags) 
{
    const char                *argv[2];
    unsigned                  presented_signs = 0;
    struct rpmQVKArguments_s  qva; /* poor RPM API... */
    rpmts                     ts;
    FD_t                      fdt = NULL;
    int                       rc;

    
    n_assert(flags & (VRFYSIG_DGST |
                      VRFYSIG_SIGNGPG | VRFYSIG_SIGNPGP));
    
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
        
        DBGF("rpmVerifySignatures %s %s\n", n_basenam(path), rc == 0 ? "OK" : "BAD");
    }
    
    if (fdt)
        Fclose(fdt);
    
    return rc == 0;
}

#endif
