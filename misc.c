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
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <sys/wait.h>
#include <sys/utsname.h>
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
int mdigest(FILE *stream, unsigned char *md, int *md_size, int digest_type)
{
    unsigned char buf[8*1024];
    EVP_MD_CTX ctx;
    int n, nn = 0;


    n_assert(md_size && *md_size);

    if (digest_type == DIGEST_MD5) 
        EVP_DigestInit(&ctx, EVP_md5());
    else
        EVP_DigestInit(&ctx, EVP_sha1());

    while ((n = fread(buf, 1, sizeof(buf), stream)) > 0) {
        EVP_DigestUpdate(&ctx, buf, n);
        nn += n; 
    }
    
    EVP_DigestFinal(&ctx, buf, &n);

    if (n > *md_size) {
        *md = '\0';
        *md_size = 0;
    } else {
        memcpy(md, buf, n);
        *md_size = n;
    }
    
    return *md_size;
}

int mhexdigest(FILE *stream, unsigned char *mdhex, int *mdhex_size, int digest_type)
{
    unsigned char md[128];
    int  md_size = sizeof(md);

    
    if (mdigest(stream, md, &md_size, digest_type)) {
        int i, n = 0, nn = 0;
        
        for (i=0; i < md_size; i++) {
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

    if ((pw = getpwuid(getuid()))) { /* use $HOME/.poldek-cache if exists */
        char *d = pw->pw_dir;
        if (poldek__is_in_testing_mode())
            d = getenv("HOME");
        
        if (d) {
            n_snprintf(inhome_path, sizeof(inhome_path), "%s/%s", d, cachedn);
            if (poldek_util_is_rwxdir(inhome_path)) {
                tmp_cachedir = inhome_path;
                goto l_end;
            }
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

void packages_display_summary(int verbose_l, const char *prefix, tn_array *pkgs,
                              int parseable)
{
    int i, npkgs = n_array_size(pkgs);

    n_assert(pkgs);
    n_assert(n_array_size(pkgs) > 0);
    
    if (parseable) {
        for (i=0; i < n_array_size(pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgs, i);
            msgn(verbose_l, "%%%s %s", prefix, pkg_id(pkg));
        }
        
    } else {
        int ncol = 2, term_width, prefix_printed = 0;
        const char *p, *colon = ", ";
        tn_buf *nbuf = n_buf_new(512);
        
        term_width = poldek_term_get_width() - 5;
        ncol = strlen(prefix) + 1;

        for (i=0; i < n_array_size(pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgs, i);

            if (prefix_printed == 0) {
                n_buf_printf(nbuf, "%s ", prefix);
                prefix_printed = 1;
            }
            	
            p = pkg_id(pkg);
            if (ncol + (int)strlen(p) >= term_width) {
                msgn(verbose_l, "%s", (char*)n_buf_ptr(nbuf));
                
                n_buf_clean(nbuf);
                ncol = 3;
                n_buf_printf(nbuf, "%s ", prefix);
            }
        
            if (--npkgs == 0)
                colon = "";
            n_buf_printf(nbuf, "%s%s", p, colon);
            ncol += strlen(p) + strlen(colon);
        }

        if (prefix_printed)
            n_buf_printf(nbuf, "\n");

        if (n_buf_size(nbuf) > 0)
            msg(verbose_l, "%s", (char*)n_buf_ptr(nbuf));
        
        n_buf_free(nbuf);
    }
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
        snprintf(dest, size, pw->pw_dir);
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

const char *poldek_util_expand_env_vars(char *dest, int size, const char *str)
{
    return poldek_util_expand_vars(dest, size, str, '$', NULL, 0);
}

char *util__abs_path(const char *path) 
{
    char *endslash, *rpath = NULL;

    endslash = strrchr(path, '/');
    if (endslash && *(endslash + 1) != '\0')
        endslash = NULL;

    rpath = realpath(path, NULL);

    if (endslash) {
        int n = strlen(rpath);
        
        rpath = n_realloc(rpath, n + 2);
        rpath[n++] = '/';
        rpath[n] = '\0';
    }

    DBGF("%s -> %s\n", path, rpath);
    return rpath;
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

tn_array *lc_lang_select(tn_array *avlangs, const char *lc_lang)
{
    tn_array    *r_langs;
    const char  **langs, **p;
    int         has_C = 0;
    
    if (lc_lang == NULL || n_array_size(avlangs) == 0)
        return NULL;
    
    r_langs = n_array_clone(avlangs);
    n_array_sort(avlangs);
        
    langs = n_str_tokl(lc_lang, ":");
    p = langs;
    
    while (*p) {
        char   *l, *q, *sep = "@._";
        int    len;

        if (n_array_bsearch(avlangs, *p)) {
            if (strcmp(*p, "C") == 0)
                has_C = 1;
            n_array_push(r_langs, n_strdup(*p));
            p++;
            continue;
        }
        
        len = strlen(*p) + 1;
        l = alloca(len + 1);
        memcpy(l, *p, len);

        while (*sep) {
            if ((q = strchr(l, *sep))) {
                *q = '\0';
                
                if (n_array_bsearch(avlangs, l)) {
                    if (strcmp(*p, "C") == 0)
                        has_C = 1;
                    n_array_push(r_langs, n_strdup(l));
                    continue;
                }
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

int poldek__is_in_testing_mode(void) 
{
    return getenv("POLDEK_TESTING") != NULL;
}

