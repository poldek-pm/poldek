/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <dirent.h>

#include <trurl/nhash.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>
#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
#include <trurl/nstream.h>
#include <vfile/vfile.h>

#define ENABLE_TRACE 0
#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "conf.h"
#include "misc.h"
#include "poldek_util.h"
#include "conf_intern.h"

#define POLDEK_LDCONF_APTSOURCES  (1 << 15) 
 
static const char *global_tag = "global";
static const char *include_tag = "%include";
static const char *includedir_tag = "%includedir";

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

static void load_apt_sources_list(tn_hash *htconf, const char *path);

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
    
    if (opt->flags & COPT_MULTIPLE)
        n_array_free(opt->vals);
    else
        n_cfree(&opt->val);

    free(opt);
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

    if (opt->vals == NULL) 
        opt->vals = n_array_new(2, free, (tn_fn_cmp)strcmp);
    
    while (*p) {
        if (opt->val == NULL) {
            DBGF("%s[list] += %s\n", name, *p);
            opt->val = n_strdup(*p);
            
        } else {
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
        n = n_snprintf(dest, size, *tl);
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
        
        n_snprintf(val, v_len + 1, v);
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

static char *eat_wws(char *s) 
{
    char *p;
    
    while (isspace(*s))
        s++;
    
    p = strrchr(s, '\0'); /* eat trailing ws */
    n_assert(p);
    p--;
    while (isspace(*p))
        *p-- = '\0';

    return s;
}

static int verify_section(const struct poldek_conf_section *sect, tn_hash *ht)
{
    int i = 0, nerr = 0;
    struct poldek_conf_tag *tags;
    struct copt *fl;

    fl = n_hash_get(ht, "__file__line");
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


static
const char *do_expand_value(char *expanded_val, size_t size, const char *val,
                            tn_hash *ht, tn_hash *ht_global)
{
    const char *new_val;
    char expand_val[PATH_MAX], expand_val2[PATH_MAX];

    if (strchr(val, '%') == NULL)
        return val;
    
    new_val = expand_macros(expand_val, sizeof(expand_val), val, ht);
    if (ht_global && strchr(new_val, '%')) {
        new_val = expand_macros(expand_val2, sizeof(expand_val2),
                                new_val, ht_global);
    }
    
    if (val != new_val) {
        n_snprintf(expanded_val, size, "%s", new_val);
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

        val = do_expand_value(expanded_val, sizeof(expanded_val), opt->val,
                              ht, ht_global);
        if (val != opt->val) {
            n_cfree(&opt->val);
            opt->val = n_strdup(val);
        }
        
        if (opt->vals == NULL)
            continue;
        
        vals = n_array_clone(opt->vals);
        for (j=0; j < n_array_size(opt->vals); j++) {
            const char *v = n_array_nth(opt->vals, j);
            val = do_expand_value(expanded_val, sizeof(expanded_val), v,
                                  ht, ht_global);
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
            logn(LOGWARN, _("%s %s::%s redefined"), filemark, section, name);
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
    if ((flags & ADD_PARAM_FOREIGN) == 0) /* replace '_' and '-' with ' ' */
        if (*name != '_') {          /* user defined macro */
            char *p = name + 1;
            while (*p) {                /* backward compat */
                if (*p == '_' || *p == '-')
                    *p = ' ';
                p++;
            }
        }

    overwrite = (flags & ADD_PARAM_OVERWRITE);
    validate = (flags & ADD_PARAM_VALIDATE);

    if (path)
        n_snprintf(filemark, sizeof(filemark), "%s:%d:", path, nline);
    else
        n_snprintf(filemark, sizeof(filemark), "config:");
               
    if ((tagindex = find_tag(section, name, &sect)) == -1) {
        if (*name == '_')       /* internal or _macro */
            validate = 0;
        
        if (!validate) {
            unknown_tag.name = name;
            tag = &unknown_tag;
                
        } else {
            logn(LOGWARN, _("%s unknown option '%s::%s'"), filemark,
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
        logn(LOGERR, _("%s invalid value of '%s::%s'"), filemark, section, name);
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
            logn(LOGWARN, _("%s invalid value '%s' of '%s::%s'"), filemark,
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


static struct afile *afile_open(const char *path, const char *parent_path,
                                const char *section_to_load, int update)
{
    const char    *ppath;
    struct afile  *af = NULL;
    struct vfile  *vf;
    int           is_local, is_parent_remote = 0, vfmode;

    ppath = parent_path;        /* just for short */
    
    is_local = (vf_url_type(path) == VFURL_PATH);
    if (ppath)
        is_parent_remote = (vf_url_type(ppath) != VFURL_PATH);

    if (ppath) {
        int prepend = 0;

        /* relative: %include foo.conf */
        if (is_local && *path != '/' && strrchr(ppath, '/') != NULL)
            prepend = 1;

        /* parent is remote -> included file must be remote too */
        if (is_parent_remote && is_local)
            prepend = 1;

        if (prepend) {
            char incpath[PATH_MAX], *s;
            int n;

            n = n_snprintf(incpath, sizeof(incpath), "%s", ppath);
            s = strrchr(incpath, '/');
            n_assert(s);
        
            n_snprintf(s + 1, sizeof(incpath) - n, "%s", path);
            path = incpath;
        }
    }
    
    if (ppath)  /* included file */
        msgn(3, "-- %s --", path);

    
    vfmode = VFM_RO | VFM_UNCOMPR | VFM_NOEMPTY;
    if (!update)
        vfmode |= VFM_CACHE;
    else 
        vfmode |= VFM_NODEL | VFM_CACHE_NODEL;

    if (update) DBGF("UPDATING %s...\n", path);

    if ((vf = vfile_open(path, VFT_TRURLIO, vfmode)) == NULL) 
        return NULL;

    af = afile_new(vf, path, section_to_load);
    return af;
}

/*
  %include path|url
  %include_<section_name> path|url -> load only <section_name>
*/
static char *prepare_include_path(const char *tag, char *path, size_t size,
                                  char *line, char **sectnam,
                                  tn_hash *ht, tn_hash *ht_global)
{
    char expenval[PATH_MAX], expval[PATH_MAX], *p;

    if (n_str_eq(tag, include_tag))
        *sectnam = NULL;
    else                        
        n_assert(sectnam == NULL); /* irrelevant for non %include */
    
    p = line + strlen(tag);
    if (n_str_eq(tag, include_tag) && *p == '_') {            
        p++;
        *sectnam = p;
        while (!isspace(*p))
            p++;
        *p = '\0';
        p++;
    }
    
    p = eat_wws(p);

    if (strchr(p, '%'))
        p = (char*)do_expand_value(expval, sizeof(expval), p, ht, ht_global);
    
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

        n_snprintf(filemark, sizeof(filemark), "%s:%d:", path, nline);
        opt = copt_new("__file__line");
        opt->val = n_strdup(filemark);
        n_hash_insert(ht_sect, opt->name, opt);
    }
    
    if (!n_hash_exists(ht_sect, "__section_name")) { 
        struct copt *opt = copt_new("__section_name");
        opt->val = n_strdup(sectnam);
        n_hash_insert(ht_sect, opt->name, opt);
    }

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

static tn_hash *new_htconf(const char *sectnam, tn_hash **ht_sect_ptr)
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
    if (ht_sect_ptr)
        *ht_sect_ptr = ht_sect;
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
        char *q = strchr(p, '\0') - 1;
        while (isspace(*q))
            *q-- = '\0';
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

static tn_array *includedir_files(const char *dirpath, const char *ppath) 
{
    tn_array       *configs = NULL;
    struct dirent  *ent;
    DIR            *dir;
    char           *sep = "/", tmpath[PATH_MAX];

    
    if (*dirpath != '/' && strrchr(ppath, '/') != NULL) {
        char *s;
        int n;

        n = n_snprintf(tmpath, sizeof(tmpath), "%s", ppath);
        s = strrchr(tmpath, '/');
        n_assert(s);
        
        n_snprintf(s + 1, sizeof(tmpath) - n, "%s", dirpath);
        dirpath = tmpath;
    }
    
    if ((dir = opendir(dirpath)) == NULL) {
        logn(LOGERR, "%%includedir %s: %m", dirpath);
        return NULL;
    }

    configs = n_array_new(32, free, (tn_fn_cmp)strcmp);

    while ((ent = readdir(dir))) {
        char path[PATH_MAX];
        int n;
        
        if (fnmatch("*.conf", ent->d_name, 0) != 0) 
            continue;

        n = n_snprintf(path, sizeof(path), "%s%s%s", dirpath, sep, ent->d_name);
        if (access(path, R_OK) == 0)
            n_array_push(configs, n_strdupl(path, n));
    }
    closedir(dir);
    
    n_array_sort(configs);
    if (n_array_size(configs) == 0)
        n_array_cfree(&configs);
    
    return configs;
}
        

static tn_hash *do_ldconf(tn_hash *af_htconf,
                   const char *path, const char *parent_path,
                   const char *section_to_load, unsigned flags)
{
    struct    afile *af;
    int       nline = 0, is_err = 0;
    tn_hash   *ht, *ht_sect, *ht_global_sect;
    char      buf[PATH_MAX], *sectnam, *dn;
    int       validate = 1, update = 0;
    unsigned  addparam_flags = 0;
    
    if (flags & POLDEK_LDCONF_FOREIGN) {
        validate = 0;
        addparam_flags |= ADD_PARAM_FOREIGN;
    }

    if (flags & POLDEK_LDCONF_NOVALIDATE)
        validate = 0;

    if (flags & POLDEK_LDCONF_UPDATE)
        update = 1;

    if (n_hash_exists(af_htconf, path)) {
        logn(LOGERR, "%s: included twice", path);
        return NULL;
    }

    sectnam = (char*)global_tag;
    ht = new_htconf(global_tag, &ht_sect);
    ht_global_sect = ht_sect;

    // set __dirname macro 
    n_snprintf(buf, sizeof(buf), "%s", path);
    dn = n_dirname(buf);
    if (dn)
        poldek_conf_set(ht_sect, "__dirname", dn);
    
    af = afile_open(path, parent_path, section_to_load, update);
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
        line = eat_wws(buf);
        if (*line == '#' || *line == '\0')
            continue;

        /* %includedir <directory> */
        if (strncmp(line, includedir_tag, strlen(includedir_tag)) == 0) {
            char ipath[PATH_MAX];
            tn_array *configs;
            int i;
            

            if (flags & POLDEK_LDCONF_NOINCLUDE) 
                continue;
            
            line = prepare_include_path(includedir_tag, ipath, sizeof(ipath), line,
                                        NULL, ht_sect, ht);
            
            if (line == NULL) {
                logn(LOGERR, _("%s:%d: wrong %%includedir"), af->path, nline);
                is_err = 1;
                goto l_end;
            }
            DBGF("includedir %s\n", line);
            
            if ((configs = includedir_files(line, af->path)) == NULL)
                continue;

            for (i=0; i < n_array_size(configs); i++) {
                char *path = n_array_nth(configs, i);
                
                if (!do_ldconf(af_htconf, path, af->path, NULL, flags)) {
                    n_array_free(configs);
                    is_err = 1;
                    goto l_end;
                }
            }
            n_array_free(configs);
            continue;
        }

        /* %include <file> */
        if (strncmp(line, include_tag, strlen(include_tag)) == 0) {
            char *section_to_load = NULL, ipath[PATH_MAX];
            
            if (flags & POLDEK_LDCONF_NOINCLUDE) 
                continue;
            
            line = prepare_include_path(include_tag, ipath, sizeof(ipath),
                                        line, &section_to_load, ht_sect, ht);
            
            if (line == NULL) {
                logn(LOGERR, _("%s:%d: wrong %%include"), af->path, nline);
                is_err = 1;
                goto l_end;
            }
            
            if (!do_ldconf(af_htconf, line, af->path, section_to_load, flags)) {
                is_err = 1;
                goto l_end;
            }
            continue;
        }
        
        read_continuation(af, buf, sizeof(buf), &nline);

        if (*line == '%')          /* unknown directive */
            continue;

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
            addparam_flags |= (validate ? ADD_PARAM_VALIDATE : 0);
            add_param(ht_sect, sectnam, name, value, addparam_flags,
                      af->path, nline);
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

tn_hash *poldek_conf_addlines(tn_hash *htconf, const char *sectnam,
                              tn_array *lines)
{
    tn_hash *ht_sect = NULL;
    int i, nerr = 0, htconfown = 0;

    if (sectnam == NULL)
        sectnam = global_tag;
    
    if (htconf == NULL) {
        htconfown = 1;
        htconf = new_htconf(sectnam, &ht_sect);
    
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
        
        if (split_option_line(tmp, &name, &value, NULL, 0)) {
            if (!add_param(ht_sect, sectnam, name, value,
                           ADD_PARAM_VALIDATE | ADD_PARAM_OVERWRITE, NULL, 0))
                nerr++;
        }
    }
    
    return htconf;
}

static int default_config_path(char *path, int size)
{
    char *homedir;
    char *sysconfdir = "/etc";
    char legacypath[PATH_MAX];
    
#ifdef SYSCONFDIR
    if (access(SYSCONFDIR, R_OK) == 0)
        sysconfdir = SYSCONFDIR;
#endif

    if ((homedir = getenv("HOME")) != NULL) {
        int n = n_snprintf(path, size, "%s/.poldekrc", homedir);
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
    const char *section_to_load = NULL;
    char confpath[PATH_MAX];
    
    if (path == NULL && (flags & POLDEK_LDCONF_FOREIGN) == 0) {
        *confpath = '\0';
        default_config_path(confpath, sizeof(confpath));
        n_assert(*confpath != '\0');
        path = confpath;
    }
    n_assert(path);

    if (flags & POLDEK_LDCONF_GLOBALONLY) {
        section_to_load = global_tag;
        flags |= POLDEK_LDCONF_NOINCLUDE;
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
    }


    DBGF("ret htconf %s %p\n", path, htconf);
    if (htconf) {
        htconf = n_ref(htconf);
        
        if ((flags & (POLDEK_LDCONF_NOINCLUDE | POLDEK_LDCONF_FOREIGN)) == 0) {
            tn_hash *global;
            
            global = poldek_conf_get_section(htconf, "global");
            
            if (poldek_conf_get_bool(global, "load apt sources list", 0))
                flags |= POLDEK_LDCONF_APTSOURCES;
            
            if (flags & POLDEK_LDCONF_APTSOURCES)
                load_apt_sources_list(htconf, "/etc/apt/sources.list");
        }
    }
    
    
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
    int bool;
    
    if ((v = poldek_conf_get(htconf, name, NULL)) == NULL)
        return default_v;

    if ((bool = poldek_util_parse_bool(v)) < 0) {
        logn(LOGERR, _("invalid value ('%s') of boolean option '%s'"), v, name);
        bool = default_v;
    }

    return bool;
}

int poldek_conf_get_bool3(const tn_hash *htconf, const char *name, int default_v)
{
    const char *v;
    int bool;
    
    if ((v = poldek_conf_get(htconf, name, NULL)) == NULL)
        return default_v;

    if ((bool = poldek_util_parse_bool3(v)) < 0) {
        logn(LOGERR, _("invalid value ('%s') of option '%s'"), v, name);
        bool = default_v;
    }

    return bool;
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

static void load_apt_sources_list(tn_hash *htconf, const char *path) 
{
    const struct poldek_conf_section *sect = NULL;
    const char **tl, **tl_save, *sectnam;
    char buf[1024];
    FILE *stream;
    int nline = 0;

    if (access(path, R_OK) != 0)
        return;

    sectnam = "source";
    sect = find_section(sectnam);
    n_assert(sect);
    
    if ((stream = fopen(path, "r")) == NULL) {
        logn(LOGERR, "fopen %s: %m", path);
        return;
    }

    nline = 0;
    while (fgets(buf, sizeof(buf) - 1, stream)) {
        char *p = buf;
        const char *uri = NULL, *distribution = NULL;
        
        nline++;
        
        p = buf;
        while (isspace(*p))
            p++;

        if (*p == '#' || *p == '\0')
            continue;
        
        if (strncasecmp(p, "rpm ", 4) != 0)
            continue;

        p += 4;                 /* skip "rpm " */
        while (isspace(*p))
            p++;

        tl = tl_save = n_str_tokl(p, " \t\n\r");
        uri = *tl;
        if (uri) {
            tl++;
            distribution = *tl;
            tl++;
        }
        
        if (uri && distribution && *tl) {
            while (*tl) {
                char name[PATH_MAX], url[PATH_MAX], pkg_prefix[PATH_MAX];
                const char *component;
                tn_hash *ht_sect;
                
                component = *tl;
                tl++;
                if (*component == '\0')
                    continue;

                n_snprintf(url, sizeof(url), "%s/%s/base/pkglist.%s.bz2",
                           uri, distribution, component);

                n_snprintf(pkg_prefix, sizeof(pkg_prefix), "%s/%s/RPMS.%s",
                           uri, distribution, component);

                n_snprintf(name, sizeof(name), "%s-%s", distribution, component);
                p = name;
                while (*p) {
                    if (*p == '/') *p = '-';
                    else if (!isalnum(*p) && strchr("-+", *p) == NULL) *p = '.';
                    p++;
                }
                
                ht_sect = open_section_ht(htconf, sect, sectnam, path, -1);
                add_param(ht_sect, sectnam, "type", "apt", ADD_PARAM_VALIDATE,
                          path, nline);
                add_param(ht_sect, sectnam, "name", name, ADD_PARAM_VALIDATE,
                          path, nline);
                add_param(ht_sect, sectnam, "url", url, ADD_PARAM_VALIDATE,
                          path, nline);
                add_param(ht_sect, sectnam, "prefix", pkg_prefix,
                          ADD_PARAM_VALIDATE, path, nline);
            }
            n_str_tokl_free(tl_save);
        }
    }
    
    fclose(stream);
}

