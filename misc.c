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

#ifndef HAVE_CANONICALIZE_FILE_NAME /* have safe GNU ext? */
# error "missing safe realpath()"
#endif

#ifdef HAVE_STRSIGNAL
# define _GNU_SOURCE 1  /* for strsignal */
#endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <argp.h>

#include <openssl/evp.h>

#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>

#include <vfile/p_open.h>
#include <vfile/vfile.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "misc.h"
#include "pkg.h"
#include "poldek_term.h"
#include "pkgmisc.h"

int bin2hex(char *hex, int hex_size, const unsigned char *bin, int bin_size)
{
    int i, n = 0, nn = 0;

    n_assert(hex_size > 2 * bin_size); /* with end '\0' */

    for (i=0; i < bin_size; i++) {
        n = n_snprintf(hex + nn, hex_size - nn, "%02x", bin[i]);
        nn += n;
        if (nn >= hex_size)
            break;
    }
    return nn;
}

static
int mdigest(FILE *stream, unsigned char *md, unsigned *md_size, int digest_type)
{
    unsigned char buf[8*1024];
    EVP_MD_CTX *ctx;
    unsigned n, nn = 0;

    n_assert(md_size && *md_size);
    ctx = EVP_MD_CTX_create();

    switch (digest_type) {
    case DIGEST_MD5:
        EVP_DigestInit(ctx, EVP_md5());
        break;

    case DIGEST_SHA1:
        EVP_DigestInit(ctx, EVP_sha1());
        break;

    case DIGEST_SHA256:
        EVP_DigestInit(ctx, EVP_sha256());
        break;

    default:
        n_die("%d: unknown digest type", digest_type);
    }

    while ((n = fread(buf, 1, sizeof(buf), stream)) > 0) {
        EVP_DigestUpdate(ctx, buf, n);
        nn += n;
    }

    EVP_DigestFinal(ctx, buf, &n);
    EVP_MD_CTX_destroy(ctx);

    if (n > *md_size) {
        *md = '\0';
        *md_size = 0;
    } else {
        memcpy(md, buf, n);
        *md_size = n;
    }

    return *md_size;
}

int mhexdigest(FILE *stream, char *mdhex, int *mdhex_size, int digest_type)
{
    unsigned char md[128];
    unsigned md_size = sizeof(md);


    if (mdigest(stream, md, &md_size, digest_type)) {
        int n = 0, nn = 0;

        for (unsigned i=0; i < md_size; i++) {
            n = n_snprintf(mdhex + nn, *mdhex_size - nn, "%02x", md[i]);
            nn += n;
        }
        *mdhex_size = nn;

    } else {
        *mdhex = '\0';
        *mdhex_size = 0;
    }

    return *mdhex_size;
}

static char *setup_default_cachedir(void)
{
    char *dir, path[PATH_MAX], inhome_path[PATH_MAX], *tmp_cachedir = NULL;
    const char *cachedn = ".poldek-cache";
    struct passwd *pw = NULL;
    const char *home = NULL;

    if ((pw = getpwuid(getuid()))) { /* use $HOME/.poldek-cache if exists */
        home = pw->pw_dir;
        if (poldek__is_in_testing_mode())
            home = getenv("HOME");

        if (home) {
            n_snprintf(inhome_path, sizeof(inhome_path), "%s/%s", home, cachedn);
            if (poldek_util_is_rwxdir(inhome_path)) {
                tmp_cachedir = inhome_path;
                goto l_end;
            }
        }
    }

    n_assert(tmp_cachedir == NULL);
    dir = getenv("XDG_CACHE_HOME");        /* try env XDG_CACHE_HOME */
    if ((dir == NULL || *dir == '\0') && home && *home) {
        int size = strlen(home) + 1 /* '/' */ + strlen(".cache") + 1;
        dir = alloca(size);
        n_snprintf(dir, size, "%s/.cache", home);
    }

    if (dir && *dir && poldek_util_is_rwxdir(dir)) {
        const char *dn = PACKAGE_NAME; /* XDG_CACHE_HOME/poldek */
        n_snprintf(path, sizeof(path), "%s/%s", dir, dn);

        if (!util__isdir(path))
            mkdir(path, 0700);

        if (poldek_util_is_rwxdir(path)) {
            tmp_cachedir = path;
            goto l_end;
        }
    }

    n_assert(tmp_cachedir == NULL);
    dir = getenv("TMPDIR");        /* try env $TMP* */
    if (dir == NULL || *dir == '\0')
        dir = getenv("TMP");
    if (dir && *dir && poldek_util_is_rwxdir(dir)) {
        const char *dn = cachedn + 1; /* in $TMP -> unhide */
        char suffix[32];

        if ((pw = getpwuid(getuid())) && pw->pw_name && *pw->pw_name)
            n_snprintf(suffix, sizeof(suffix), "%s", pw->pw_name);
        else
            n_snprintf(suffix, sizeof(suffix), "%.4ld", getuid());

        n_snprintf(path, sizeof(path), "%s/%s-%s", dir, dn, suffix);
        if (!util__isdir(path))
            mkdir(path, 0700);

        if (poldek_util_is_rwxdir(path)) {
            tmp_cachedir = path;
            goto l_end;
        }
    }
    n_assert(tmp_cachedir == NULL);

    if (pw) {                     /* try $HOME */
        char *d = pw->pw_dir;
        if (poldek__is_in_testing_mode())
            d = getenv("HOME");

        if (d && poldek_util_is_rwxdir(d) && util__mksubdir(d, cachedn)) {
            n_snprintf(path, sizeof(path), "%s/%s", d, cachedn);
            tmp_cachedir = path;
        }
    }

    if (tmp_cachedir == NULL)   /* weird */
        tmp_cachedir = "/tmp";

l_end:
    return n_strdup(tmp_cachedir);
}

char *util__setup_cachedir(const char *path)
{
    if (path == NULL)
        return setup_default_cachedir();

    if (!poldek_util_is_rwxdir(path)) {
        struct stat st;

        if (stat(path, &st) != 0 && errno == ENOENT)
            mkdir(path, 0755);
    }

    if (!poldek_util_is_rwxdir(path)) {
        logn(LOGERR, "%s: invalid cache directory (%m)", path);
        return NULL;
    }

    return n_strdup(path);
}


char *trimslash(char *path)
{
    if (path) {
        char *p = strchr(path, '\0');

        if (p) {
            p--;
            while (p != path && *p == '/') {
                *p = '\0';
                p--;
            }
        }
    }
    return path;
}

char *next_token(char **str, char delim, int *toklen)
{
    char *p, *token;

    if (*str == NULL)
        return NULL;


    if ((p = strchr(*str, delim)) == NULL) {
        token = *str;
        if (toklen)
            *toklen = strlen(*str);
        *str = NULL;

    } else {
        *p = '\0';

        if (toklen)
            *toklen = p - *str;
        p++;
        while(isspace(*p))
            p++;
        token = *str;
        *str = p;
    }

    return token;
}

int poldek_util_is_rwxdir(const char *path)
{
    struct stat st;

    errno = 0;
    if (stat(path, &st) == 0) {
        if (!S_ISDIR(st.st_mode) && errno == 0)
            errno = ENOENT;
        else if ((st.st_mode & S_IRWXU) != S_IRWXU)
            errno = EACCES;
    }

    return errno == 0;
}

int util__isdir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int util__mksubdir(const char *path, const char *dn)
{
    struct stat st;
    char fpath[PATH_MAX];

    if (!vf_valid_path(path))
        return 0;

    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        logn(LOGERR, _("%s: no such directory"), path);
        return 0;
    }

    if ((st.st_mode & S_IRWXU) != S_IRWXU) {
        logn(LOGERR, _("%s: mkdir: permission denied"), path);
        return 0;
    }

    snprintf(fpath, sizeof(fpath), "%s/%s", path, dn);
    if (!util__isdir(fpath)) {
        if (mkdir(fpath, 0755) != 0) {
            logn(LOGERR, "%s: mkdir: %m", fpath);
            return 0;
        }
    }

    return 1;
}


int util__mkdir_p(const char *path, const char *dn)
{
    const char **tl, **tl_save;
    char dpath[PATH_MAX];
    int n = 0, nerr = 0;


    tl = tl_save = n_str_tokl(dn, "/");
    n = n_snprintf(dpath, sizeof(dpath), "%s", path);
    while (*tl) {
        const char *d = *tl;
        tl++;
        if (*d == '\0')
            continue;

        n += n_snprintf(&dpath[n], sizeof(dpath) - n, "/%s", d);
        if (!util__isdir(dpath)) {
            if (mkdir(dpath, 0755) != 0) {
                logn(LOGERR, "%s: mkdir: %m", dpath);
                nerr++;
                break;
            }
        }
    }
    n_str_tokl_free(tl_save);
    return nerr == 0;
}

const char *poldek_util_ngettext_n_packages_fmt(int n)
{
#if ENABLE_NLS
    return ngettext("%d package",
                    "%d packages", n);
#else
    return "%d package(s)";
#endif
}


static char *get_env(char *dest, int size, const char *name)
{
    struct passwd *pw;
    char *val;

    if ((val = getenv(name)) != NULL) {
        if (*val)
            return val;
        else
            val = NULL;
    }

    if (strcmp(name, "HOME") == 0 && (pw = getpwuid(getuid()))) {
        snprintf(dest, size, "%s", pw->pw_dir);
        val = dest;
    }

    return val;
}

const char *poldek_util_expand_vars(char *dest, int size, const char *src,
                                    char varmark, tn_hash *varh, unsigned flags)
{
    const char **tl, **tl_save;
    tn_array *usedvars = NULL;
    char smark[16];
    int n = 0;

    if (varmark == '\0')
        varmark = '$';

    n_snprintf(smark, sizeof(smark), "%c", varmark);

    tl = tl_save = n_str_tokl(src, smark);
    if (*src != varmark && tl[1] == NULL) {
        n_str_tokl_free(tl);
        return src;
    }

    if (flags & POLDEK_UTIL_EXPANDVARS_RMUSED)
        usedvars = n_array_new(16, free, (tn_fn_cmp)strcmp);

    if (*src != varmark) {
        n = n_snprintf(dest, size, *tl);
        tl++;
    }

    while (*tl) {
        const char *p, *vv, *v, *var;
        char val[256], buf[PATH_MAX];
        int  v_len;


        p = vv = v = *tl;
        DBGF("token: %s\n", *tl);
        tl++;

        if (*vv == '{')
            v++;

        vv = v;

        v_len = 0;
        while (isalnum(*vv)) {
            vv++;
            v_len++;
        }

        if (*vv == '}')
            vv++;

        if (v_len + 1 > (int)sizeof(val))
            return src;

        n_snprintf(val, v_len + 1, v);
        DBGF("var %c{%s}\n", varmark, val);

        var = NULL;
        if (varh && varmark != '$') {
            var = n_hash_get(varh, val);
            if (usedvars)
                n_array_push(usedvars, n_strdup(val));

        } else if (varmark == '$') {
            var = get_env(buf, sizeof(buf), val);

        }

        if (var == NULL) {
            n += n_snprintf(&dest[n], size - n, "%c%s", varmark, p);

        } else {
            n += n_snprintf(&dest[n], size - n, "%s", var);
            n += n_snprintf(&dest[n], size - n, "%s", vv);
        }
    }

    n_str_tokl_free(tl_save);

    if (usedvars) {             /* RMUSED is requested */
        int i;

        n_array_sort(usedvars);
        n_array_uniq(usedvars);
        for (i=0; i < n_array_size(usedvars); i++)
            n_hash_remove(varh, n_array_nth(usedvars, i));
    }

    return dest;
}

time_t poldek_util_mtime(const char *path)
{
    struct stat st;

    if (stat(path, &st) != 0)
        return 0;

    return st.st_mtime;
}

int poldek_util_set_mtime(const char *path, time_t t)
{
    struct utimbuf ut;

    n_assert(t > 0);
    ut.actime = ut.modtime = t;
    return utime(path, &ut) == 0;
}

const char *poldek_util_expand_env_vars(char *dest, int size, const char *str)
{
    return poldek_util_expand_vars(dest, size, str, '$', NULL, 0);
}

/* NOTICE: caller must free result IF it's differ from argument */
char *util__abs_path(/* const */ char *path)
{
    char *endslash, *rpath = NULL;

    endslash = strrchr(path, '/');
    if (endslash && *(endslash + 1) != '\0')
        endslash = NULL;

    rpath = realpath(path, NULL);

    if (rpath && endslash) {
        int n = strlen(rpath);

        rpath = n_realloc(rpath, n + 2);
        rpath[n++] = '/';
        rpath[n] = '\0';
    }

    DBGF("%s -> %s\n", path, rpath);
    return rpath ? rpath : path;
}


int snprintf_size(char *buf, int bufsize, unsigned long nbytes,
                  int ndigits, int longunit)
{
    char unit[3], fmt[32];
    double nb;


    nb = nbytes;
    unit[0] = 'B';
    unit[1] = unit[2] = '\0';

    if (nb > 1024) {
        nb /= 1024.0;

        unit[0] = 'K';
        unit[1] = 'B';

        if (nb > 1024) {
            nb /= 1024;
            unit[0] = 'M';
        }
    }

    n_snprintf(fmt, sizeof(fmt), "%%.%df%%s", ndigits);
    if (!longunit)
        unit[1] = '\0';

    return n_snprintf(buf, bufsize, fmt, nb, unit);
}

const char *poldek_util_lc_lang(const char *category)
{
    const char *lang = NULL;

    if ((lang = getenv("LANGUAGE")) == NULL &&
        (lang = getenv("LC_ALL")) == NULL &&
        (lang = getenv(category)) == NULL &&
        (lang = getenv("LANG")) == NULL)
        lang = "C";

    else if (strcmp(lang, "POSIX") == 0)
        lang = "C";

    if (lang == NULL || *lang == '\0')
        lang = "C";

    return lang;
}

const char *lc_messages_lang(void)
{
    return poldek_util_lc_lang("LC_MESSAGES");
}

/*
 * cut_country_code:
 *
 * Usually lang looks like:
 *   ll[_CC][.EEEE][@dddd]
 * where:
 *   ll      ISO language code
 *   CC      (optional) ISO country code
 *   EE      (optional) encoding
 *   dd      (optional) dialect
 *
 * Returns: lang without country code (ll[.EEEE][@dddd]) or NULL when it's not
 *          present in lang string. Returned value must be released.
 */
static char *cut_country_code (const char *lang)
{
    char *c, *p, *q, *newlang;

    if ((q = strchr(lang, '_')) == NULL)
	return NULL;

    /* newlang is always shorter than lang */
    newlang = malloc(strlen(lang));

    p = n_strncpy(newlang, lang, q - lang + 1);

    if ((c = strchr(q, '.')))
	n_strncpy(p, c, strlen(c) + 1);
    else if ((c = strchr(q, '@')))
	n_strncpy(p, c, strlen(c) + 1);

    n_assert(strlen(lang) > strlen(newlang));

    return newlang;
}

static inline void n_array_push_check_dup(tn_array *langs, const char *value)
{
    int i;

    for (i = 0; i < n_array_size(langs); i++) {
	if (strcmp(n_array_nth(langs, i), value) == 0)
            return;
    }
    n_array_push(langs, n_strdup(value));
}

/*
 * lang_match_avlangs:
 *
 * Checks whether lang (or lang without country code) matches value from avlangs.
 * If so, it will be added to the r_langs and if it's equal to "C", has_C will
 * be set to 1.
 */
static inline void lang_match_avlangs(tn_array *avlangs, tn_array *r_langs,
                                      const char *lang, int *has_C)
{
    char lang_utf8[32];
    char *cut = NULL;

    /* first try */
    if (n_array_bsearch(avlangs, lang)) {
        if (strcmp(lang, "C") == 0)
            *has_C = 1;

	n_array_push_check_dup(r_langs, lang);
    }

    n_snprintf(lang_utf8, sizeof(lang_utf8), "%s.UTF-8", lang);
    if (n_array_bsearch(avlangs, lang_utf8))
	n_array_push_check_dup(r_langs, lang_utf8);

    /* second try, without country code */
    if ((cut = cut_country_code(lang))) {
        if (n_array_bsearch(avlangs, cut)) {
            if (strcmp(cut, "C") == 0)
                *has_C = 1;

	    n_array_push_check_dup(r_langs, cut);
        }

        n_snprintf(lang_utf8, sizeof(lang_utf8), "%s.UTF-8", cut);
	if (n_array_bsearch(avlangs, lang_utf8))
	    n_array_push_check_dup(r_langs, lang_utf8);

	free(cut);
    }
}

tn_array *lc_lang_select(tn_array *avlangs, const char *lc_lang)
{
    tn_array    *r_langs;
    const char  **langs, **p;
    int         has_C = 0;

    if (lc_lang == NULL || n_array_size(avlangs) == 0)
        return NULL;

    r_langs = n_array_new(n_array_size(avlangs), free, (tn_fn_cmp)strcmp);
    n_array_sort(avlangs);

    langs = n_str_tokl(lc_lang, ":");
    p = langs;

    while (*p) {
        char   *l, *q, *sep = "@.";
        int    len;

        /* try a complete match */
        lang_match_avlangs(avlangs, r_langs, *p, &has_C);

        len = strlen(*p) + 1;
        l = alloca(len + 1);
        memcpy(l, *p, len);

        while (*sep) {
            if ((q = strchr(l, *sep))) {
                *q = '\0';

                lang_match_avlangs(avlangs, r_langs, l, &has_C);
            }
            sep++;
        }

        p++;
    }

    n_str_tokl_free(langs);

    if (!has_C)
        n_array_push(r_langs, n_strdup("C"));

    if (n_array_size(r_langs) == 0) {
        n_array_free(r_langs);
        r_langs = NULL;
    }

    return r_langs;
}

static int gmt_off = 0; /* TZ offset */
static int gmt_off_flag = 0;

#include <time.h>
static void setup_gmt_off(void)
{
    time_t t;
    struct tm *tm;

    t = time(NULL);
    if ((tm = localtime(&t)))
#ifdef HAVE_TM_GMTOFF
        gmt_off = tm->tm_gmtoff;
#elif defined HAVE_TM___GMTOFF
        gmt_off = tm->__tm_gmtoff;
#endif
}

int poldek_util_get_gmt_offs(void)
{
    if (gmt_off_flag == 0) {
        setup_gmt_off();
        gmt_off_flag = 1;
    }
    return gmt_off;
}


int poldek_util_parse_bool(const char *v)
{
    if (strcasecmp(v, "yes") == 0 || strcasecmp(v, "y") == 0 ||
        strcasecmp(v, "1") == 0 ||
        strcasecmp(v, "true") == 0 || strcasecmp(v, "t") == 0 ||
        strcasecmp(v, "on") == 0 || strcasecmp(v, "enabled") == 0)
        return 1;

    if (strcasecmp(v, "no") == 0 || strcasecmp(v, "n") == 0 ||
        strcasecmp(v, "0") == 0 ||
        strcasecmp(v, "false") == 0 || strcasecmp(v, "f") == 0 ||
        strcasecmp(v, "off") == 0 || strcasecmp(v, "disabled") == 0)
        return 0;

    return -1;
}

int poldek_util_parse_bool3(const char *v)
{
    int rv = -1;

    if ((rv = poldek_util_parse_bool(v)) == -1) {
        if (strcasecmp(v, "auto") == 0 || strcasecmp(v, "a"))
            rv = 2;
    }

    return rv;
}

char *strtime_(time_t t)
{
    char buf[128];
    if (t)
        strftime(buf, sizeof(buf), "%H:%M:%S", gmtime(&t));
    else
        buf[0] = '\0';

    buf[sizeof(buf)-1] = '\0';
    return n_strdup(buf);
}

static int poldek_TESTING_MODE = -1;
int poldek__is_in_testing_mode(void)
{
    if (poldek_TESTING_MODE == -1) {
        poldek_TESTING_MODE = getenv("POLDEK_TESTING") == NULL ? 0 : 1;
    }

    return poldek_TESTING_MODE == 1;
}

static void process_output(struct p_open_st *st, int verbose_level)
{
    int endl = 1, nlines = 0;

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
            char  buf[4096], *fmt = "_%s";
            int   n, i;

            if ((n = read(st->fd, buf, sizeof(buf) - 1)) <= 0)
                break;

            buf[n] = '\0';
            if (buf[n - 1] == '\n') {
                buf[n - 1] = '\0'; /* deal with last_endlined -> move '\n' to fmt */
                fmt = "_%s\n";
            }

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
                    //if (nlines)
                    //    msg_f(0, "_\n");
                    nlines++;
                    msg_f(0, "cp: %c", c);
                    continue;
                }
                msg_f(0, "_%c", c);
            }
        }
    }

    return;
}

int poldek_util_copy_file(const char *src, const char *dst)
{
    struct p_open_st pst;
    unsigned p_open_flags = P_OPEN_KEEPSTDIN;
    char *argv[5];
    int n, ec;

    p_st_init(&pst);

    n = 0;
    argv[n++] = "cp";
//    argv[n++] = "-v";
    argv[n++] = (char*)src;
    argv[n++] = (char*)dst;
    argv[n++] = NULL;

    if (p_open(&pst, p_open_flags, "/bin/cp", argv) == NULL) {
        if (pst.errmsg) {
            logn(LOGERR, "%s", pst.errmsg);
            p_st_destroy(&pst);
        }
    }

    process_output(&pst, 1);
    if ((ec = p_close(&pst) != 0))
        logn(LOGERR, "cp %s: %s", src, pst.errmsg ? pst.errmsg : "copying failed");

    p_st_destroy(&pst);
    return ec == 0;
}

void *timethis_begin(void)
{
    struct timeval *tv;

    tv = n_malloc(sizeof(*tv));
    gettimeofday(tv, NULL);
    return tv;
}

void timethis_end(int verbose_level, void *tvp, const char *prefix)
{
    struct timeval tv, *tv0 = (struct timeval *)tvp;

    gettimeofday(&tv, NULL);

    tv.tv_sec -= tv0->tv_sec;
    tv.tv_usec -= tv0->tv_usec;
    if (tv.tv_usec < 0) {
        tv.tv_sec--;
        tv.tv_usec = 1000000 + tv.tv_usec;
    }
#ifdef WITH_TIMETHIS
    verbose_level = 1;
#endif

    msgn(verbose_level, "time [%s] %ld.%06ld\n", prefix, tv.tv_sec, tv.tv_usec);
    free(tvp);
}
