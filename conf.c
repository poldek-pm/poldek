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

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>

#include <trurl/nhash.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>
#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
#include <trurl/nstream.h>
#include <vfile/vfile.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "conf.h"
#include "misc.h"
#include "poldek_util.h"
#include "conf_intern.h"
#include "pm/rpmorg/arch.h"

static const char *global_tag = "global";
static const char include_tag[] = "%include";
static const char includedir_tag[] = "%includedir";

static struct poldek_conf_tag unknown_tag = {
    NULL, CONF_TYPE_STRING | CONF_TYPE_F_ENV | CONF_TYPE_F_MULTI_EXCL,
    NULL, 0, { 0 },
};

/* XXX: aliases are not implemented yet
static struct poldek_conf_tag alias_tags[] = {
    { "name",       CONF_TYPE_STRING | CONF_TYPE_F_REQUIRED,  { 0 } },
    { "cmd",        CONF_TYPE_STRING | CONF_TYPE_F_REQUIRED, { 0 } },
    { "ctx",        CONF_TYPE_ENUM, { "none", "installed", "available", "upgradeable", NULL } },
    {  NULL,        0, { 0 } },
};
    { "pri",         CONF_TYPE_STRING , { 0 } },
*/

struct poldek_conf_section *sections = poldek_conf_sections; /* just for short */

#define COPT_MULTIPLE (1 << 0)
struct copt {
    unsigned flags;

    tn_array *vals;
    char     *val;
    int      _refcnt;
    char     name[0];
};

/* configuration file */
struct afile {
    struct vfile  *vf;
    char          *section_to_load; /* load only this sections */
    char          path[0];
};

static struct copt *copt_new(const char *name)
{
    struct copt *opt;
    opt = n_malloc(sizeof(*opt) + strlen(name) + 1);
    strcpy(opt->name, name);
    opt->flags = 0;
    opt->_refcnt = 0;
    opt->val = NULL;
    opt->vals= NULL;
    return opt;
}

static void copt_free(struct copt *opt)
{
    if (opt->_refcnt > 0) {
        opt->_refcnt--;
        return;
    }

    if (opt->flags & COPT_MULTIPLE) {
        n_array_free(opt->vals);
    } else {
        n_assert(opt->vals == NULL);
    }
    n_cfree(&opt->val);
    free(opt);
}

/* remember which section's tags validate the options of ht_sect */
static void section_set_name(tn_hash *ht_sect, const char *sectnam)
{
    struct copt *opt;

    if (n_hash_exists(ht_sect, "__section_name"))
        return;

    opt = copt_new("__section_name");
    opt->val = n_strdup(sectnam);
    n_hash_insert(ht_sect, opt->name, opt);
}

static int parse_val_list(tn_hash *ht, char *name, char *vstr, const char *sep,
                          const char *path, int nline)
{
    const char **v, **p;
    struct copt *opt;


    path = path; nline = nline;
    if (sep == NULL)
        sep = " \t,";

    p = v = n_str_tokl(vstr, sep);

    if (v == NULL)              /* n_str_tokl error */
        return 0;

    if (*v == NULL) {             /* empty option value */
        n_str_tokl_free(v);
        return 1;
    }

    if (n_hash_exists(ht, name)) {
        opt = n_hash_get(ht, name);

    } else {
        opt = copt_new(name);
        n_hash_insert(ht, opt->name, opt);
    }


    while (*p) {
        if (opt->val == NULL) {
            DBGF("%s[list] += %s\n", name, *p);
            opt->val = n_strdup(*p);

        } else {
            if (opt->vals == NULL)
                opt->vals = n_array_new(2, free, (tn_fn_cmp)strcmp);

            if (n_array_size(opt->vals) == 0) {
                opt->flags |= COPT_MULTIPLE;
                n_array_push(opt->vals, n_strdup(opt->val));
            }

            n_array_push(opt->vals, n_strdup(*p));
            DBGF("%s[list] += %s\n", name, *p);
        }
        p++;
    }

    n_str_tokl_free(v);
    return 1;
}

static char *parse_val(char *vstr, const char *path, int nline)
{
    char *p, *q;

    p = n_str_strip_ws(vstr);
    if (p == NULL || *p == '\0')
        return NULL;

    q = p;
    if (q && (q = strchr(q, '#'))) {
        if (q == p) {
            p = NULL;
        } else {
            if (*(q - 1) != '\\') {
                *q = '\0';
                p = n_str_strip_ws(p);
            }
        }
    }

    if (p && *p == '"') {
        p++;
        q = strchr(p, '\0');
        q--;
        while (isspace(*q))
            q--;

        if (*q != '"') {
            logn(LOGERR, _("%s:%d: missing '\"'"), path, nline);
            p = NULL;
        }

        *q = '\0';
        if (p == q)
            p = NULL;
    }

    return p;
}

static const struct poldek_conf_section *find_section(const char *name)
{
    int i = 0;

    while (sections[i].name) {
        if (strcmp(sections[i].name, name) == 0)
            return &sections[i];
        i++;
    }

    return NULL;
}


static int find_tag(const char *sectname, const char *key,
                    const struct poldek_conf_section **sectp)
{
    int i = 0;
    struct poldek_conf_tag *tags = NULL;
    const struct poldek_conf_section *sect;

    *sectp = NULL;
    if ((sect = find_section(sectname)) == NULL)
        return -1;
    *sectp = sect;

    tags = sect->tags;
    i = 0;
    while (tags[i].name) {
        if (strcmp(tags[i].name, key) == 0)
            return i;

        if (fnmatch(tags[i].name, key, 0) == 0)
            return i;
        i++;
    }
    return -1;
}

/* expand %{...} macros */
static const char *expand_macros(char *dest, int size, const char *str,
                                 const tn_hash *ht)
{
    const char **tl, **tl_save;
    int n = 0;


    tl = tl_save = n_str_tokl(str, "%");
    if (*str != '%' && tl[1] == NULL) {
        n_str_tokl_free(tl);
        return str;
    }

    if (*str != '%') {
        n = n_snprintf(dest, size, "%s", *tl);
        tl++;
    }

    while (*tl) {
        const char *p,  *vv, *v, *var;
        char val[256];
        int  v_len;


        p = v = *tl;
        DBGF("token: %s\n", *tl);
        tl++;

        if (*v != '{') {
            n += n_snprintf(&dest[n], size - n, "%%%s", v);
            continue;
        }

        v++;

        vv = v;
        v_len = 0;
        while (isalnum(*vv) || *vv == '_' || *vv == '-') {
            vv++;
            v_len++;
        }

        if (*vv == '}')
            vv++;

        if (v_len + 1 > (int)sizeof(val))
            return str;

        n_snprintf(val, v_len + 1, "%s", v);
        DBGF("var (%s)\n", val);

        if ((var = poldek_conf_get(ht, val, NULL)) == NULL) {
            n += n_snprintf(&dest[n], size - n, "%%%s", p);

        } else {
            n += n_snprintf(&dest[n], size - n, "%s", var);
            n += n_snprintf(&dest[n], size - n, "%s", vv);
        }
    }

    n_str_tokl_free(tl_save);
    return dest;
}

static int verify_section(const struct poldek_conf_section *sect, tn_hash *ht)
{
    int i = 0, nerr = 0;
    struct poldek_conf_tag *tags;
    tags = sect->tags;

    while (tags[i].name) {
        struct poldek_conf_tag *t = &tags[i];

        if ((t->flags & CONF_TYPE_F_REQUIRED) && !n_hash_exists(ht, t->name)) {
            const char *missing_tag = t->name;

            if (n_str_eq(sect->name, "source") &&
                (n_str_eq(t->name, "path") || n_str_eq(t->name, "url"))) {

                const char *type = poldek_conf_get(ht, "type", NULL);

                if (type && n_str_eq(type, "group")) { /* source group */
                    missing_tag = NULL;
                    if (!n_hash_exists(ht, "sources"))
                        missing_tag = "sources";
                }
            }

            if (missing_tag) {
                struct copt *opt = n_hash_get(ht, "__file__line");
                logn(LOGERR, "%s%s[%s]: missing required '%s'", opt ? opt->val : "",
                     opt ? ": " : "",  sect->name, missing_tag);
                nerr++;
            }
        }
        i++;
    }

    return nerr == 0;
}


static tn_hash *sysmacros = NULL;

static void sysmacros_free(void)
{
    if (sysmacros) {
        n_hash_free(sysmacros);
        sysmacros = NULL;
    }
}

static tn_hash *sysmacros_get(void)
{
    if (sysmacros == NULL) {
        sysmacros = n_hash_new(2, (tn_fn_free)copt_free);
        n_hash_ctl(sysmacros, TN_HASH_NOCPKEY);

        char *arch = pm_rpm_arch_standalone();
        if (arch) {
            struct copt *opt = copt_new("_arch");
            opt->val = arch;
            n_hash_insert(sysmacros, opt->name, opt);
        }

        atexit(sysmacros_free);
    }

    return sysmacros;
}

static
const char *do_expand_value(char *expanded_val, size_t size, const char *val,
                            tn_hash *ht, tn_hash *ht_global)
{
    const char *new_val;
    char expand_val[PATH_MAX], expand_val2[PATH_MAX];

    new_val = expand_macros(expand_val, sizeof(expand_val), val, ht);
    if (ht_global && strchr(new_val, '%')) {
        new_val = expand_macros(expand_val2, sizeof(expand_val2),
                                new_val, ht_global);
    }

    if (strchr(new_val, '%') && strstr(new_val, "_arch")) {
        /* ping-pong: write to the buffer that is not the current source */
        if (new_val == expand_val)
            new_val = expand_macros(expand_val2, sizeof(expand_val2),
                                    new_val, sysmacros_get());
        else
            new_val = expand_macros(expand_val, sizeof(expand_val),
                                    new_val, sysmacros_get());
    }

    if (val != new_val) {
        n_snprintf(expanded_val, size, "%s", new_val);
        val = expanded_val;
    }

    return val;
}

static
const char *expand_value(char *expanded_val, size_t size, const char *val,
                         tn_hash *ht, tn_hash *ht_global)
{
    const char *new_val, *tmp_val, *orig_val;
    int n = 0;

    if (strchr(val, '%') == NULL)
        return val;

    orig_val = tmp_val = strdupa(val);

    /* try to unroll "nested" macros */
    do {
        new_val = do_expand_value(expanded_val, size, tmp_val, ht, ht_global);
        if (new_val != tmp_val)
            tmp_val = strdupa(new_val);
        n++;
    } while (n < 5 && strchr(tmp_val, '%') != NULL);

    if (orig_val != tmp_val) {    /* expanded? */
        n_snprintf(expanded_val, size, "%s", tmp_val);
        val = expanded_val;
    }

    return val;
}


static int expand_section_vars(tn_hash *ht, tn_hash *ht_global)
{
    const char *val;
    char expanded_val[PATH_MAX];
    tn_array *keys, *vals;
    int i, j, rc = 1;

    keys = n_hash_keys(ht);

    for (i=0; i<n_array_size(keys); i++) {
        const char *key = n_array_nth(keys, i);
        struct copt *opt;

        if ((opt = n_hash_get(ht, key)) == NULL)
            continue;

        val = expand_value(expanded_val, sizeof(expanded_val), opt->val, ht, ht_global);
        if (val != opt->val) {
            n_cfree(&opt->val);
            opt->val = n_strdup(val);
        }

        if (opt->vals == NULL)
            continue;

        vals = n_array_clone(opt->vals);
        for (j=0; j < n_array_size(opt->vals); j++) {
            const char *v = n_array_nth(opt->vals, j);
            val = expand_value(expanded_val, sizeof(expanded_val), v, ht, ht_global);
            n_array_push(vals, n_strdup(val));
        }
        n_array_free(opt->vals);
        opt->vals = vals;
    }

    n_array_free(keys);
    return rc;
}

static int poldek_conf_postsetup(tn_hash *ht)
{
    tn_hash *ht_global = NULL;
    int i, j, nerr = 0;

    ht_global = poldek_conf_get_section(ht, global_tag);
    expand_section_vars(ht_global, NULL);

    i = 0;
    while (sections[i].name) {
        if (n_str_ne(sections[i].name, global_tag)) {
            tn_array *list = poldek_conf_get_sections(ht, sections[i].name);
            if (list)
                for (j=0; j < n_array_size(list); j++) {
                    tn_hash *htsect = n_array_nth(list, j);
                    expand_section_vars(htsect, ht_global);
                    if (!verify_section(&sections[i], htsect))
                        nerr++;
                }
        }
        i++;
    }

    return nerr == 0;
}

#define ADD_PARAM_VALIDATE  (1 << 0)
#define ADD_PARAM_OVERWRITE (1 << 1)
#define ADD_PARAM_FOREIGN   (1 << 2)
#define ADD_PARAM_STRICT    (1 << 3)

static int verify_param_presence(tn_hash *ht_sect, const char *section,
                                 const char *name,
                                 const struct poldek_conf_tag *tag,
                                 unsigned flags,
                                 const char *filemark)

{
    int gotit = 0, overwrite = (flags & ADD_PARAM_OVERWRITE), rc = 1;

    if (n_hash_exists(ht_sect, name)) {
        struct copt *opt = n_hash_get(ht_sect, name);
        gotit = (opt->val != NULL);
    }

    if (!gotit)
        return rc;

    if (overwrite || (tag->flags & CONF_TYPE_F_MULTI_EXCL)) {
        if (!overwrite || poldek_VERBOSE > 1)
            logn(LOGWARN, _("%s: %s::%s redefined"), filemark, section, name);
        n_hash_remove(ht_sect, name);

    } else if ((tag->flags & CONF_TYPE_F_MULTI) == 0) {
        logn(LOGWARN, _("%s: multiple '%s' not allowed"), filemark, name);
        rc = 0;
    }

    return rc;
}

static int add_param(tn_hash *ht_sect, const char *section,
                     char *name, char *value, unsigned flags,
                     const char *path, int nline)
{
    char *val, expanded_val[PATH_MAX], filemark[512];
    const struct poldek_conf_section *sect;
    const struct poldek_conf_tag *tag;
    struct copt *opt;
    int tagindex, validate, overwrite;

    tag = NULL;
    if ((flags & ADD_PARAM_FOREIGN) == 0) { /* replace '_' and '-' with ' ' */
        if (*name != '_') {          /* user defined macro */
            char *p = name + 1;
            while (*p) {                /* backward compat */
                if (*p == '_' || *p == '-')
                    *p = ' ';
                p++;
            }
        }
    }

    validate = (flags & ADD_PARAM_VALIDATE);

    if (path)
        n_snprintf(filemark, sizeof(filemark), "%s:%d", path, nline);
    else
        n_snprintf(filemark, sizeof(filemark), "config");

    if ((tagindex = find_tag(section, name, &sect)) == -1) {
        if (*name == '_')       /* internal or _macro */
            validate = 0;

        if (!validate) {
            unknown_tag.name = name;
            tag = &unknown_tag;

        } else if (flags & ADD_PARAM_STRICT) {
            logn(LOGERR, _("%s: unknown option '%s::%s'"), filemark,
                 section, name);
            return 0;

        } else {
            logn(LOGWARN, _("%s: unknown option '%s::%s'"), filemark,
                 section, name);
            return 0;
        }
    }

    if (!tag)
        tag = &sect->tags[tagindex];

    msgn_i(3, 2, "%s::%s = %s", section, name, value);

    if (tag->flags & CONF_TYPE_F_ALIAS) {
        int n = tagindex;
        char *p = NULL;
        while (n > 0) {
            n--;
            if ((sect->tags[n].flags & CONF_TYPE_F_ALIAS) == 0) {
                msg(5, "alias %s -> %s\n", name, sect->tags[n].name);
                p = name = sect->tags[n].name;
                tag = &sect->tags[n];
                break;
            }
        }
        if (p == NULL) {
            logn(LOGERR, "%s: wrong alias (internal error)", name);
            n_assert(0);
        }

    }

    if ((tag->flags & CONF_TYPE_F_MULTI) == 0) {
        flags |= ADD_PARAM_OVERWRITE;
    }

    overwrite = (flags & ADD_PARAM_OVERWRITE);

    if (!verify_param_presence(ht_sect, section, name, tag, flags, filemark))
        return 0;

    if (tag->flags & CONF_TYPE_F_LIST)
        return parse_val_list(ht_sect, name, value,
                              (tag->flags & CONF_TYPE_F_PATH) ? " \t,:" : " \t,",
                              path, nline);

    val = parse_val(value, path, nline);
    //printf("Aname = %s, v = %s\n", name, val);
    if (val == NULL && *name == '_')           /* a macro */
        val = "";

    if (val == NULL) {
        logn(LOGERR, _("%s: invalid value of '%s::%s'"), filemark, section, name);
        return 0;
    }

    if ((tag->flags & CONF_TYPE_ENUM)) {
        int n = 0, valid = 0;
        while (tag->enums[n]) {
            if (strcmp(tag->enums[n++], val) == 0) {
                valid = 1;
                break;
            }
        }

        if (!valid) {
            logn(LOGWARN, _("%s: invalid value '%s' of '%s::%s'"), filemark,
                 val, section, name);
            return 0;
        }
    }

    if (n_hash_exists(ht_sect, name)) {
        opt = n_hash_get(ht_sect, name);

    } else {
        opt = copt_new(name);
        n_hash_insert(ht_sect, opt->name, opt);
    }

    if (tag->flags & CONF_TYPE_F_ENV)
        val = (char*)poldek_util_expand_env_vars(expanded_val, sizeof(expanded_val),
                                                 val);

    if (opt->val == NULL) {
        opt->val = n_strdup(val);
        DBGF("ADD %p %s -> %s\n", ht_sect, name, val);

    } else if (overwrite || (tag->flags & CONF_TYPE_F_MULTI_EXCL)) {
        n_assert(0);  /* verify_param_presence() should catch this */
        free(opt->val);
        opt->val = n_strdup(val);

    } else if ((tag->flags & CONF_TYPE_F_MULTI) == 0) {
        n_assert(0);  /* verify_param_presence() should catch this */
        //logn(LOGWARN, _("%s: multiple '%s' not allowed"), filemark, name);
        return 0;

    } else if (opt->vals != NULL) {
        DBGF("ADD %p %s -> %s\n", ht_sect, name, val);
        n_array_push(opt->vals, n_strdup(val));

    } else if (opt->vals == NULL) {
        DBGF("ADD %p %s -> %s\n", ht_sect, name, val);
        opt->vals = n_array_new(2, free, (tn_fn_cmp)strcmp);
        /* put ALL opts to opt->vals */
        n_array_push(opt->vals, n_strdup(opt->val));
        n_array_push(opt->vals, n_strdup(val));
        opt->flags |= COPT_MULTIPLE;
    }

    return 1;
}

static struct afile *afile_new(struct vfile *vf, const char *path,
                               const char *section_to_load)
{
    struct afile *af;
    int len;

    len  = strlen(path) + 1;
    af = n_malloc(sizeof(*af) + len);

    af->vf = vf;
    af->section_to_load = NULL;
    if (section_to_load)
        af->section_to_load = n_strdup(section_to_load);

    memcpy(af->path, path, len);
    return af;
}

static void afile_close(struct afile *af)
{
    vfile_close(af->vf);
    af->vf = NULL;
    n_cfree(&af->section_to_load);
    *af->path = '\0';
    free(af);
}

/* the path of an included file or directory, resolved against the directory
   of the including file when it is relative */
static const char *include_path(char *buf, size_t size, const char *path,
                                const char *parent_path)
{
    char *s;
    int n;

    if (*path == '/' || parent_path == NULL || strrchr(parent_path, '/') == NULL)
        return path;

    n = n_snprintf(buf, size, "%s", parent_path);
    s = strrchr(buf, '/');
    n_assert(s);

    n_snprintf(s + 1, size - n, "%s", path);
    return buf;
}

static struct afile *afile_open(const char *path, const char *parent_path,
                                const char *section_to_load)
{
    char incpath[PATH_MAX];
    struct afile  *af = NULL;
    struct vfile  *vf;

    path = include_path(incpath, sizeof(incpath), path, parent_path);

    if (parent_path)  /* included file */
        msgn(3, "-- %s --", path);

    /* a configuration is a local file: no fetching, and no cached copy of a
       path that used to be remote can shadow it */
    if ((vf = vfile_open(path, VFT_TRURLIO,
                         VFM_RO | VFM_UNCOMPR | VFM_NOEMPTY)) == NULL)
        return NULL;

    af = afile_new(vf, path, section_to_load);
    return af;
}

/*
  %include path
  %include_<section_name> path -> load only <section_name>

  arg is the rest of the line after the directive; sectnam is not NULL only
  for %include, which is the one taking a section name
*/
static char *prepare_include_path(char *path, size_t size, char *arg,
                                  char **sectnam,
                                  tn_hash *ht, tn_hash *ht_global)
{
    char expenval[PATH_MAX], expval[PATH_MAX], *p = arg;

    if (sectnam) {
        *sectnam = NULL;
        if (*p == '_') {
            p++;
            *sectnam = p;
            while (!isspace(*p))
                p++;
            *p = '\0';
            p++;
        }
    }

    p = n_str_strip_ws(p);

    if (strchr(p, '%'))
        p = (char*)expand_value(expval, sizeof(expval), p, ht, ht_global);

    if (strchr(p, '$'))
        p = (char*)poldek_util_expand_env_vars(expenval, sizeof(expenval), p);

    if (p)
        n_snprintf(path, size, "%s", p);

    return *path != '\0' ? path : NULL;
}

static tn_hash *open_section_ht(tn_hash *htconf,
                                const struct poldek_conf_section *sect,
                                const char *sectnam, const char *path, int nline)
{
    tn_array *arr_sect;
    tn_hash  *ht_sect = NULL;

    arr_sect = n_hash_get(htconf, sectnam);
    DBGF("[%s] sect=%p, is_multi=%d\n", sectnam, sect,
         sect ? sect->is_multi : -1);
    msgn(3, " [%s]", sectnam);

    if (arr_sect) {
        if (sect && sect->is_multi == 0) {
            ht_sect = n_array_nth(arr_sect, 0);

        } else {
            ht_sect = n_hash_new(11, (tn_fn_free)copt_free);
            n_hash_ctl(ht_sect, TN_HASH_NOCPKEY);
            n_array_push(arr_sect, ht_sect);
        }

    } else {
        ht_sect = n_hash_new(11, (tn_fn_free)copt_free);
        n_hash_ctl(ht_sect, TN_HASH_NOCPKEY);

        arr_sect = n_array_new(4, (tn_fn_free)n_hash_free, NULL);
        n_array_push(arr_sect, ht_sect);
        n_hash_insert(htconf, sectnam, arr_sect);
    }

    if (ht_sect && path && !n_hash_exists(ht_sect, "__file__line")) {
        char filemark[PATH_MAX];
        struct copt *opt;

        int n = n_snprintf(filemark, sizeof(filemark), "%s", path);
        opt = copt_new("__file");
        opt->val = n_strdup(filemark);
        n_hash_insert(ht_sect, opt->name, opt);

        n_snprintf(&filemark[n], sizeof(filemark) - n, ":%d", nline);
        opt = copt_new("__file__line");
        opt->val = n_strdup(filemark);
        n_hash_insert(ht_sect, opt->name, opt);
    }

    section_set_name(ht_sect, sectnam);

    return ht_sect;
}

void *poldek_conf_add_section(tn_hash *htconf, const char *name)
{
    const struct poldek_conf_section *sect = NULL;
    tn_hash                          *ht_sect = NULL;

    if ((sect = find_section(name)) == NULL) {
        logn(LOGERR, _("'%s': invalid section name"), name);
        return NULL;
    }

    ht_sect = open_section_ht(htconf, sect, name, NULL, -1);
    return ht_sect;
}

int poldek_conf_add_to_section(void *sect, const char *akey, const char *aval)
{
    const char *name;
    char *key, *val;
    tn_hash  *ht_sect = sect;

    n_strdupap(akey, &key);
    n_strdupap(aval, &val);
    name = poldek_conf_get(ht_sect, "__section_name", NULL);
    if (name == NULL)
        name = global_tag;

    return add_param(ht_sect, name, key, val, ADD_PARAM_VALIDATE, NULL, -1);
}

int poldek_conf_set(tn_hash *ht_sect, const char *akey, const char *aval)
{
    return poldek_conf_add_to_section(ht_sect, akey, aval);
}

const char *conf__expand_value(char *dest, size_t size,
                               const tn_hash *htconf,
                               const tn_hash *ht, const char *val)
{
    tn_hash *ht_global = NULL;

    if (htconf)
        ht_global = poldek_conf_get_section(htconf, "global");

    return do_expand_value(dest, size, val, (tn_hash *)ht, ht_global);
}

static int is_macro_key(const char *key)
{
    if (key == NULL || *key != '_')
        return 0;

    /* metadata keys (including __dirname) are not macros */
    if (strncmp(key, "__", 2) == 0)
        return 0;

    return 1;
}

static int is_metadata_key(const char *key)
{
    return key && strncmp(key, "__", 2) == 0;
}

static void fprint_param(FILE *file, const char *key, const struct copt *opt)
{
    int i;

    if (opt->flags & COPT_MULTIPLE) {
        fprintf(file, "%s =", key);
        for (i = 0; i < n_array_size(opt->vals); i++)
            fprintf(file, "%s%s", i > 0 ? "," : " ",
                    (const char *)n_array_nth(opt->vals, i));
        fputc('\n', file);

    } else {
        fprintf(file, "%s = %s\n", key, opt->val);
    }
}

int conf__save(const tn_hash *htconf, const char *path,
               const char *header)
{
    tn_array *sect_keys, *opt_keys;
    tn_hash *ht_global;
    FILE *file = NULL;
    char tmp[PATH_MAX];
    int fd, i, j, k;
    int has_global_opts = 0;
    int rc = 0;

    /* the file keeps mkstemp()'s 0600: a saved configuration may carry a
       repository URL with a password in it */
    n_snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path);
    fd = mkstemp(tmp);
    if (fd < 0) {
        logn(LOGERR, _("%s: mkstemp: %m"), tmp);
        return 0;
    }

    if ((file = fdopen(fd, "w")) == NULL) {
        logn(LOGERR, _("%s: fdopen: %m"), tmp);
        close(fd);
        unlink(tmp);
        return 0;
    }

    if (header)
        fputs(header, file);

    ht_global = poldek_conf_get_section(htconf, "global");
    if (ht_global) {
        opt_keys = n_hash_keys(ht_global);
        n_array_sort(opt_keys);

        /* macros/global vars are written before any section */
        for (i = 0; i < n_array_size(opt_keys); i++) {
            const char *key = n_array_nth(opt_keys, i);
            struct copt *opt = n_hash_get(ht_global, key);

            if (opt == NULL || opt->val == NULL)
                continue;

            if (is_macro_key(key))
                fprint_param(file, key, opt);
        }

        /* regular global options need an explicit [global] section */
        for (i = 0; i < n_array_size(opt_keys); i++) {
            const char *key = n_array_nth(opt_keys, i);
            struct copt *opt = n_hash_get(ht_global, key);

            if (opt == NULL || opt->val == NULL)
                continue;

            if (!is_macro_key(key) && !is_metadata_key(key)) {
                has_global_opts = 1;
                break;
            }
        }

        if (has_global_opts) {
            fprintf(file, "\n[global]\n");
            for (i = 0; i < n_array_size(opt_keys); i++) {
                const char *key = n_array_nth(opt_keys, i);
                struct copt *opt = n_hash_get(ht_global, key);

                if (opt == NULL || opt->val == NULL)
                    continue;

                if (!is_macro_key(key) && !is_metadata_key(key))
                    fprint_param(file, key, opt);
            }
        }

        n_array_free(opt_keys);
    }

    sect_keys = n_hash_keys(htconf);
    n_array_sort(sect_keys);
    for (i = 0; i < n_array_size(sect_keys); i++) {
        const char *sectname = n_array_nth(sect_keys, i);
        tn_array *sect_list;

        if (strcmp(sectname, "global") == 0)
            continue;

        sect_list = n_hash_get(htconf, sectname);
        for (j = 0; j < n_array_size(sect_list); j++) {
            tn_hash *ht = n_array_nth(sect_list, j);

            fprintf(file, "\n[%s]\n", sectname);
            opt_keys = n_hash_keys(ht);
            n_array_sort(opt_keys);
            for (k = 0; k < n_array_size(opt_keys); k++) {
                const char *key = n_array_nth(opt_keys, k);
                struct copt *opt = n_hash_get(ht, key);

                if (opt == NULL || opt->val == NULL)
                    continue;

                if (is_metadata_key(key))
                    continue;

                fprint_param(file, key, opt);
            }
            n_array_free(opt_keys);
        }
    }
    n_array_free(sect_keys);

    fflush(file);
    fsync(fileno(file));

    if (fclose(file) != 0) {
        file = NULL;
        logn(LOGERR, _("%s: write error: %m"), tmp);
        goto l_end;
    }
    file = NULL;

    if (rename(tmp, path) != 0) {
        logn(LOGERR, _("%s: %m"), path);
        goto l_end;
    }

    rc = 1;

l_end:

    if (rc == 0)
        unlink(tmp);

    return rc;
}

/* a fresh configuration with a single, empty `sectnam` section */
tn_hash *conf__new_htconf(const char *sectnam)
{
    tn_hash   *ht, *ht_sect;
    tn_array  *arr_sect;

    if (!sectnam)
        sectnam = global_tag;

    ht = n_hash_new(24, (tn_fn_free)n_array_free);

    arr_sect = n_array_new(4, (tn_fn_free)n_hash_free, NULL);

    ht_sect = n_hash_new(12, (tn_fn_free)copt_free);
    n_array_push(arr_sect, ht_sect);

    n_hash_insert(ht, sectnam, arr_sect);
    section_set_name(ht_sect, sectnam);
    return ht;
}

static int split_option_line(char *line, char **name, char **value,
                             const char *path, int nline)
{
    char *p, *q;

    p = line;
    while (isspace(*p))
        p++;

    if (*p == '#' || *p == '\0')
        return 0;

    *name = p;
    while (isalnum(*p) || *p == '_' || *p == '-' || isspace(*p))
        p++;

    if (p == *name || *p != '=') {
        if (path)
            logn(LOGERR, _("%s:%d: missing '='"), path, nline);
        else
            logn(LOGERR, _("%s: missing '='"), line);
        return 0;
    }

    q = p - 1;
    while (isspace(*q))         /* eat end spaces of name */
        *q-- = '\0';

    *p++ = '\0';
    while (isspace(*p))         /* eat head spaces of value */
        p++;

    if (*p != '\0') {
        char *qq = strchr(p, '\0') - 1;
        while (isspace(*qq))
            *qq-- = '\0';
    }
    *value = p;
    return 1;
}

static
void read_continuation(struct afile *af, char *buf, int size, int *nline)
{
    while (1) {
        char *q;
        int sizeleft;

        q = strrchr(buf, '\0'); /* eat trailing ws */
        n_assert(q);
        q--;

        while (isspace(*q))
            *q-- = '\0';

        if (*q != '\\')     /* not continuation? */
            break;

        *q = '\0';

        sizeleft = size - (q - buf) - 1;
        if (sizeleft > 0) {
            if (!n_stream_gets(af->vf->vf_tnstream, q, sizeleft))
                break;
        } else {
            break;
        }

        (*nline)++;
    }
}

static tn_array *include_files(const char *path)
{
    tn_array *configs = NULL;
    struct stat st;

    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
        configs = poldek_util_readdir(path, "*.conf");
        if (configs == NULL)
            logn(LOGERR, "%s: %m", path);
    } else {
        configs = n_array_new(2, free, (tn_fn_cmp)strcmp);
        n_array_push(configs, n_strdup(path));
    }

    return configs;
}


/* An ignored include is worth a warning, except in the GLOBALONLY
   pre-pass for the cachedir: the real load that follows reports it */
static void warn_include_ignored(const char *path, int nline,
                                 const char *tag, unsigned flags)
{
    if ((flags & POLDEK_LDCONF_QUIET) == 0)
        logn(LOGWARN, _("%s:%d: %s is ignored"), path, nline, tag);
}


static tn_hash *do_ldconf(tn_hash *af_htconf,
                   const char *path, const char *parent_path,
                   const char *section_to_load, unsigned flags)
{
    struct    afile *af;
    int       nline = 0, is_err = 0;
    tn_hash   *ht, *ht_sect; /*, *ht_global_sect;*/
    char      buf[PATH_MAX], *sectnam, *dn;
    int       validate = 1;
    unsigned  addparam_flags = 0;

    if (flags & POLDEK_LDCONF_FOREIGN) {
        validate = 0;
        addparam_flags |= ADD_PARAM_FOREIGN;
    }

    if (flags & POLDEK_LDCONF_NOVALIDATE)
        validate = 0;

    if (flags & POLDEK_LDCONF_STRICT)
        addparam_flags |= ADD_PARAM_STRICT;

    if (n_hash_exists(af_htconf, path)) {
        logn(LOGERR, "%s: included twice", path);
        return NULL;
    }

    sectnam = (char*)global_tag;
    ht = conf__new_htconf(global_tag);
    ht_sect = poldek_conf_get_section(ht, global_tag);
    //ht_global_sect = ht_sect; // unused

    // set __dirname macro
    n_snprintf(buf, sizeof(buf), "%s", path);
    dn = n_dirname(buf);
    if (dn)
        poldek_conf_set(ht_sect, "__dirname", dn);

    af = afile_open(path, parent_path, section_to_load);
    if (af == NULL) {
        is_err = 1;
        goto l_end;
    }

    if (n_hash_exists(af_htconf, af->path))
        logn(LOGERR, "%s: included twice", af->path);
    else
        n_hash_insert(af_htconf, af->path, NULL);

    while (n_stream_gets(af->vf->vf_tnstream, buf, sizeof(buf) - 1)) {
        char *name, *value, *line;

        nline++;
        line = n_str_strip_ws(buf);
        if (*line == '#' || *line == '\0')
            continue;

        /* %include [SECTION] <file|directory>, and %includedir as its older
           name: a directory includes every *.conf file in it */
        if (strncmp(line, include_tag, sizeof(include_tag) - 1) == 0) {
            int is_includedir = strncmp(line, includedir_tag,
                                        sizeof(includedir_tag) - 1) == 0;
            const char *tag = is_includedir ? includedir_tag : include_tag;
            size_t taglen = is_includedir ? sizeof(includedir_tag) - 1
                                          : sizeof(include_tag) - 1;
            char *section = NULL, ipath[PATH_MAX], rpath[PATH_MAX];

            if (flags & POLDEK_LDCONF_NOINCLUDE) {
                warn_include_ignored(af->path, nline, tag, flags);
                continue;
            }

            if (is_includedir)
                msgn(2, _("%s is deprecated, use %s"), includedir_tag,
                     include_tag);

            line = prepare_include_path(ipath, sizeof(ipath), line + taglen,
                                        is_includedir ? NULL : &section,
                                        ht_sect, ht);

            if (line == NULL) {
                logn(LOGERR, _("%s:%d: wrong %s"), af->path, nline, tag);
                is_err = 1;
                goto l_end;
            }

            if (vf_url_type(line) != VFURL_PATH) {
                logn(LOGERR, _("%s:%d: a remote configuration is not included; "
                               "use 'poldek repo add %s'"), af->path, nline,
                     line);
                is_err = 1;
                goto l_end;
            }

            line = (char*)include_path(rpath, sizeof(rpath), line, af->path);
            DBGF("include %s\n", line);

            tn_array *configs = include_files(line);
            if (configs) {
                for (int i=0; i < n_array_size(configs); i++) {
                    char *cpath = n_array_nth(configs, i);

                    if (!do_ldconf(af_htconf, cpath, af->path, section, flags)) {
                        n_array_free(configs);
                        is_err = 1;
                        goto l_end;
                    }
                }
                n_array_free(configs);
            }

            continue;
        }

        read_continuation(af, buf, sizeof(buf), &nline);

        if (*line == '%') {        /* unknown directive */
            if (flags & POLDEK_LDCONF_STRICT) {
                logn(LOGERR, _("%s:%d: unknown directive '%s'"),
                     af->path, nline, line);
                is_err++;
                goto l_end;
            }
            continue;
        }

        if (*line == '[') {        /* section */
            const struct poldek_conf_section *sect = NULL;

            line++;
            name = line;

            while (isalnum(*line) || *line == '-')
                line++;
            *line = '\0';

            if (validate && (sect = find_section(name)) == NULL) {
                logn(LOGERR, _("%s:%d: '%s': invalid section name"),
                     af->path, nline, name);
                is_err++;
                goto l_end;
            }

            n_strdupap(name, &sectnam);
            if (af->section_to_load == NULL || n_str_eq(af->section_to_load, sectnam))
                ht_sect = open_section_ht(ht, sect, sectnam, af->path, nline);
            else
                ht_sect = NULL;
            continue;
        }

        if (!split_option_line(line, &name, &value, af->path, nline))
            goto l_end;

        if (ht_sect) {
            int add_rc;

            addparam_flags |= (validate ? ADD_PARAM_VALIDATE : 0);
            add_rc = add_param(ht_sect, sectnam, name, value, addparam_flags,
                               af->path, nline);
            if ((flags & POLDEK_LDCONF_STRICT) && !add_rc) {
                is_err++;
                goto l_end;
            }
        } else
            msgn(1, _("%s: skipped %s::%s"), af->path, sectnam, name);
    }


    msgn(3, _("-- %s EOF --"), af->path);


    if (ht) {
        if (!poldek_conf_postsetup(ht)) {
            DBGF("ERR %s\n", af->path);
            is_err = 1;
            goto l_end;
        }

        n_hash_replace(af_htconf, af->path, ht);
        DBGF("Loaded %s %p\n", af->path, n_hash_get(af_htconf, af->path));
    }

 l_end:
    if (is_err) {
        if (af)
            logn(LOGERR, _("%s: load configuration failed"), path);
        n_hash_free(ht);
        ht = NULL;
    }
    if (af)
        afile_close(af);

    return ht;
}

static void merge_htconf(tn_hash *htconf, tn_hash *ht)
{
    tn_array *keys;
    int i;

    keys = n_hash_keys(ht);

    for (i=0; i < n_array_size(keys); i++) {
        char *key = n_array_nth(keys, i);
        tn_array *arr_sect;

        if (strcmp(key, global_tag) == 0) /* ignore [global] from included files */
            continue;

        if ((arr_sect = n_hash_get(ht, key))) {
            if (!n_hash_exists(htconf, key)) {
                n_hash_insert(htconf, key, n_ref(arr_sect));

            } else {
                tn_array *htconf_arr;

                htconf_arr = n_hash_get(htconf, key);
                while (n_array_size(arr_sect) > 0)
                    n_array_push(htconf_arr, n_array_pop(arr_sect));
            }
        }
    }

    n_array_free(keys);
}

char *poldek_conf_reposd(char *dest, size_t size)
{
    const char *dir;
    char home[512];

    if (poldek__is_in_testing_mode()) {
        if ((dir = getenv_path("POLDEK_TESTING_REPOSDIR")) != NULL) {
            n_snprintf(dest, size, "%s", dir);
            return dest;
        }
    }

    if (geteuid() == 0) {
#ifdef LOCALSTATEDIR
        n_snprintf(dest, size, "%s/lib/%s/repos.d", LOCALSTATEDIR, PACKAGE_NAME);
#else
        n_snprintf(dest, size, "/var/lib/%s/repos.d", PACKAGE_NAME);
#endif
        return dest;
    }

    if ((dir = getenv_path("XDG_CONFIG_HOME")) != NULL) {
        n_snprintf(dest, size, "%s/%s/repos.d", dir, PACKAGE_NAME);
        return dest;
    }

    if (get_homedir(home, sizeof(home))) {
        n_snprintf(dest, size, "%s/.config/%s/repos.d", home, PACKAGE_NAME);
        return dest;
    }

    return NULL;
}

/* Load the *.conf files of the built-in repos.d. The caller decides
   whether they apply (the default configuration only): this function
   does not check the load flags itself, and always skips includes in
   the files it reads. */
static int load_reposd(tn_hash *htconf, tn_hash *af_htconf, unsigned flags)
{
    char reposd[PATH_MAX];
    tn_array *configs;
    int i;

    if (poldek_conf_reposd(reposd, sizeof(reposd)) == NULL)
        return 1;

    if (access(reposd, R_OK | X_OK) != 0) {
        if (errno != ENOENT)
            logn(LOGWARN, _("%s: %m"), reposd);
        return 1;
    }

    configs = poldek_util_readdir(reposd, "*.conf");
    if (configs == NULL) {
        logn(LOGERR, "%s: read directory: %m", reposd);
        return 1;
    }

    for (i = 0; i < n_array_size(configs); i++) {
        const char *cpath = n_array_nth(configs, i);
        tn_hash *ht;

        /* the configuration included this file by itself: its sources are
           already loaded, and loading it twice is an error; the directive
           is not needed, the directory is read here */
        if (n_hash_exists(af_htconf, cpath)) {
            logn(LOGWARN, _("%s: repos.d is read automatically, "
                            "%%include/%%includedir of it is not needed"),
                 cpath);
            continue;
        }

        if (!do_ldconf(af_htconf, cpath, NULL, "source",
                       flags | POLDEK_LDCONF_NOINCLUDE)) {
            n_array_free(configs);
            return 0;
        }

        ht = n_hash_get(af_htconf, cpath);
        if (ht) {
            tn_array *sources = poldek_conf_get_sections(ht, "source");
            if (sources) {
                for (int j = 0; j < n_array_size(sources); j++) {
                    tn_hash *src = n_array_nth(sources, j);
                    poldek_conf_set(src, "__managed", "yes");
                }
            }
            merge_htconf(htconf, ht);
        }
    }

    n_array_free(configs);
    return 1;
}

tn_hash *poldek_conf_addlines(tn_hash *htconf, const char *sectnam,
                              tn_array *lines)
{
    tn_hash *ht_sect = NULL;
    int i;

    if (sectnam == NULL)
        sectnam = global_tag;

    if (htconf == NULL) {
        htconf = conf__new_htconf(sectnam);
        if (htconf == NULL)
            return NULL;
        ht_sect = poldek_conf_get_section(htconf, sectnam);

    } else {
        ht_sect = poldek_conf_get_section(htconf, sectnam);
        if (ht_sect == NULL) {
            logn(LOGERR, "%s: no such configuration section", sectnam);
            return NULL;
        }
    }

    for (i=0; i < n_array_size(lines); i++) {
        const char *line = n_array_nth(lines, i);
        char *tmp, *name, *value;

        n_strdupap(line, &tmp);

        if (split_option_line(tmp, &name, &value, NULL, 0))
            add_param(ht_sect, sectnam, name, value, ADD_PARAM_VALIDATE, NULL, 0);
    }

    return htconf;
}

static int default_config_path(char *path, int size)
{
    char *sysconfdir = "/etc";
    char legacypath[PATH_MAX], home[512];

#ifdef SYSCONFDIR
    if (access(SYSCONFDIR, R_OK) == 0)
        sysconfdir = SYSCONFDIR;
#endif

    if (get_homedir(home, sizeof(home)) != NULL) {
        int n = n_snprintf(path, size, "%s/.poldekrc", home);

        if (access(path, R_OK) == 0)
            return n;
    }

    DBGF("%s\n", sysconfdir);

    n_snprintf(legacypath, sizeof(legacypath), "%s/poldek.conf", sysconfdir);
    if (access(legacypath, R_OK) == 0) {
        logn(LOGNOTICE, _("%s: legacy configuration detected but ignored"),
             legacypath);
    }

    return n_snprintf(path, size, "%s/poldek/poldek.conf", sysconfdir);
}


tn_hash *poldek_conf_load(const char *path, unsigned flags)
{
    tn_hash   *af_htconf, *htconf = NULL;
    //const char *section_to_load = NULL;
    char confpath[PATH_MAX];
    int is_default_path = (path == NULL);

    if (path == NULL && (flags & POLDEK_LDCONF_FOREIGN) == 0) {
        *confpath = '\0';
        default_config_path(confpath, sizeof(confpath));
        n_assert(*confpath != '\0');
        path = confpath;

        /* avoid error messages with NOVALIDATE (lib_init's default config preload) */
        if ((flags & POLDEK_LDCONF_NOVALIDATE) && access(path, R_OK) != 0)
            return NULL;

    }
    n_assert(path);

    /* a remote configuration is added once with `repo add`, not read here */
    if (vf_url_type(path) != VFURL_PATH) {
        logn(LOGERR, _("%s: a remote configuration is not read; use "
                       "'poldek repo add %s'"), path, path);
        return NULL;
    }

    if (flags & POLDEK_LDCONF_GLOBALONLY) {
        //section_to_load = global_tag;
        flags |= POLDEK_LDCONF_NOINCLUDE | POLDEK_LDCONF_QUIET;
    }

    af_htconf = n_hash_new(23, (tn_fn_free)n_hash_free);

    if (do_ldconf(af_htconf, path, NULL, NULL, flags) == NULL) {
        n_hash_free(af_htconf);
        return NULL;
    }

    htconf = n_hash_get(af_htconf, path);

    /* move non "global" sections from included files to main htconf */
    if (htconf && (flags & POLDEK_LDCONF_NOINCLUDE) == 0) {
        tn_array *paths;
        int i;

        paths = n_hash_keys(af_htconf);
        DBGF("htconf %s %p\n", path, htconf);

        for (i=0; i < n_array_size(paths); i++) {
            char *apath = n_array_nth(paths, i);
            tn_hash  *ht;

            if (strcmp(path, apath) == 0) /* skip main config */
                continue;

            if ((ht = n_hash_get(af_htconf, apath)))
                merge_htconf(htconf, ht);
        }

        n_array_free(paths);

        /* repos.d belongs to the default configuration only: a foreign
           or global-only pre-pass load must not pick it up */
        if (is_default_path && (flags & POLDEK_LDCONF_FOREIGN) == 0) {
            if (!load_reposd(htconf, af_htconf, flags)) {
                n_hash_free(af_htconf);
                return NULL;
            }
        }
    }

    DBGF("ret htconf %s %p\n", path, htconf);
    if (htconf)
        htconf = n_ref(htconf);

    n_hash_free(af_htconf);
    return htconf;
}

tn_array *poldek_conf_get_sections(const tn_hash *htconf, const char *name)
{
    return n_hash_get(htconf, name);
}

tn_hash *poldek_conf_get_section(const tn_hash *htconf, const char *name)
{
    tn_array *arr_sect;

    if ((arr_sect = n_hash_get(htconf, name)))
        return n_array_nth(arr_sect, 0);

    return NULL;
}

static struct copt *do_conf_get(const tn_hash *htconf, const char *name)
{
    struct copt *opt;
    char fc = '_', tc = ' ';    /* from char, to char */
    const char *name1;

    n_assert(htconf);

    name1 = name + 1;           /* first '_' is allowed */
    if (strchr(name1, ' ')) {
        fc = ' ';
        tc = '_';
    }

    if ((opt = n_hash_get(htconf, name)) == NULL && strchr(name1, fc)) {
        char *s, *p;

        n_strdupap(name, &s);
        p = s + 1;              /* skip first char */
        while (*p) {
            if (*p == fc)
                *p = tc;
            p++;
        }
        opt = n_hash_get(htconf, s);
        DBGF("[%s], [%s] %p\n", name, s, opt);
    }

    return opt;
}


const char *poldek_conf_get(const tn_hash *htconf, const char *name,
                            int *is_multi)
{
    struct copt *opt;
    char *v = NULL;

    if (is_multi)
        *is_multi = 0;

    if ((opt = do_conf_get(htconf, name))) {
        v = opt->val;
        if (is_multi)
            *is_multi = (opt->flags & COPT_MULTIPLE);
    }

    return v;
}

int poldek_conf_get_int(const tn_hash *htconf, const char *name, int default_v)
{
    const char *vs;
    int  v;

    if ((vs = poldek_conf_get(htconf, name, NULL)) == NULL)
        return default_v;

    if (sscanf(vs, "%d", &v) != 1) {
        logn(LOGERR, _("invalid value ('%s') of integer option '%s'"), vs, name);
        v = default_v;
    }

    return v;
}


int poldek_conf_get_bool(const tn_hash *htconf, const char *name, int default_v)
{
    const char *v;
    int b;

    if ((v = poldek_conf_get(htconf, name, NULL)) == NULL)
        return default_v;

    if ((b = poldek_util_parse_bool(v)) < 0) {
        logn(LOGERR, _("invalid value ('%s') of boolean option '%s'"), v, name);
        b = default_v;
    }

    return b;
}

int poldek_conf_get_bool3(const tn_hash *htconf, const char *name, int default_v)
{
    const char *v;
    int b;

    if ((v = poldek_conf_get(htconf, name, NULL)) == NULL)
        return default_v;

    if ((b = poldek_util_parse_bool3(v)) < 0) {
        logn(LOGERR, _("invalid value ('%s') of option '%s'"), v, name);
        b = default_v;
    }

    return b;
}


tn_array *poldek_conf_get_multi(const tn_hash *htconf, const char *name)
{
    struct copt *opt;
    tn_array    *list = NULL;

    if ((opt = do_conf_get(htconf, name)) == NULL)
        return NULL;

    if (opt->vals && n_array_size(opt->vals))
        list = n_ref(opt->vals);

    else {
        list = n_array_new(2, NULL, (tn_fn_cmp)strcmp);
        n_array_push(list, opt->val);
    }

    return list;
}
