/*
  Copyright (C) 2000 - 2005 Pawel A. Gajda <mis@k2.net.pl>

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
//static
//int valid_dir(const char *envname, const char *dir);

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


char *setup_cachedir(const char *path) 
{
    struct passwd *pw;
    char *dir, *default_dn = ".poldek-cache";

    if (path) {
        if (vf_valid_path(path) && poldek_util_is_rwxdir(path)) 
            return n_strdup(path);
        else 
            logn(LOGWARN, _("%s: invalid cachedir path, "
                            "fallback to default"), path);
    }
    
    if ((dir = getenv("TMPDIR")) && vf_valid_path(dir))
        return n_strdup(dir);
    
    if ((pw = getpwuid(getuid())) == NULL)
        return n_strdup(tmpdir());

    if (!poldek_util_is_rwxdir(pw->pw_dir))
        return n_strdup(tmpdir());
    
    if (vf_valid_path(pw->pw_dir) && mk_dir(pw->pw_dir, default_dn)) {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", pw->pw_dir, default_dn);
        return n_strdup(path);
    }

    return n_strdup(tmpdir());
}

const char *tmpdir(void) 
{
    struct stat st;
    static char *tmpdir = NULL;
    char *dir;

    if (tmpdir != NULL)
        return tmpdir;
    
    if ((dir = getenv("TMPDIR")) == NULL)
        tmpdir = "/tmp";
    
    else if (*dir != '/' || strlen(dir) < 4)
        dir = "/tmp";
    
    else {
        char *p;
            
        p = dir + 1;
        while (*p) {
            if (!isalnum(*p) && *p != '/' && *p != '-') {
                tmpdir = "/tmp";
                logn(LOGWARN,
                     _("$TMPDIR (%s) contains non alnum characters, "
                       "using /tmp"), dir);
                break;
            }
            p++;
        }

        if (tmpdir == NULL) {
            if (stat(dir, &st) != 0) {
                logn(LOGERR, _("$TMPDIR (%s): %m, using /tmp"), dir);
                tmpdir = "/tmp";
                
            } else if (!S_ISDIR(st.st_mode)) {
                logn(LOGERR, _("$TMPDIR (%s): not a directory, "
                               "using /tmp"), dir);
                tmpdir = "/tmp";
            }
        }
    }

    if (tmpdir == NULL)
        tmpdir = dir;

    return tmpdir;
}

#if 0                           /* not used */
static
int valid_dir(const char *envname, const char *dir) 
{
    struct stat st;
    const char *p;
    int rc = 1;

    
    p = dir + 1;
    while (*p) {
        if (!isalnum(*p) && *p != '/' && *p != '-') {
            logn(LOGWARN,
                 _("%s (%s) contains non alphanumeric characters"),
                 envname, dir);
            rc = 0;
            break;
        }
        p++;
    }
    
    if (rc) {
        rc = 0;
        if (stat(dir, &st) != 0)
            logn(LOGERR, _("%s (%s): %m, using /tmp"), envname, dir);
            
        else if (!S_ISDIR(st.st_mode))
            logn(LOGERR, _("%s (%s): not a directory"), envname, dir);
            
        else if ((st.st_mode & S_IRWXU) != S_IRWXU)
            logn(LOGERR, _("%s (%s): permission denied"), envname, dir);
            
        else 
            rc = 1;
    }

    return rc;
}
#endif

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

int is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}


void die(void) 
{
    printf("Something wrong, something not quite right, die\n");
    abort();
}

void process_cmd_output(struct p_open_st *st, const char *prefix) 
{
    int c, endl = 1, cnt = 0;
    
    if (prefix == NULL)
        prefix = st->cmd;

    setvbuf(st->stream, NULL, _IONBF, 0);
    while ((c = fgetc(st->stream)) != EOF) {
        
        if (endl) {
            msg(1, "_%s: ", prefix);
            endl = 0;
        }

        msg(1, "_%c", c);
        if (c == '\n' && cnt > 0)
            endl = 1;
        
        cnt++;
    }
}

int lockfile(const char *lockfile) 
{
    struct flock fl;
    int    fd;
    
    
    if ((fd = open(lockfile, O_RDWR | O_CREAT, 0644)) < 0) {
        logn(LOGERR, "open %s: %m", lockfile);
        return -1;
    }

    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    
    if (fcntl(fd, F_SETLK, &fl) == -1) {
        if (errno == EAGAIN || errno == EACCES)
            fd = 0;
        else
            logn(LOGERR, "fcntl %s: %m", lockfile);
        
    } else {
        char buf[64];
        
        ftruncate(fd, 0);
        snprintf(buf, sizeof(buf), "%d", getpid());
        write(fd, buf, strlen(buf));
    }
    
    return fd;
}

pid_t readlockfile(const char *lockfile) 
{
    char buf[256];
    int fd, nread;
    pid_t pid;
    
    fd = open(lockfile, O_RDONLY, 0444);
    if(fd < 0) 
        return -1;
    
    nread = read(fd, buf, sizeof(buf));
    close(fd);

    if (sscanf(buf, "%d", &pid) == 1)
        return pid;
    
    return -1;
}


int mk_dir(const char *path, const char *dn) 
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
    if (!is_dir(fpath)) {
        if (mkdir(fpath, 0755) != 0) {
            logn(LOGERR, "%s: mkdir: %m", fpath);
            return 0;
        }
    }
    
    return 1;
}


int mk_dir_parents(const char *path, const char *dn) 
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
        if (!is_dir(dpath)) {
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


int mklock(const char *dir) 
{
    char path[PATH_MAX];
    int rc;
    
    snprintf(path, sizeof(path), "%s/poldek..lck", dir);

    rc = lockfile(path);
    
    if (rc == 0) {
        char buf[64];
        pid_t pid = readlockfile(path);
        
        if (pid > 0) 
            snprintf(buf, sizeof(buf), " (%d)", pid);
        else
            *buf = '\0';
            
        logn(LOGERR, _("There seems another poldek%s uses %s"), buf, dir);
    }

    return rc > 0; 
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


void packages_iinf_display(int verbose_l, const char *prefix, tn_array *pkgs,
                           struct pkgmark_set *pms, unsigned pmsflags, int simple)
{
    int   i, npkgs = 0;
    
    npkgs =  n_array_size(pkgs);
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        DBGF("%s %d %d\n", pkg_id(pkg), flags,
             pms ? pkgmark_isset(pms, pkg, pmsflags) : -1);
        if (pmsflags && pms && !pkgmark_isset(pms, pkg, pmsflags))
            npkgs--;
    }

    if (simple) {
        for (i=0; i < n_array_size(pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgs, i);
            
            if (pmsflags && pms && !pkgmark_isset(pms, pkg, pmsflags))
                continue;
            msgn(verbose_l, "%%%s %s", prefix, pkg_id(pkg));
        }
        
    } else {
        int ncol = 2, term_width, hdr_printed = 0;
        const char *p, *colon = ", ";

        term_width = poldek_term_get_width() - 5;
        ncol = strlen(prefix) + 1;

        for (i=0; i < n_array_size(pkgs); i++) {
            struct pkg *pkg = n_array_nth(pkgs, i);

            if (pmsflags && pms && !pkgmark_isset(pms, pkg, pmsflags))
                continue;

            if (hdr_printed == 0) {
                msg(verbose_l, "%s ", prefix);
                hdr_printed = 1;
            }
            	
            p = pkg_id(pkg);
            if (ncol + (int)strlen(p) >= term_width) {
                ncol = 3;
                msg(verbose_l, "_\n%s ", prefix);
            }
        
            if (--npkgs == 0)
                colon = "";
            
            msg(verbose_l, "_%s%s", p, colon);
            ncol += strlen(p) + strlen(colon);
        }

        if (hdr_printed)
            msg(verbose_l, "_\n");
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



const char *poldek_util_expand_env_vars(char *dest, int size, const char *str)
{
    return poldek_util_expand_vars(dest, size, str, '$', NULL, 0);
}

const char *abs_path(char *buf, int size, const char *path) 
{
    
    if (*path == '/')
        return path;

    if (getcwd(buf, size) == NULL)
        return path;
    
    if (strcmp(path, ".") != 0) {
        int n = strlen(buf);
        n = snprintf(&buf[n], size - n, "/%s", path);
        if (n < (int)strlen(path) + 1)
            return path;
    }

    return buf;
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
