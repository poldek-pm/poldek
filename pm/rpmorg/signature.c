/*
  Copyright (C) 2000 - 2019 Pawel A. Gajda <mis@pld-linux.org>

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

#include <rpm/rpmlog.h>

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

/* missing rpm prototypes */
extern rpmRC rpmLeadRead(FD_t fd, char **emsg);
extern rpmRC rpmReadSignature(FD_t fd, Header *sighp, char ** msg);

static int rpm_signatures(const char *path, unsigned *signature_flags, FD_t *fd)
{
    unsigned        flags;
    FD_t            fdt;
    Header          sign = NULL;
    HeaderIterator  it;
    char            *msg = NULL;
    struct rpmtd_s td;

    *signature_flags = 0;

    fdt = Fopen(path, "r.ufdio");
    if (fdt == NULL || Ferror(fdt)) {
        if (fdt)
            Fclose(fdt);
        return 0;
    }

    if (rpmLeadRead(fdt, &msg) != RPMRC_OK) {
        logn(LOGERR, "%s: %s", n_basenam(path), msg);
        if (msg)
            free(msg);
        return 0;
    }

    if (rpmReadSignature(fdt, &sign, &msg) != RPMRC_OK) {
        logn(LOGERR, "%s: rpmReadSignature failed: %s", n_basenam(path), msg);
        if (msg)
            free(msg);
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

    while (headerNext(it, &td)) {
        if (rpmtdCount(&td) == 0) {
            continue;
        }

        switch (td.tag) {
            case RPMSIGTAG_RSA:
            case RPMSIGTAG_PGP5:	/* XXX legacy */
            case RPMSIGTAG_PGP:
                flags |= PKGVERIFY_PGP;
                break;

            case RPMSIGTAG_DSA:
            case RPMSIGTAG_GPG:
                flags |= PKGVERIFY_GPG;
                break;

            case RPMSIGTAG_LEMD5_2:
            case RPMSIGTAG_LEMD5_1:
            case RPMSIGTAG_MD5:
                flags |= PKGVERIFY_MD;
                break;

            default:
                continue;
                break;
        }
        rpmtdFreeData(&td);
    }

    headerFreeIterator(it);
    headerFree(sign);
    *signature_flags = flags;
    return 1;
}

static int do_verify_signature(const char *path, unsigned flags)
{
    unsigned                  presented_signs = 0;
    struct rpmQVKArguments_s  qva; /* poor RPM API... */
    rpmts                     ts;
    FD_t                      fdt = NULL;
    int                       rc;


    n_assert(flags & (PKGVERIFY_MD | PKGVERIFY_GPG | PKGVERIFY_PGP));

    if (!rpm_signatures(path, &presented_signs, NULL))
        return 0;

    if ((presented_signs & flags) == 0) {
        char signam[255];
        int n = 0;

        if (flags & PKGVERIFY_MD)
            n += n_snprintf(&signam[n], sizeof(signam) - n, "digest/");

        if (flags & PKGVERIFY_GPG)
            n += n_snprintf(&signam[n], sizeof(signam) - n, "gpg/");

        if (flags & PKGVERIFY_PGP)
            n += n_snprintf(&signam[n], sizeof(signam) - n, "pgp/");

        n_assert(n > 0);
        signam[n - 1] = '\0';   /* eat last '/' */
        logn(LOGWARN, _("%s: %s signature not found"), n_basenam(path),
             signam);
        return 0;
    }
    unsigned vfyflags = RPMVSF_DEFAULT;

    if ((flags & (PKGVERIFY_PGP | PKGVERIFY_GPG)) == 0) {
        vfyflags |= RPMVSF_MASK_NOSIGNATURES;
    }

    // always check digests - without them rpmVerifySignature returns error
    //if ((flags & PKGVERIFY_MD) == 0)
    //   vfyflags |= RPMVSF_MASK_NODIGESTS;

    rc = -1;
    fdt = Fopen(path, "r.ufdio");

    if (fdt != NULL && Ferror(fdt) == 0) {
        ts = rpmtsCreate();
        rpmtsSetVfyFlags(ts, vfyflags);
        rc = rpmVerifySignatures(&qva, ts, fdt, n_basenam(path));
        rpmtsFree(ts);

        DBGF("rpmVerifySignatures[md=%d, sign=%d] %s %s\n",
             flags & PKGVERIFY_MD ? 1:0, flags & (PKGVERIFY_GPG | PKGVERIFY_PGP) ? 1:0,
             n_basenam(path), rc == 0 ? "OK" : "BAD");
    }

    if (fdt)
        Fclose(fdt);

    return rc == 0;
}

static
int do_pm_rpm_verify_signature(void *pm_rpm, const char *path, unsigned flags)
{
    if (access(path, R_OK) != 0) {
        logn(LOGERR, "%s: verify signature failed: %m", path);
        return 0;
    }

    return do_verify_signature(path, flags);
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
