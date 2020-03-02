/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

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

#include <string.h>
#include <limits.h>
#include <sys/param.h>          /* for PATH_MAX */
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

#include "compiler.h"
#include "pkgdir.h"
#include "pkgdir_intern.h"
#include "source.h"
#include "misc.h"
#include "log.h"
#include "poldek_term.h"
#include "i18n.h"
#include "conf.h"
#include "source.h"

extern const char *pkgdir_dirindex_basename;

#define SOURCE_DEFAULT_PRI 0

const char source_TYPE_GROUP[] = "group";
const char *poldek_conf_PKGDIR_DEFAULT_TYPE = "pndir";
const char *poldek_conf_PKGDIR_DEFAULT_COMPR = COMPR_GZ;

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
    { "lang",     0, PKGSOURCE_DSCR |
                     PKGSRC_OPTION_STRING | PKGSRC_OPTION_SUBOPT, NULL },
    { "group",    0, PKGSOURCE_GROUP |
                     PKGSRC_OPTION_STRING | PKGSRC_OPTION_SUBOPT, NULL },
    { "pri",      0, PKGSOURCE_PRI | PKGSRC_OPTION_SUBOPT, NULL},
    { "compr",    0, PKGSOURCE_COMPR |
                     PKGSRC_OPTION_STRING | PKGSRC_OPTION_SUBOPT, NULL },
    {  NULL,      0, 0, NULL },
};

static char *source_set(char **member, const char *value);

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
        source_set(&src->type, str);
        v = 1;

    } else if (opt->flag & PKGSOURCE_DSCR) {
        source_set(&src->dscr, str);
        v = 1;

    } else if (opt->flag & PKGSOURCE_GROUP) {
        source_set(&src->group, str);
        v = 1;

    } else if (opt->flag & PKGSOURCE_COMPR) {
        source_set(&src->compr, str);
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

    src->type = src->original_type = NULL;
    src->flags = src->subopt_flags = 0;
    src->pri = 0;
    src->no = 0;
    //src->flags |= PKGSOURCE_PRI;
    src->name = src->path = src->pkg_prefix = NULL;
    src->group = src->dscr = NULL;
    src->lc_lang = NULL;
    src->_refcnt = 0;
    src->exclude_path = n_array_new(4, free, (tn_fn_cmp)strcmp);
    src->ign_patterns = n_array_new(4, free, (tn_fn_cmp)strcmp);
    return src;
}


struct source *source_link(struct source *src)
{
    src->_refcnt++;
    return src;
}

static void cp_str_ifnotnull(char **dst, const char *src)
{
    if (src)
        *dst = n_strdup(src);
}

struct source *source_clone(const struct source *src)
{
    struct source *nsrc;

    nsrc = source_malloc();

    nsrc->flags = src->flags;
    cp_str_ifnotnull(&nsrc->type, src->type);
    cp_str_ifnotnull(&nsrc->name, src->name);
    cp_str_ifnotnull(&nsrc->path, src->path);
    cp_str_ifnotnull(&nsrc->pkg_prefix, src->pkg_prefix);
    cp_str_ifnotnull(&nsrc->compr, src->compr);

    cp_str_ifnotnull(&nsrc->dscr, src->dscr);
    cp_str_ifnotnull(&nsrc->group, src->group);
    cp_str_ifnotnull(&nsrc->lc_lang, src->lc_lang);
    cp_str_ifnotnull(&nsrc->original_type, src->original_type);

    n_array_free(nsrc->exclude_path);
    nsrc->exclude_path = n_ref(src->exclude_path);

    n_array_free(nsrc->ign_patterns);
    nsrc->ign_patterns = n_ref(src->ign_patterns);

    nsrc->subopt_flags = src->subopt_flags;
    return nsrc;
}

void source_free(struct source *src)
{
    if (src->_refcnt > 0) {
        src->_refcnt--;
        return;
    }

    n_cfree(&src->type);
    n_cfree(&src->name);
    n_cfree(&src->path);
    n_cfree(&src->pkg_prefix);

    n_cfree(&src->compr);
    n_cfree(&src->dscr);
    n_cfree(&src->group);
    n_cfree(&src->lc_lang);
    n_cfree(&src->original_type);

    if (src->exclude_path)
        n_array_free(src->exclude_path);

    if (src->ign_patterns)
        n_array_free(src->ign_patterns);

    memset(src, 0, sizeof(*src));
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

static
char *source_set(char **member, const char *value)
{
    if (*member) {
        free(*member);
        *member = NULL;
    }

    if (value)
        *member = n_strdup(value);

    return *member;
}

struct source *source_set_type(struct source *src, const char *type)
{
    source_set(&src->type, type);
    return src;
}

struct source *source_set_defaults(struct source *src)
{
    if ((src->flags & PKGSOURCE_TYPE) == 0) /* not set by config*/
        source_set(&src->type, poldek_conf_PKGDIR_DEFAULT_TYPE);

    if ((src->flags & PKGSOURCE_COMPR) == 0) /* not set by config*/
        source_set(&src->compr, poldek_conf_PKGDIR_DEFAULT_COMPR);

    return src;
}

struct source *source_set_compr(struct source *src, const char *compr)
{
    source_set(&src->compr, compr);
    return src;
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
        if (n_hash_exists(langs, *p)) {
            p++;
            continue;
        }

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


struct source *source_new(const char *name, const char *type,
                          const char *path, const char *pkg_prefix)
{
    struct source   *src;
    struct stat     st;
    char            clpath[PATH_MAX], clprefix[PATH_MAX];
    int             n;

    n_assert(name || path);

    if (path) {
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

    }

    if (pkg_prefix) {
        n_assert(path);
        if ((n = vf_cleanpath(clprefix, sizeof(clprefix), pkg_prefix)) == 0 ||
            n == sizeof(clprefix))
            return NULL;
    }

    src = source_malloc();
    if (name) {
        src->flags |= PKGSOURCE_NAMED;
        src->name = n_strdup(name);
    }

    if (type) {
        src->type = n_strdup(type);
        src->flags |= PKGSOURCE_TYPE;

    } else {
        src->type = n_strdup(poldek_conf_PKGDIR_DEFAULT_TYPE);
    }


    if (path)
        src->path = n_strdup(clpath);

    if (pkg_prefix)
        src->pkg_prefix = n_strdup(clprefix);

    char *p;
    if (src->path && (p = strrchr(n_basenam(src->path), '.')) != NULL) {
        p++;
        if (n_str_in(p, COMPR_GZ, COMPR_ZST, NULL))
            src->compr = n_strdup(p);
    }

    if (src->compr == NULL)
        src->compr = n_strdup(poldek_conf_PKGDIR_DEFAULT_COMPR);

    return src;
}

struct source *source_new_pathspec(const char *type, const char *pathspec,
                                   const char *pkg_prefix)
{
    struct source   *src;
    const char      *path, *p;
    char            *name, *q, *spec_type, *opts = NULL;
    int             len;
    unsigned        flags = 0;


    if (*pathspec == '\0')
        return NULL;

    if ((spec_type = parse_cmdl_pathspec(pathspec, &path)))
        pathspec = path;

    p = pathspec;
    while (*p && *p != '|' && *p != '#' && !isspace(*p))
        p++;

    if (*p == '\0') {           /* path only */
        path = pathspec;
        name = NULL;

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

        if ((q = strchr(name, ','))) {
            *q = '\0';
            opts = ++q;
        }

        if (*name == '\0')
            name = NULL;
    }

    src = source_new(name, type ? type : spec_type, path, pkg_prefix);
    if (src == NULL)
        return NULL;

    src->flags |= flags;
    if (spec_type != NULL)
        free(spec_type);

    if (opts) {
        const char **tl, **t;

        tl = t = n_str_tokl(opts, ",");
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

    setup_langs(src);
    return src;
}

static
int get_conf_opt_list(const tn_hash *htcnf, const char *name,
                      tn_array *tolist)
{
    tn_array *list;
    int i = 0;

    if (n_array_size(tolist) > 0)
        return 0;

    if ((list = poldek_conf_get_multi(htcnf, name))) {
        for (i=0; i < n_array_size(list); i++)
            n_array_push(tolist, n_strdup(n_array_nth(list, i)));

        n_array_free(list);
    }

    n_array_sort(tolist);
    n_array_uniq(tolist);
    return i;
}

struct source *source_new_htcnf(const tn_hash *htcnf)
{
    char spec[PATH_MAX];
    struct source *src;
    const char *vs, *type, *srcnam;
    int  n = 0;
    int  v;

    vs = poldek_conf_get(htcnf, "name", NULL);
    if (vs == NULL)
        vs = "anon";
    srcnam = vs;

    n += n_snprintf(&spec[n], sizeof(spec) - n, "%s", vs);

    if ((vs = poldek_conf_get(htcnf, "type", NULL)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",type=%s", vs);

    type = vs;

    if ((v = poldek_conf_get_int(htcnf, "pri", 0)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",pri=%d", v);

    if ((v = poldek_conf_get_bool(htcnf, "noauto", 0)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",noauto");

    else if ((v = poldek_conf_get_bool(htcnf, "auto", 1)) == 0)
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",noauto");

    if ((v = poldek_conf_get_bool(htcnf, "noautoup", 0)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",noautoup");

    else if ((v = poldek_conf_get_bool(htcnf, "autoup", 1)) == 0)
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",noautoup");

    if ((v = poldek_conf_get_bool(htcnf, "signed", 0)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",sign");

    else if ((v = poldek_conf_get_bool(htcnf, "sign", 0)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",sign");

    if ((vs = poldek_conf_get(htcnf, "lang", NULL)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",lang=%s", vs);

    if ((vs = poldek_conf_get(htcnf, "group", NULL)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",group=%s", vs);

    if ((vs = poldek_conf_get(htcnf, "compr", NULL)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",compr=%s", vs);

    vs = poldek_conf_get(htcnf, "path", NULL);
    if (vs == NULL)
        vs = poldek_conf_get(htcnf, "url", NULL);

    if (vs == NULL && type && n_str_ne(type, source_TYPE_GROUP)) {
        logn(LOGERR, "source: %s: missing required 'path'", srcnam);
        return NULL;
    }

    if (type && n_str_eq(type, source_TYPE_GROUP)) {
        char tmp[PATH_MAX], *p;
        int i, nt = 0;

        tn_array *arr = poldek_conf_get_multi(htcnf, "sources");
        n_array_sort(arr);
        for (i=0; i<n_array_size(arr); i++)
            nt += n_snprintf(&tmp[nt], sizeof(tmp) - nt, "%s%s", n_array_nth(arr, i),
                             i < n_array_size(arr) - 1 ? ", " : "");
        n_array_free(arr);
        n_strdupap((char*)tmp, &p);
        vs = p;
    }

    //printf("spec %d = %s\n", n_hash_size(htcnf), spec);
    //n_assert(vs);

    n_snprintf(&spec[n], sizeof(spec) - n, " %s", vs);

    vs = poldek_conf_get(htcnf, "prefix", NULL);

    src = source_new_pathspec(NULL, spec, vs);

    vs = poldek_conf_get(htcnf, "original type", NULL);
    if (vs && src->type && n_str_eq(src->type, vs)) {
        logn(LOGERR, "%s: original type and type must be differ",
             source_idstr(src));

        source_free(src);
        return NULL;
    }
    if (vs)
        src->original_type = n_strdup(vs);

    get_conf_opt_list(htcnf, "exclude path", src->exclude_path);
    get_conf_opt_list(htcnf, "ignore", src->ign_patterns);
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

    if ((rc = source_cmp(s1, s2)) == 0) {
        const char *n1, *n2;
        n1 = s1->type ? s1->type : "";
        n2 = s2->type ? s2->type : "";
        rc = strcmp(n1, n2);
    }

    if (rc == 0)
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
    const char *n1, *n2;
    n1 = s1->name ? s1->name : "";
    n2 = s2->name ? s2->name : "";
    return strcmp(n1, n2);
}

int source_cmp_pri_name(const struct source *s1, const struct source *s2)
{
    int rc;

    if ((rc = (s1->pri - s2->pri)) == 0)
        return source_cmp_name(s1, s2);

    return rc;
}

int source_cmp_no(const struct source *s1, const struct source *s2)
{
    int rc;

    rc = s1->no - s2->no;
    n_assert(rc);
    return rc;
}

static int source_update_a(struct source *src)
{
    if (src->type == NULL)
        source_set_type(src, poldek_conf_PKGDIR_DEFAULT_TYPE);

    return pkgdir_update_a(src);
}

int source_update(struct source *src, unsigned flags)
{
    struct pkgdir  *pkgdir;
    int            pcaps, rc = 0;


    if (src->type == NULL)
        source_set_type(src, poldek_conf_PKGDIR_DEFAULT_TYPE);

    pcaps = pkgdir_type_info(src->type);

    if ((pcaps & (PKGDIR_CAP_UPDATEABLE_INC | PKGDIR_CAP_UPDATEABLE)) == 0) {
        logn(LOGWARN, _("%s: this type (%s) of source is not updateable"),
			 source_idstr(src), src->type);

    } else if ((pcaps & PKGDIR_CAP_UPDATEABLE_INC) == 0) {
        if (flags & (PKGSOURCE_UPA | PKGSOURCE_UPAUTOA))
            return source_update_a(src);

        logn(LOGWARN, _("%s: this type (%s) of source is not updateable; "
                        "use --upa to refresh it"),
             source_idstr(src), src->type);

    } else {
        if ((flags & PKGSOURCE_UPA) && (flags & PKGSOURCE_UPAUTOA) == 0)
            return source_update_a(src);

        if (flags & PKGSOURCE_UPAUTOA)
            src->flags |= PKGSOURCE_AUTOUPA;

        pkgdir = pkgdir_srcopen(src, 0);
        if (pkgdir != NULL) {
            rc = pkgdir_update(pkgdir);
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
            n += poldek_term_snprintf_c(PRCOLOR_GREEN, &str[n], size - n,
                                        "%s", opt->name);
            n += n_snprintf(&str[n], size - n, ",");
            // n += n_snprintf(&str[n], size - n, "%s,", opt->name);

        } else if ((opt->flag & PKGSOURCE_PRI)) {
            if (src->pri) {
                n += poldek_term_snprintf_c(PRCOLOR_GREEN, &str[n], size - n,
                                            "%s", opt->name);
                n += n_snprintf(&str[n], size - n, "=%d,", src->pri);
            }

        } else if ((opt->flag & PKGSOURCE_TYPE)) {
            if (src->type) {
                n += poldek_term_snprintf_c(PRCOLOR_GREEN, &str[n], size - n,
                                            "%s", opt->name);
                n += n_snprintf(&str[n], size - n, "=%s,", src->type);
            }

        } else if ((opt->flag & PKGSOURCE_GROUP)) {
            if (src->type) {
                n += poldek_term_snprintf_c(PRCOLOR_GREEN, &str[n], size - n,
                                            "%s", opt->name);
                n += n_snprintf(&str[n], size - n, "=%s,", src->group);
            }

        } else if ((opt->flag & PKGSOURCE_DSCR)) {
            if (src->dscr) {
                n += poldek_term_snprintf_c(PRCOLOR_GREEN, &str[n], size - n,
                                            "%s", opt->name);
                n += n_snprintf(&str[n], size - n, "=%s,", src->dscr);
            }

        } else {
            int j = 0;

            while (opt->subopts[j].name != NULL) {
                struct subopt *subopt = &opt->subopts[j++];

                if (subopt->isdefault)
                    continue;

                if (src->subopt_flags & subopt->flag) {
                    n += poldek_term_snprintf_c(PRCOLOR_GREEN, &str[n],
                                                size - n, "%s", opt->name);

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
           src->name ? src->name : "-", vf_url_slim_s(src->path, 0),
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

        if (!source_update(src, flags))
            nerr++;
    }

    return nerr == 0;
}

static
int do_source_clean(struct source *src, const char *idxdir,
                    const char *idxbn, unsigned flags)
{
    int urltype = vf_url_type(idxdir);

    n_assert(src->type);

    DBGF("%s: %s, %s\n", src->path, idxdir, idxbn);

    /* clean-pkg makes no sense for local repositories */
    if ((urltype & VFURL_LOCAL) && (flags & PKGSOURCE_CLEAN)) {
        char path[PATH_MAX];
        vf_localdirpath(path, sizeof(path), idxdir);
        pkgdir__cache_clean(path, "*", flags & PKGSOURCE_CLEAN_TEST);

    } else {
        char amask[1024], *mask = NULL;

        if ((flags & PKGSOURCE_CLEANA) == PKGSOURCE_CLEANA) {
            mask = "*";

        } else if (flags & PKGSOURCE_CLEAN) {
            n_assert(idxbn);
            n_snprintf(amask, sizeof(amask), "%s.*", idxbn);
            mask = amask;

        } else { // (flags & PKGSOURCE_CLEANPKG -- default
            n_snprintf(amask, sizeof(amask), "*.rpm");
            mask = amask;
        }

        n_assert(mask);
        pkgdir__cache_clean(idxdir, mask, flags & PKGSOURCE_CLEAN_TEST);
    }

    return 1;
}

int source_clean(struct source *src, unsigned flags)
{
    char path[PATH_MAX], *dn, *bn;
    int rc = 0;

    n_assert(src->type);
    if (pkgdir__make_idxpath(path, sizeof(path), src->path,
                             src->type, COMPR_NONE) != NULL) {

        n_basedirnam(path, &dn, &bn);
        rc = do_source_clean(src, dn, bn, flags);
        /* should be able to pass multiple masks at once, TODO */
        rc = do_source_clean(src, dn, pkgdir_dirindex_basename, flags);

    }

    if (src->pkg_prefix && (flags & PKGSOURCE_CLEANPKG))
        rc = do_source_clean(src, src->pkg_prefix, NULL, flags);

    /* we don't really care about the result */
    return rc;
}

int sources_clean(tn_array *sources, unsigned flags)
{
    int i,  nerr = 0;

    if (flags & PKGSOURCE_CLEAN_WHOLE_CACHEDIR) {
	// test mode is not supported
	if (flags & PKGSOURCE_CLEAN_TEST)
	    return 1;

	return vfile_cachedir_clean() == 0;
    }

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
        src->no = n_array_size(sources) * 60;

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
