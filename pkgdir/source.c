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

#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <trurl/nmalloc.h>
#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>
#include <trurl/nhash.h>

#include <vfile/vfile.h>

#define ENABLE_TRACE 0

#define PKGDIR_INTERNAL
#include "pkgdir.h"
#include "source.h"
#include "misc.h"
#include "log.h"
#include "poldek_term.h"
#include "i18n.h"

#define SOURCE_DEFAULT_PRI 0

const char *pkgdir_default_type = "pndir";

struct subopt {
    char      *name;
    unsigned  flag;              /* MUST BE non-zero */
    char      *val;
    int       isdefault;         /* is default value */
};

struct src_option {
    char      *name;
    int       len;
    unsigned  flag;
    struct subopt *subopts;
};

#define PKGSRC_OPTION_SUBOPT  (1 << 15)
#define PKGSRC_OPTION_STRING  (1 << 16)
#define PKGSRC_OPTION_OPTMASK (PKGSRC_OPTION_SUBOPT | PKGSRC_OPTION_STRING)

static struct src_option source_options[] = {
    { "noauto",   0, PKGSOURCE_NOAUTO,      NULL}, 
    { "noautoup", 0, PKGSOURCE_NOAUTOUP,    NULL}, 
    { "gpg",      0, PKGSOURCE_VRFY_GPG,    NULL},
    { "pgp",      0, PKGSOURCE_VRFY_PGP,    NULL},
    { "sign",     0, PKGSOURCE_VRFY_SIGN,   NULL},
    { "type",     0, PKGSOURCE_TYPE |
                     PKGSRC_OPTION_STRING | PKGSRC_OPTION_SUBOPT, NULL },
    { "dscr",     0, PKGSOURCE_DSCR |
                     PKGSRC_OPTION_STRING | PKGSRC_OPTION_SUBOPT, NULL },
    { "pri",      0, PKGSOURCE_PRI | PKGSRC_OPTION_SUBOPT, NULL},
    { "compress", 0, PKGSOURCE_COMPRESS |
                     PKGSRC_OPTION_STRING | PKGSRC_OPTION_SUBOPT, NULL },
    {  NULL,      0, 0, NULL }, 
};
#if 0    
static unsigned find_subopt(const char *optstr, struct subopt *subopts)
{
    int i;
    unsigned flag = 0;
    
    i = 0;
    while (subopts[i].name) {
        if (strcmp(subopts[i].name, optstr) == 0) {
            flag = subopts[i].flag;
            break;
        }
        i++;
    }

    return flag;
}
#endif

static
unsigned get_subopt(struct source *src, struct src_option *opt,
                    const char *str, const char *options_str)
{
    unsigned v = 0;
    
    n_assert(strncmp(str, opt->name, opt->len) == 0);
    
    str += opt->len;
    if (*str != '=') {
        logn(LOGWARN, _("%s: %s unknown option"), options_str, str);
        return 0;
    }
    
    str++;
    
    if (opt->flag & PKGSOURCE_TYPE) {
        src->type = n_strdup(str);
        v = 1;
        
    } else if (opt->flag & PKGSOURCE_DSCR) {
        src->dscr = n_strdup(str);
        v = 1;

    } else if (opt->flag & PKGSOURCE_COMPRESS) {
        src->compress = n_strdup(str);
        v = 1;
        
    } else if (opt->flag & PKGSOURCE_PRI) {
        if (sscanf(str, "%d", &v) == 1) {
            src->pri = v;
            v = 1;
        }
    }
    

    if (v == 0)
        logn(LOGWARN, _("%s%sinvalid value ('%s') for option '%s'"),
             src->name ? src->name : "", src->name ? ": " : "", 
             str, opt->name);

    return v;
}


const char *source_guess_type(const char *path) 
{
    path = path;
    return NULL;
}



struct source *source_malloc(void)
{
    struct source *src;
    
    src = n_malloc(sizeof(*src));
    memset(src, '\0', sizeof(*src));

    src->type = NULL;
    src->flags = src->subopt_flags = 0;
    src->pri = 0;
    src->no = 0;
    //src->flags |= PKGSOURCE_PRI;
    src->name = src->path = src->idxpath = src->pkg_prefix = NULL;
    src->dscr = src->type = NULL;
    src->lc_lang = NULL;
    src->_refcnt = 0;

    return src;
}


struct source *source_link(struct source *src) 
{
    src->_refcnt++;
    return src;
}


void source_free(struct source *src)
{
    if (src->_refcnt > 0) {
        src->_refcnt--;
        return;
    }
    
    if (src->path)    
        free(src->path);

    if (src->idxpath)    
        free(src->idxpath);
    
    if (src->pkg_prefix)
        free(src->pkg_prefix);
    
    if (src->name)
        free(src->name);

    if (src->type)
        free(src->type);

    if (src->dscr)
        free(src->dscr);

    if (src->lc_lang)
        free(src->lc_lang);
    
    free(src);
}

struct source *source_set_pkg_prefix(struct source *src, const char *prefix)
{
    char  clprefix[PATH_MAX];
    int   n;

    n_assert(prefix);
    n_assert(src->pkg_prefix == NULL);
    
    if ((n = vf_cleanpath(clprefix, sizeof(clprefix), prefix)) == 0 ||
        n == sizeof(clprefix))
        return NULL;
    
    
    src->pkg_prefix = n_strdup(clprefix);
    return src;
}


struct source *source_set_type(struct source *src, const char *type)
{
    
    if (src->type != NULL) {
        free(src->type);
        src->type = NULL;
    }
    
    if (type != NULL)
        src->type = n_strdup(type);
    
    return src;
}


char *source_set(char **member, char *value)
{
    if (*member) {
        free(*member);
        *member = NULL;
    }

    if (value)
        *member = n_strdup(value);

    return *member;
}

    

const char *source_set_idxpath(struct source *src)
{
    if (src->idxpath != NULL)
        free(src->idxpath);

    src->idxpath = pkgdir_idxpath(src->path, src->type, src->compress);
    return src->idxpath;
}


static char *parse_cmdl_pathspec(const char *pathspec, const char **path)
{
    const char  *p;
    char        *type;
    int         len, seplen = 1;

    
    p = pathspec;
    while (isalnum(*p))
        p++;

    if (p == pathspec)
        return NULL;

    if (*p != '#' && (seplen = strspn(p, ",")) != 2)
        return NULL;
    
    len  = p - pathspec + 1;
    type = n_malloc(len);
    memcpy(type, pathspec, len - 1);
    type[len - 1] = '\0';

    *path = p + seplen;
    return type;
}

static
void setup_langs(struct source *src)
{
    const char **langs_tokl, **p, *lang;
    char       lc_lang[256];
    tn_hash   *langs = NULL;
    int  n;
    
    if (src->dscr)
        lang = src->dscr;
    else 
        lang = lc_messages_lang();
    
    
    if (lang == NULL || *lang == '\0' || strcasecmp(lang, "none") == 0)
        return;

    
    langs_tokl = n_str_tokl(lang, ":");

    langs = n_hash_new(7, NULL);
    n_hash_ctl(langs, TN_HASH_NOCPKEY);
    
    n = 0;
    p = langs_tokl;
    while (*p) {
        if (n_hash_exists(langs, *p))
            continue;
        n += n_snprintf(&lc_lang[n], sizeof(lc_lang) - n, "%s:", *p);
        n_hash_insert(langs, *p, *p);
        p++;
    }
    if (!n_hash_exists(langs, "C"))
        n += n_snprintf(&lc_lang[n], sizeof(lc_lang) - n, "C:");
    
    if (n)
        lc_lang[n - 1] = '\0';  /* eat last ':' */

    
    src->lc_lang = n_strdupl(lc_lang, n - 1);
    //printf("source_setup_lc %s: %s -> %s\n", src->path, lang, lc_lang);
    n_hash_free(langs);
    n_str_tokl_free(langs_tokl);
}


struct source *source_new(const char *type, const char *pathspec,
                          const char *pkg_prefix)
{
    struct source   *src;
    struct stat     st;
    const char      *path, *p;
    char            *name, *q, *spec_type;
    int             len;
    char            clpath[PATH_MAX], clprefix[PATH_MAX];
    int             n;
    unsigned        flags = 0;

    
    if ((spec_type = parse_cmdl_pathspec(pathspec, &path)))
        pathspec = path;
    
    p = pathspec;
    while (*p && *p != '|' && *p != '#' && !isspace(*p))
        p++;

    if (*p == '\0') {           /* path only */
        path = pathspec;
        name = "-";
        
    } else {
        path = p + 1;
        while (isspace(*path))
            path++;
        
        len = p - pathspec;
        name = alloca(len + 1);
        memcpy(name, pathspec, len);
        name[len] = '\0';
        
        if (*name == '[') 
            name++;
        
        if ((q = strrchr(name, ']')))
            *q = '\0';
        
        if (*name == '\0')
            name = "-";
        else
            flags = PKGSOURCE_NAMED;
    }


    if ((n = vf_cleanpath(clpath, sizeof(clpath), path)) == 0 ||
        n == sizeof(clpath))
        return NULL;
    
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        if (clpath[n - 1] != '/')
            clpath[n++] = '/';
    
    } else {
        int l = strlen(path);
        if (clpath[n - 1] != '/' && path[l - 1] == '/')
            clpath[n++] = '/';
    }
    clpath[n] = '\0';

    if (pkg_prefix) {
        if ((n = vf_cleanpath(clprefix, sizeof(clprefix), pkg_prefix)) == 0 ||
            n == sizeof(clprefix))
            return NULL;
    }
    
    src = source_malloc();
    src->path = n_strdup(clpath);
    if (pkg_prefix)
        src->pkg_prefix = n_strdup(clprefix);
    else
        src->pkg_prefix = NULL;

    
    if ((q = strchr(name, ','))) {
        const char **tl, **t;
        
        *q++ = '\0';
        src->name = name;       /* temporary */
        
        tl = t = n_str_tokl(q, ",");
        n_assert(tl);

        while (*t) {
            int n = 0;
            while (source_options[n].name != NULL) {
                struct src_option *opt = &source_options[n];
                
                if (opt->len == 0)
                    opt->len = strlen(opt->name);
                
                if (opt->flag & PKGSRC_OPTION_SUBOPT) {
                    if (strncmp(*t, opt->name, opt->len) == 0) {
                        if (get_subopt(src, opt, *t, name))
                            src->flags |= opt->flag;
                        break;
                    }
                    
                } else if (strcmp(*t, opt->name) == 0) {
                    src->flags |= opt->flag;
                    break;
                }
                
                n++;
            }
            
            if (source_options[n].name == NULL)
                logn(LOGWARN, _("%s: %s unknown option"), name, *t);
            t++;
        }
        n_str_tokl_free(tl);
    }

    
    if (type != NULL)
        src->type = n_strdup(type);

    else if (spec_type != NULL && src->type == NULL) 
        src->type = n_strdup(spec_type);

    if (spec_type != NULL)
        free(spec_type);

    src->name = n_strdup(name);
    src->flags |= flags;
    if (src->type)
        src->flags |= PKGSOURCE_TYPE;
    else
        src->type = n_strdup(pkgdir_default_type);

    setup_langs(src);
    source_set_idxpath(src);
    return src;
}


int source_cmp(const struct source *s1, const struct source *s2)
{
    n_assert(s1->path);
    n_assert(s2->path);
    
    return strcmp(s1->path, s2->path);
}


int source_cmp_uniq(const struct source *s1, const struct source *s2)
{
    register int rc;
    
    if ((rc = source_cmp(s1, s2)) == 0) 
        logn(LOGWARN, _("removed duplicated source %s%s%s"),
             (s2->flags & PKGSOURCE_NAMED) ? s2->name : "",
             (s2->flags & PKGSOURCE_NAMED) ? " -- " : "",
             s2->path);
    
    return rc;
}


int source_cmp_pri(const struct source *s1, const struct source *s2)
{
    return s1->pri - s2->pri;
}


int source_cmp_name(const struct source *s1, const struct source *s2)
{
    if (strcmp(s1->name, "-") == 0)
        return 1;

    if (strcmp(s2->name, "-") == 0)
        return -1;
    
    return strcmp(s1->name, s2->name);
}


int source_cmp_pri_name(const struct source *s1, const struct source *s2)
{
    int rc;
    
    if ((rc = (s1->pri - s2->pri)) == 0)
        return source_cmp_name(s1, s2);
    
    return rc;
}


static int source_update_a(struct source *src) 
{
    if (src->type == NULL)
        source_set_type(src, pkgdir_default_type);
    source_set_idxpath(src);
    
    return pkgdir_update_a(src);
}


int source_update(struct source *src, unsigned flags)
{
    struct pkgdir  *pkgdir;
    int            pcaps, rc = 0;


    if (src->type == NULL)
        source_set_type(src, pkgdir_default_type);
    source_set_idxpath(src);
    
	pcaps = pkgdir_type_info(src->type);
	
    if ((pcaps & (PKGDIR_CAP_UPDATEABLE_INC | PKGDIR_CAP_UPDATEABLE)) == 0) {
        logn(LOGWARN, _("%s: this type (%s) of source is not updateable"),
			 source_idstr(src), src->type);
		
	} else if ((pcaps & PKGDIR_CAP_UPDATEABLE_INC) == 0) {
		if (flags & PKGSOURCE_UPA)
			return source_update_a(src);
		
		logn(LOGWARN, _("%s: this type (%s) of source is not updateable; "
						"use --upa to refresh it"),
			 source_idstr(src), src->type);
		
	} else {
        if (flags & PKGSOURCE_UPA)
			return source_update_a(src);
        
		pkgdir = pkgdir_srcopen(src, 0);
		if (pkgdir != NULL) {
			rc = pkgdir_update(pkgdir, 0);
			pkgdir_free(pkgdir);
		}
	}
	
	return rc;
}


static
int source_snprintf_flags(char *str, int size, const struct source *src)
{
    int n, i;
    
    n_assert(size > 0);
    
    *str = '\0';

    i = n = 0;
    while (source_options[i].name != NULL) {
        struct src_option *opt = &source_options[i++];
        
        if (opt->len == 0)
            opt->len = strlen(opt->name);
        
        if ((src->flags & (opt->flag & ~PKGSRC_OPTION_OPTMASK)) == 0)
            continue;

        if ((opt->flag & PKGSRC_OPTION_SUBOPT) == 0) {
            n += snprintf_c(PRCOLOR_GREEN, &str[n], size - n, "%s", opt->name);
            n += n_snprintf(&str[n], size - n, ",");
            // n += n_snprintf(&str[n], size - n, "%s,", opt->name);

        } else if ((opt->flag & PKGSOURCE_PRI)) {
            if (src->pri) {
                n += snprintf_c(PRCOLOR_GREEN, &str[n], size - n, "%s", opt->name);
                n += n_snprintf(&str[n], size - n, "=%d,", src->pri);
            }

        } else if ((opt->flag & PKGSOURCE_TYPE)) {
            if (src->type && !source_is_type(src, pkgdir_default_type)) {
                n += snprintf_c(PRCOLOR_GREEN, &str[n], size - n, "%s",
                                opt->name);
                n += n_snprintf(&str[n], size - n, "=%s,", src->type);
            }

        } else if ((opt->flag & PKGSOURCE_DSCR)) {
            if (src->dscr) {
                n += snprintf_c(PRCOLOR_GREEN, &str[n], size - n, "%s",
                                opt->name);
                n += n_snprintf(&str[n], size - n, "=%s,", src->dscr);
            }


        } else {
            int j = 0;
            
            while (opt->subopts[j].name != NULL) {
                struct subopt *subopt = &opt->subopts[j++];
                
                if (subopt->isdefault)
                    continue;
                    
                if (src->subopt_flags & subopt->flag) {
                    n += snprintf_c(PRCOLOR_GREEN, &str[n], size - n, "%s",
                                    opt->name);
                    
                    n += n_snprintf(&str[n], size - n, "=%s,", subopt->name);
                    
                    //n += n_snprintf(&str[n], size - n, "%s=%s,", opt->name,
                    //                subopt->name);
                    break;
                }
            }
        }
    }
    
    
    if (n > 0)
        str[n - 1] = '\0';      /* eat last comma */
    
    return n;
}


void source_printf(const struct source *src) 
{
    char optstr[256];

    *optstr = '\0';
    source_snprintf_flags(optstr, sizeof(optstr), src);
    
    printf("%-12s %s%s%s%s\n",
           src->name, vf_url_slim_s(src->path, 0),
           *optstr ? "  (" : "", optstr, *optstr ? ")" : "");

    if (src->pkg_prefix) {
        //printf_c(PRCOLOR_GREEN, "%-14s prefix: ", "");
        //printf("%s\n", src->pkg_prefix);
        printf("%-14s prefix => %s\n", "", vf_url_slim_s(src->pkg_prefix, 0));
    }
}

int sources_update(tn_array *sources, unsigned flags)
{
    int i, nerr = 0;
    
    for (i=0; i < n_array_size(sources); i++) {
        struct source *src = n_array_nth(sources, i);
        
        if (src->flags & PKGSOURCE_NOAUTOUP)
            continue;
        
        if (i > 0)
            msgn(0, "\n");

        if (!source_update(src, flags))
            nerr++;
    }

    return nerr == 0;
}


int source_clean(struct source *src, unsigned flags)
{
    char                        *p, *idxpath;
    int                         urltype;


    n_assert(src->type);
    if ((urltype = vf_url_type(src->path)) == VFURL_UNKNOWN)
        return 1;

    idxpath = pkgdir_idxpath(src->path, src->type, "none");
    if (idxpath == NULL)
        return 0;

    
    if (urltype & VFURL_LOCAL) {
        char path[PATH_MAX];
        vf_localdirpath(path, sizeof(path), n_dirname(idxpath));
        //printf("%s, %s, %s\n", src->path, idxpath, path);
        pkgdir_cache_clean(path, "*");
        
    } else if ((p = strrchr(idxpath, '/'))) {
        char amask[1024], *mask;
        
        *p = '\0';

        if (flags & PKGSOURCE_CLEANA)
            mask = "*";
        else {
            n_snprintf(amask, sizeof(amask), "%s.*", p + 1);
            mask = amask;
        }
        
        pkgdir_cache_clean(idxpath, mask);
    }
    
    free(idxpath);
    return 1;
}


int sources_clean(tn_array *sources, unsigned flags) 
{
    int i,  nerr = 0;
    
    for (i=0; i < n_array_size(sources); i++) {
        struct source *src = n_array_nth(sources, i);
        if (!source_clean(src, flags))
            nerr++;
    }

    return nerr == 0;
}


int sources_add(tn_array *sources, struct source *src) 
{
    if (src->no == 0)
        src->no = n_array_size(sources) * 10;
    
    DBGF("%p %s (%d) %s\n", sources, src->name ? src->name: "-", src->no,
         src->path ? src->path:"null");
         
    n_array_push(sources, src);
    return n_array_size(sources);
}


void sources_score(tn_array *sources) 
{
    int i;
    int pri_min = INT_MAX;
    
    for (i=0; i < n_array_size(sources); i++) {
        struct source *src = n_array_nth(sources, i);
        if (src->pri < pri_min)
            pri_min = src->pri;
    }

    for (i=0; i < n_array_size(sources); i++) {
        struct source *src = n_array_nth(sources, i);
        if (src->pri == 0) {
            src->pri = src->no + pri_min + 1;
            if (src->pri < 0)   /* set to default 0 */
                src->pri = 0;
        }
    }
}


int source_make_idx(struct source *src,
                    const char *type, const char *idxpath,
                    unsigned cr_flags) 
{
    struct pkgdir   *pkgdir;
    char            path[PATH_MAX];
    int             rc = 0;
    unsigned        ldflags = 0;
    
    n_assert(type);
    if (idxpath == NULL && strstr(src->path, "://")) {
        logn(LOGERR, _("mkidx requested for remote source without destination "
            "specified"));
        return 0;
    }

    if (idxpath == NULL)
        idxpath = src->path;
    
    if (is_dir(idxpath)) {
        const char *fn = pkgdir_type_default_idxfn(type);
        if (fn) {
            pkgdir_make_idxpath(path, sizeof(path), type, idxpath,
                                fn, src->compress);
            idxpath = path;
        }
    }
    
    printf("mkidx[%s, %s] %s\n", src->type, type, src->path);
    pkgdir = pkgdir_srcopen(src, 0);
    if (pkgdir == NULL)
        return 0;

    if (source_is_type(src, "dir")) {
        struct pkgdir *pdir;
        printf("LOAD ORIG\n");
        ldflags |= PKGDIR_LD_DESC;
        
        pdir = pkgdir_open_ext(src->path,
                               src->pkg_prefix, type,
                               "orig", NULL, 0, src->lc_lang);
        if (pdir && !pkgdir_load(pdir, NULL, 0)) {
            pkgdir_free(pdir);
            pdir = NULL;
        }

        pkgdir->prev_pkgdir = pdir;
    }

    rc = 0;
    if (pkgdir_load(pkgdir, NULL, ldflags)) {
        int n = n_array_size(pkgdir->pkgs);
        rc = pkgdir_save(pkgdir, type, idxpath, cr_flags);
    }
    
    if (pkgdir)
        pkgdir_free(pkgdir);
    
    return rc;
}

