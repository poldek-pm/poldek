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
#include <rpm/rpmkeyring.h>

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

static int rpm_signatures(const char *path, unsigned *signature_flags)
{
    unsigned        flags;
    FD_t            fdt;
    Header          h = NULL;
    rpmts           ts;
    rpmKeyring      keyring;
    rpmRC           rc;

    *signature_flags = 0;

    fdt = Fopen(path, "r.ufdio");
    if (fdt == NULL || Ferror(fdt)) {
        if (fdt)
            Fclose(fdt);
        return 0;
    }

    /* read the headers only; an empty keyring keeps rpm from loading
       the system keyring */
    ts = rpmtsCreate();
    keyring = rpmKeyringNew();
    rpmtsSetKeyring(ts, keyring);
    rpmKeyringFree(keyring);
    rpmtsSetVSFlags(ts, RPMVSF_MASK_NODIGESTS | RPMVSF_MASK_NOSIGNATURES);
    rc = rpmReadPackageFile(ts, fdt, n_basenam(path), &h);
    rpmtsFree(ts);
    Fclose(fdt);

    if (rc != RPMRC_OK || h == NULL) {
        if (rc == RPMRC_NOTFOUND)   /* rpm says nothing about a bad lead */
            logn(LOGERR, "%s: not an rpm package", n_basenam(path));
        else
            logn(LOGERR, "%s: rpmReadPackageFile failed", n_basenam(path));
        headerFree(h);
        return 0;
    }

    /* rpm copies these from the signature header and refuses a package
       whose main header has them (CVE-2021-20271): they cannot be faked */
    flags = 0;
    if (headerIsEntry(h, RPMTAG_RSAHEADER))
        flags |= PKGVERIFY_PGP | PKGVERIFY_GPG;

    if (headerIsEntry(h, RPMTAG_SIGPGP))
        flags |= PKGVERIFY_PGP;

    if (headerIsEntry(h, RPMTAG_DSAHEADER) || headerIsEntry(h, RPMTAG_SIGGPG))
        flags |= PKGVERIFY_GPG;

    if (headerIsEntry(h, RPMTAG_SIGMD5))
        flags |= PKGVERIFY_MD;

    headerFree(h);
    *signature_flags = flags;
    return 1;
}

static int do_verify_signature(const char *path, const char *rootdir,
                               rpmKeyring keyring, unsigned flags)
{
    unsigned                  presented_signs = 0;
    struct rpmQVKArguments_s  qva; /* poor RPM API... */
    rpmts                     ts;
    FD_t                      fdt = NULL;
    int                       rc;


    n_assert(flags & (PKGVERIFY_MD | PKGVERIFY_GPG | PKGVERIFY_PGP));

    if (!rpm_signatures(path, &presented_signs))
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
        if (n > 0)
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
        if (rootdir && *rootdir)
            rpmtsSetRootDir(ts, rootdir);
        if (keyring && rpmtsSetKeyring(ts, keyring) != 0) {
            rpmtsFree(ts);
            Fclose(fdt);
            return 0;
        }
        rpmtsSetVfyFlags(ts, vfyflags);
        int vfylevel = rpmtsVfyLevel(ts);
        if ((flags & (PKGVERIFY_PGP | PKGVERIFY_GPG)) == 0)
            vfylevel &= ~RPMSIG_SIGNATURE_TYPE;
        else    /* the default %_pkgverify_level accepts unsigned packages */
            vfylevel |= RPMSIG_SIGNATURE_TYPE;
        rpmtsSetVfyLevel(ts, vfylevel);
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

extern int pm_rpm_verbose;

static int verify_signature(void *pm_rpm, const char *rootdir,
                            rpmKeyring keyring, const char *path,
                            unsigned flags)
{
    int v, rv = pm_rpm_verbose, rc;

    (void)pm_rpm;
    pm_rpm_verbose = 1;
    v = poldek_set_verbose(pm_rpm_verbose);

    if (access(path, R_OK) != 0) {
        logn(LOGERR, "%s: verify signature failed: %m", path);
        rc = 0;
    } else {
        rc = do_verify_signature(path, rootdir, keyring, flags);
    }

    pm_rpm_verbose = rv;
    poldek_set_verbose(v);
    return rc;
}

/*
  pm_rpm_verify_signature() has no transaction context and therefore uses
  the default rpmdb.  During an install into --root the package must be
  verified against the target root's rpmdb/keyring, so pass ts->rootdir
  down to rpmtsSetRootDir().
*/
int pm_rpm_verify_signature_ts(void *pm_rpm, const struct poldek_ts *ts,
                               const char *path, unsigned flags)
{
    n_assert(ts);
    return verify_signature(pm_rpm, ts->rootdir, NULL, path, flags);
}

int pm_rpm_verify_signature_keyring(void *pm_rpm, const struct poldek_ts *ts,
                                    const struct signkey *sk, const char *path,
                                    unsigned flags)
{
    n_assert(ts);
    n_assert(sk);
    return verify_signature(pm_rpm, ts->rootdir,
                            pm_rpm_signkey_keyring(sk),
                            path, flags);
}

int pm_rpm_verify_signature(void *pm_rpm, const char *path, unsigned flags)
{
    return verify_signature(pm_rpm, NULL, NULL, path, flags);
}
