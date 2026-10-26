/*
  Copyright (C) 2000 - 2026 Pawel A. Gajda <mis@pld-linux.org>

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

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <rpm/rpmkeyring.h>
#include <rpm/rpmpgp.h>
#include <rpm/rpmts.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>

#include <vfile/vfile.h>

#include "i18n.h"
#include "log.h"
#include "poldek_intern.h"
#include "pm_rpm.h"

#define SIGNKEY_ARMOR    "-----BEGIN PGP PUBLIC KEY BLOCK-----"
/* public key files are small; keep an untrusted st_size from causing a
   huge allocation */
#define SIGNKEY_MAX_SIZE (100 * 1024)

struct signkey {
    char *url;
    char *path;
    tn_array *keys;
    rpmKeyring keyring;
    int import_status;
};

static void signkey_key_free(void *key)
{
    rpmPubkeyFree((rpmPubkey)key);
}

static int signkey_add_packets(tn_array *keys, const uint8_t *pkts,
                               size_t pktslen)
{
    while (pktslen > 0) {
        size_t certlen = 0;
        rpmPubkey key;

        if (pgpPubKeyCertLen(pkts, pktslen, &certlen) != 0 || certlen == 0)
            return 0;

        key = rpmPubkeyNew(pkts, certlen);
        if (key == NULL)
            return 0;

        if (n_array_push(keys, key) == NULL) {
            rpmPubkeyFree(key);
            return 0;
        }

        pkts += certlen;
        pktslen -= certlen;
    }

    return 1;
}

static int signkey_read_file(const char *path, char **bufp, size_t *lenp)
{
    struct stat st;
    char *buf;
    size_t len = 0;
    ssize_t n;
    int fd;

    if ((fd = open(path, O_RDONLY)) == -1)
        return 0;

    if (fstat(fd, &st) != 0 ||
        st.st_size <= 0 || st.st_size > SIGNKEY_MAX_SIZE) {
        close(fd);
        return 0;
    }

    buf = n_malloc((size_t)st.st_size + 1);
    if (buf == NULL) {
        close(fd);
        return 0;
    }

    while (len < (size_t)st.st_size) {
        n = read(fd, buf + len, (size_t)st.st_size - len);
        if (n <= 0) {
            n_free(buf);
            close(fd);
            return 0;
        }
        len += n;
    }
    close(fd);
    buf[len] = '\0';

    *bufp = buf;
    *lenp = len;
    return 1;
}

static tn_array *signkey_parse(const char *path)
{
    tn_array *keys;
    const char *p;
    char *buf = NULL;
    size_t len = 0;
    int parsed = 0;

    if (!signkey_read_file(path, &buf, &len)) {
        logn(LOGWARN, _("%s: cannot read signing key"), path);
        return NULL;
    }

    keys = n_array_new(4, signkey_key_free, NULL);

    p = buf;
    while ((p = strstr(p, "-----BEGIN PGP ")) != NULL) {
        uint8_t *pkts = NULL;
        size_t pktslen = 0;

        if (strncmp(p, SIGNKEY_ARMOR, strlen(SIGNKEY_ARMOR)) != 0) {
            p++;
            continue;
        }

        if (pgpParsePkts(p, &pkts, &pktslen) != PGPARMOR_PUBKEY) {
            logn(LOGWARN, _("%s: signing key parse error"), path);
            free(pkts);
            n_array_free(keys);
            n_free(buf);
            return NULL;
        }

        if (!signkey_add_packets(keys, pkts, pktslen)) {
            logn(LOGWARN, _("%s: signing key parse error"), path);
            free(pkts);
            n_array_free(keys);
            n_free(buf);
            return NULL;
        }

        parsed = 1;
        free(pkts);
        p += strlen(SIGNKEY_ARMOR);
    }

    if (!parsed && !signkey_add_packets(keys, (const uint8_t *)buf, len)) {
        logn(LOGWARN, _("%s: signing key parse error"), path);
        n_array_free(keys);
        n_free(buf);
        return NULL;
    }

    n_free(buf);
    if (n_array_size(keys) == 0) {
        logn(LOGWARN, _("%s: no public key found in signing key file"), path);
        n_array_free(keys);
        return NULL;
    }

    return keys;
}

/*
  Build a keyring containing only the keys configured for one source.
  This keyring is used for the extra per-source signature check, not for
  the global rpmdb.
*/
static rpmKeyring signkey_make_keyring(const tn_array *keys,
                                       const char *signkey_url)
{
    rpmKeyring keyring;
    int i;

    keyring = rpmKeyringNew();
    if (keyring == NULL) {
        logn(LOGERR, _("%s: cannot create signing keyring"), signkey_url);
        return NULL;
    }

    for (i = 0; i < n_array_size(keys); i++) {
        rpmPubkey key = n_array_nth(keys, i);

        if (rpmKeyringAddKey(keyring, key) < 0) {
            logn(LOGERR, _("%s: cannot add key to signing keyring"),
                 signkey_url);
            rpmKeyringFree(keyring);
            return NULL;
        }
    }

    return keyring;
}

/*
  Ask the rpm keyring of the transaction's target root whether all keys
  from the file are already present.  This is a read-only operation and
  does not need root privileges.
*/
static int signkey_present(const struct poldek_ts *ts, const tn_array *keys)
{
    const char *rootdir = ts->rootdir ? ts->rootdir : "/";
    rpmVSFlags vflags;
    rpmKeyring keyring;
    rpmts rts;
    int i, rc = 1;

    rts = rpmtsCreate();
    if (rts == NULL)
        return 0;

    if (rpmtsSetRootDir(rts, rootdir) != 0) {
        rpmtsFree(rts);
        return 0;
    }

    /* rpmtsGetKeyring() does not load the keyring when signature checking
       is disabled; force it for this read-only query and restore the flags. */
    vflags = rpmtsVSFlags(rts);
    rpmtsSetVSFlags(rts, vflags & ~RPMVSF_MASK_NOSIGNATURES);
    keyring = rpmtsGetKeyring(rts, 1);
    rpmtsSetVSFlags(rts, vflags);

    if (keyring == NULL) {
        rpmtsFree(rts);
        return 0;
    }

    for (i = 0; i < n_array_size(keys); i++) {
        rpmPubkey found;

        found = rpmKeyringLookupKey(keyring, n_array_nth(keys, i));
        if (found == NULL) {
            rc = 0;
            break;
        }
        rpmPubkeyFree(found);
    }

    rpmKeyringFree(keyring);
    rpmtsFree(rts);
    return rc;
}

static void signkey_confirm_message(char *buf, size_t size,
                                    const tn_array *keys, const char *url)
{
    int i, n = 0;

    n += n_snprintf(buf + n, size - n,
                    _("Importing OpenPGP key(s) from %s:\n"), vf_url_slim_s(url, 0));

    for (i = 0; i < n_array_size(keys) && n < (int)size - 1; i++) {
        rpmPubkey key = n_array_nth(keys, i);
        const char *keyid = rpmPubkeyKeyIDAsHex(key);
        const char *fp = rpmPubkeyFingerprintAsHex(key);
        pgpDigParams params = rpmPubkeyPgpDigParams(key);
        const char *uid = params ? pgpDigParamsUserID(params) : NULL;

        n += n_snprintf(buf + n, size - n, _(" id  0x%s\n"), keyid ? keyid : _("unknown"));
        if (fp)
            n += n_snprintf(buf + n, size - n, _(" fp  %s\n"), fp);
        if (uid)
            n += n_snprintf(buf + n, size - n, _(" uid %s\n"), uid);

        n += n_snprintf(buf + n, size - n, "\n");
    }

    n_snprintf(buf + n, size - n, _("Import into rpm keyring?"));
}

struct signkey *pm_rpm_signkey_load(const char *signkey_url)
{
    struct signkey *sk;
    struct vfile *vf;
    char *path;
    tn_array *keys;
    rpmKeyring keyring;

    n_assert(signkey_url);

    vf = vfile_open_ul(signkey_url, VFT_IO, VFM_RO | VFM_CACHE, "signkey");
    if (vf == NULL) {
        logn(LOGWARN, _("%s: signing key fetch failed"), signkey_url);
        return NULL;
    }

    path = n_strdup(vfile_localpath(vf));
    keys = signkey_parse(path);
    vfile_close(vf);
    if (keys == NULL) {
        n_free(path);
        return NULL;
    }

    keyring = signkey_make_keyring(keys, signkey_url);
    if (keyring == NULL) {
        n_array_free(keys);
        n_free(path);
        return NULL;
    }

    sk = n_malloc(sizeof(*sk));
    sk->url = n_strdup(signkey_url);
    sk->path = path;
    sk->keys = keys;
    sk->keyring = keyring;
    sk->import_status = SIGNKEY_IMPORT_NOT_ATTEMPTED;
    return sk;
}

void pm_rpm_signkey_free(struct signkey *sk)
{
    if (sk == NULL)
        return;

    n_cfree(&sk->url);
    n_cfree(&sk->path);
    if (sk->keys)
        n_array_free(sk->keys);
    if (sk->keyring)
        rpmKeyringFree(sk->keyring);
    n_free(sk);
}

rpmKeyring pm_rpm_signkey_keyring(const struct signkey *sk)
{
    return sk->keyring;
}

int pm_rpm_signkey_import_status(const struct signkey *sk)
{
    return sk->import_status;
}

/*
  Make sure a source's signing key is in rpm's keyring.

  Parsing and the source keyring were done by pm_rpm_signkey_load(); this
  function only checks the target rpmdb and optionally imports the key.
*/
int pm_rpm_signkey_import(struct pm_rpm *pm, const struct poldek_ts *ts,
                          struct signkey *sk)
{
    char *cmd;
    char *argv[8];
    int   nargs = 0, ec;

    n_assert(pm);
    n_assert(ts);
    n_assert(sk);

    if (signkey_present(ts, sk->keys)) {
        sk->import_status = SIGNKEY_ALREADY_PRESENT;
        msgn(2, _("%s: signing key already imported"), sk->url);
        return 1;
    }

    if (poldek_ts_is_interactive_on(ts)) {
        char msg[4096];

        signkey_confirm_message(msg, sizeof(msg), sk->keys, sk->url);
        if (!poldek__confirm(ts, 0, msg)) {
            sk->import_status = SIGNKEY_IMPORT_DECLINED;
            return 0;
        }
    }

    pm_rpm_setup_commands(pm);
    if (pm->rpm == NULL) {
        sk->import_status = SIGNKEY_IMPORT_FAILED;
        logn(LOGERR, _("%s: command not found"), "rpm");
        return 0;
    }

    if ((cmd = pm_rpm_command(pm, ts, argv, &nargs)) == NULL) {
        sk->import_status = SIGNKEY_IMPORT_FAILED;
        return 0;
    }

    if (ts->rootdir) {
        argv[nargs++] = "--root";
        argv[nargs++] = (char *)ts->rootdir;
    }

    argv[nargs++] = "--import";
    argv[nargs++] = sk->path;
    argv[nargs] = NULL;

    ec = pm_rpm_execrpm(cmd, argv, 1, 1);
    if (ec != 0) {
        sk->import_status = SIGNKEY_IMPORT_FAILED;
        logn(LOGWARN, _("%s: signing key import failed"), sk->url);
        return 0;
    }

    if (!signkey_present(ts, sk->keys)) {
        sk->import_status = SIGNKEY_IMPORT_FAILED;
        logn(LOGWARN, _("%s: imported signing key not found in rpmdb"),
             sk->url);
        return 0;
    }

    sk->import_status = SIGNKEY_IMPORTED;
    msgn(1, _("%s: signing key imported"), sk->url);
    return 1;
}
