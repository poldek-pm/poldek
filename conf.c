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

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fnmatch.h>

#include <trurl/nhash.h>
#include <trurl/narray.h>
#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>
#include <trurl/nassert.h>
#include <trurl/nmalloc.h>
#include <trurl/nstream.h>
#include <vfile/vfile.h>

#define ENABLE_TRACE 0
#include "i18n.h"
#include "log.h"
#include "conf.h"
#include "misc.h"

#define TYPE_STR        (1 << 0)
#define TYPE_BOOL       (1 << 1)
#define TYPE_LIST       (1 << 2)
#define TYPE_PATHLIST   (1 << 3)

#define TYPE_MULTI      (1 << 5)
#define TYPE_MULTI_EXCL (1 << 6)
#define TYPE_ENUM       (1 << 7)

#define TYPE_F_ENV      (1 << 10)
#define TYPE_F_REQUIRED (1 << 11)
#define TYPE_F_ALIAS    (1 << 12)
#define TYPE_F_UNIQUE   (1 << 13)

static const char *global_tag = "global";
static const char *include_tag = "%include";

struct tag {
    char      *name;
    unsigned  flags;
    char      *enums[8];
};

static struct tag unknown_tag = {
    NULL, TYPE_STR | TYPE_F_ENV | TYPE_MULTI_EXCL, { 0 },
};

static struct tag global_tags[] = {
    { "source",        TYPE_STR | TYPE_MULTI | TYPE_F_ENV, { 0 } },
    { "source?*",      TYPE_STR | TYPE_F_ENV, { 0 } },
    { "prefix?*",      TYPE_STR | TYPE_F_ENV, { 0 } },
    { "cachedir",      TYPE_STR | TYPE_F_ENV, { 0 } },
    
    { "ftp http_get",  TYPE_STR , { 0 } }, /* obsolete */
    { "ftp get",       TYPE_STR , { 0 } }, /* obsolete */
    { "http get",      TYPE_STR , { 0 } }, /* obsolete */
    { "https get",     TYPE_STR , { 0 } }, /* obsolete */
    { "rsync get",     TYPE_STR , { 0 } }, /* obsolete */
    { "cdrom get",     TYPE_STR , { 0 } }, /* obsolete */

    { "ignore req",    TYPE_STR | TYPE_MULTI , { 0 } },
    { "ignore pkg",    TYPE_STR | TYPE_MULTI , { 0 } },

    { "rpm",           TYPE_STR | TYPE_F_ENV, { 0 } },
    { "sudo",          TYPE_STR | TYPE_F_ENV, { 0 } },
    { "rpmdef",        TYPE_STR | TYPE_MULTI | TYPE_F_ENV, { 0 } },
    { "rpm install opt",  TYPE_STR , { 0 } },
    { "rpm uninstall opt",  TYPE_STR , { 0 } },

    { "follow",            TYPE_BOOL , { 0 } },
    { "greedy",            TYPE_BOOL , { 0 } },
    { "aggressive greedy", TYPE_BOOL , { 0 } },
    { "use sudo",          TYPE_BOOL , { 0 } },
    { "mercy",          TYPE_BOOL , { 0 } },
    { "default fetcher", TYPE_STR | TYPE_MULTI , { 0 } },
    { "proxy",          TYPE_STR | TYPE_MULTI, { 0 } },
    { "hold",           TYPE_STR | TYPE_LIST | TYPE_MULTI , { 0 } },
    { "ignore",         TYPE_STR | TYPE_LIST | TYPE_MULTI , { 0 } },
    { "keep downloads", TYPE_BOOL , { 0 } },
    { "confirm installation", TYPE_BOOL , { 0 } },
    { "confirm installs", TYPE_BOOL | TYPE_F_ALIAS , { 0 } }, /* backward compat */
    { "confirm removal", TYPE_BOOL , { 0 } },
    { "choose equivalents manually", TYPE_BOOL , { 0 } },
    { "particle install", TYPE_BOOL, { 0 } },
    { "unique package names", TYPE_BOOL, { 0 } },
    { "vfile ftp sysuser as anon passwd", TYPE_BOOL , { 0 } },
    { "ftp sysuser as anon passwd", TYPE_BOOL | TYPE_F_ALIAS, { 0 } },
    { "vfile external compress", TYPE_BOOL , { 0 } },
    { "promoteepoch", TYPE_BOOL, { 0 } },
    { "default index type", TYPE_STR, { 0 } },
    { "autoupa", TYPE_BOOL, { 0 } },
    { "exclude path", TYPE_STR | TYPE_PATHLIST | TYPE_MULTI , { 0 } },
    {  NULL,           0, { 0 } }, 
};

static struct tag fetcher_tags[] = {
    { "name",       TYPE_STR,  { 0 } },
    { "proto",      TYPE_STR | TYPE_F_REQUIRED, { 0 } },
    { "cmd",        TYPE_STR | TYPE_F_ENV | TYPE_F_REQUIRED, { 0 } },
    {  NULL,           0, { 0 } }, 
};

static struct tag alias_tags[] = {
    { "name",       TYPE_STR | TYPE_F_REQUIRED,  { 0 } },
    { "cmd",        TYPE_STR | TYPE_F_REQUIRED, { 0 } },
    { "ctx",        TYPE_ENUM, { "none", "installed", "available", "upgradeable", NULL } },
    {  NULL,        0, { 0 } }, 
};

static struct tag proxy_tags[] = {
    { "name",       TYPE_STR,  { 0 } },
    { "proto",      TYPE_STR, { 0 } },
    { "url",        TYPE_STR | TYPE_F_ENV, { 0 } },
    {  NULL,           0, { 0 } }, 
};


static struct tag source_tags[] = {
    { "name",        TYPE_STR, { 0 } },
    { "url",         TYPE_STR | TYPE_F_ENV | TYPE_F_REQUIRED, { 0 } },
    { "path",        TYPE_STR | TYPE_F_ENV | TYPE_F_ALIAS, { 0 } }, /* alias for url */
    { "prefix",      TYPE_STR | TYPE_F_ENV, { 0 } },
    { "pri",         TYPE_STR , { 0 } },
    { "dscr",        TYPE_STR | TYPE_F_ENV, { 0 } },
    { "type",        TYPE_STR , { 0 } },
    { "original type", TYPE_STR , { 0 } },
    { "noauto",      TYPE_BOOL, { 0 } },
    { "noautoup",    TYPE_BOOL, { 0 } },
    { "douniq",      TYPE_BOOL, { 0 } },
    { "unique package names", TYPE_BOOL | TYPE_F_ALIAS, { 0 } },
    { "signed",      TYPE_BOOL, { 0 } },
    { "hold",        TYPE_STR | TYPE_LIST | TYPE_MULTI , { 0 } },
    { "ignore",      TYPE_STR | TYPE_LIST | TYPE_MULTI , { 0 } },
    { "exclude path", TYPE_STR | TYPE_PATHLIST | TYPE_MULTI , { 0 } },
    {  NULL,         0, { 0 } }, 
};

struct section {
    char        *name;
    struct tag  *tags;
    int         is_multi;
};

struct section sections[] = {
    { "global",  global_tags,  0 },
    { "source",  source_tags,  1 },
    { "fetcher", fetcher_tags, 1 },
    { "proxy",   proxy_tags,   1 },
    { "alias",   alias_tags,   1 },
    {  NULL,  NULL, 0 },
};

#define COPT_MULTIPLE (1 << 0)
struct copt {
    unsigned flags;
    
    tn_array *vals;
    char     *val;
    int      _refcnt;
    char     name[0];
};

static
struct copt *copt_new(const char *name)
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

#if 0                           /* unused */
static
struct copt *copt_link(struct copt *opt)
{
    opt->_refcnt++;
    return opt;
}
#endif

static                                                      
void copt_free(struct copt *opt)
{
    if (opt->_refcnt > 0) {
        opt->_refcnt--;
        return;
    }
    
    if (opt->flags & COPT_MULTIPLE)
        n_array_free(opt->vals);
    else
        free(opt->val);

    free(opt);
}

static
int getvlist(tn_hash *ht, char *name, char *vstr, const char *sep,
             const char *path, int nline)
{
    const char **v, **p;
    struct copt *opt;


    path = path; nline = nline;
    if (sep == NULL)
        sep = " \t,";
    
    p = v = n_str_tokl(vstr, sep);
    if (v == NULL)
        return 0;

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

static char *getv(char *vstr, const char *path, int nline) 
{
    char *p, *q;
    
    p = vstr;
    while (isspace(*p))
        p++;
    
    if (*p == '"') {
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

static
const struct section *find_section(const char *name) 
{
    int i = 0;

    while (sections[i].name) {
        if (strcmp(sections[i].name, name) == 0) 
            return &sections[i];
        i++;
    }

    return NULL;
}


static
int find_tag(const char *sectname, const char *key,
             const struct section **sectp) 
{
    int i = 0;
    struct tag   *tags = NULL;
    const struct section *sect;

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

static
const char *expand_vars(char *dest, int size, const char *str,
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


static int verify_section(const struct section *sect, tn_hash *ht) 
{
    int i = 0, nerr = 0;
    struct tag *tags;

    tags = sect->tags;
    
    while (tags[i].name) {
        if (tags[i].flags & TYPE_F_REQUIRED) {
            if (n_hash_get(ht, tags[i].name) == NULL) {
                logn(LOGERR, "%s: missing required '%s'", sect->name,
                     tags[i].name);
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
    
    new_val = expand_vars(expand_val, sizeof(expand_val), val, ht);
    if (ht_global && strchr(new_val, '%')) {
        new_val = expand_vars(expand_val2, sizeof(expand_val2),
                              new_val, ht_global);
    }
    
    if (val != new_val) {
        n_snprintf(expanded_val, size, "%s", new_val);
        val = expanded_val;
    }

    return val;
}


static int expand_section_vars(tn_hash *ht, tn_hash *ht_global) /*  */
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
            free(opt->val);
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

    ht_global = poldek_conf_get_section_ht(ht, global_tag);
    expand_section_vars(ht_global, NULL);

    i = 0;
    while (sections[i].name) {
        if (strcmp(sections[i].name, global_tag)  != 0) {
            tn_array *list = poldek_conf_get_section_arr(ht, sections[i].name);
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


static int add_param(tn_hash *ht_sect, const char *section,
                     char *name, char *value,
                     int validate, 
                     const char *path, int nline)
{
    char *val, expanded_val[PATH_MAX];
    const struct section *sect;
    const struct tag *tag;
    struct copt *opt;
    int tagindex;

    tag = NULL;
    if (*name != '_') {          /* user defined macro */
        char *p = name + 1;
        while (*p) {                /* backward compat */
            if (*p == '_')
                *p = ' ';
            p++;
        }
    }
    
    if ((tagindex = find_tag(section, name, &sect)) == -1) {
        if (*name == '_')
            validate = 0;
        
        if (!validate) {
            unknown_tag.name = name;
            tag = &unknown_tag;
                
        } else {
            logn(LOGWARN, _("%s:%d unknown option '%s::%s'"), path, nline,
                 section, name);
            return 0;
        }
    }
    
    if (!tag)
        tag = &sect->tags[tagindex];
        
    msgn_i(3, 2, "%s::%s = %s", section, name, value);
    
    
    if (tag->flags & (TYPE_LIST | TYPE_PATHLIST)) 
        return getvlist(ht_sect, name, value, 
                        (tag->flags & TYPE_PATHLIST) ? " \t,:" : " \t,",
                        path, nline);
        
    val = getv(value, path, nline);
    //printf("Aname = %s, v = %s\n", name, val);
    
    if (val == NULL) {
        logn(LOGERR, _("%s:%d: invalid value of '%s::%s'"), path, nline,
             section, name);
        return 0;
    }

    if ((tag->flags & TYPE_ENUM)) {
        int n = 0, valid = 0;
        while (tag->enums[n]) {
            if (strcmp(tag->enums[n++], val) == 0) {
                valid = 1;
                break;
            }
        }

        if (!valid) {
            logn(LOGWARN, _("%s:%d invalid value '%s' of '%s::%s'"),
                 path, nline, val, section, name);
            return 0;
        }
    }

    if (tag->flags & TYPE_F_ALIAS) {
        int n = tagindex;
        char *p = NULL;
        while (n > 0) {
            n--;
            if ((sect->tags[n].flags & TYPE_F_ALIAS) == 0) {
                msg(5, "alias %s -> %s\n", name, sect->tags[n].name); 
                p = name = sect->tags[n].name;
                break;
            }
        }
        if (p == NULL) {
            logn(LOGERR, "%s: wrong alias (internal error)", name);
            n_assert(0);
        }
        
    }

    if (n_hash_exists(ht_sect, name)) {
        opt = n_hash_get(ht_sect, name);
        
    } else {
        opt = copt_new(name);
        n_hash_insert(ht_sect, opt->name, opt);
    }

    if (tag->flags & TYPE_F_ENV)
        val = (char*)expand_env_vars(expanded_val, sizeof(expanded_val), val);
    
    if (opt->val == NULL) {
        opt->val = n_strdup(val);
        //printf("ADD %p %s -> %s\n", ht_sect, name, val);

    } else if (tag->flags & TYPE_MULTI_EXCL) {
        logn(LOGWARN, _("%s:%d: %s::%s redefined"), path, nline, section, name);
        free(opt->val);
        opt->val = n_strdup(val);
        
    } else if ((tag->flags & TYPE_MULTI) == 0) {
        logn(LOGWARN, _("%s:%d multiple '%s' not allowed"), path, nline, name);
        return 0;
            
    } else if (opt->vals != NULL) {
        n_array_push(opt->vals, n_strdup(val));
            
    } else if (opt->vals == NULL) {
        opt->vals = n_array_new(2, free, (tn_fn_cmp)strcmp);
        /* put ALL opts to opt->vals */
        n_array_push(opt->vals, n_strdup(opt->val)); 
        n_array_push(opt->vals, n_strdup(val));
        opt->flags |= COPT_MULTIPLE;
    }

    return 1;
}


struct afile {
    struct vfile  *vf;
    char          *sectnam_inc;
    char          path[0];
};

static 
struct afile *afile_new(struct vfile *vf, const char *path,
                        const char *sectnam_inc)
{
    struct afile *af;
    int len;
    
    len  = strlen(path) + 1;
    af = n_malloc(sizeof(*af) + len);
    
    af->vf = vf;
    af->sectnam_inc = NULL;
    if (sectnam_inc)
        af->sectnam_inc = n_strdup(sectnam_inc);
        
    memcpy(af->path, path, len);
    return af;
}

static
void afile_close(struct afile *af)
{
    vfile_close(af->vf);
    af->vf = NULL;
    
    if (af->sectnam_inc)
        n_cfree(&af->sectnam_inc);
    
    *af->path = '\0';
    free(af);
}


static
struct afile *afile_open(const char *path, const char *parent_path, 
                         const char *sectnam_inc, int update)
{
    char          incpath[PATH_MAX];
    const char    *ppath;
    struct afile  *af = NULL;
    struct vfile  *vf;
    int           is_local, vfmode;

    ppath = parent_path;
    is_local = (vf_url_type(path) == VFURL_PATH);
    
    if (ppath && is_local && *path != '/' && strrchr(ppath, '/') != NULL) {
        char *s;
        int n;
        
        n = n_snprintf(incpath, sizeof(incpath), "%s", ppath);
        s = strrchr(incpath, '/');
        n_assert(s);
        
        n_snprintf(s + 1, sizeof(incpath) - n, "%s", path);
        path = incpath;
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

    af = afile_new(vf, path, sectnam_inc);
    return af;
}

static
char *include_path(char *path, size_t size,
                   char *line, char **sectnam, tn_hash *ht, tn_hash *ht_global)
{
    char expenv_val[PATH_MAX], expval[PATH_MAX], *p;
    
    *sectnam = NULL;
    p = line + strlen(include_tag);
    if (*p == '_') {
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
        p = (char*)expand_env_vars(expenv_val, sizeof(expenv_val), p);

    n_snprintf(path, size, "%s", p);
    return path;
}

static
tn_hash *open_section_ht(tn_hash *htconf, const struct section *sect, 
                         const char *sectnam)
{
    tn_array *arr_sect;
    tn_hash  *ht_sect = NULL;
    
    arr_sect = n_hash_get(htconf, sectnam);
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
        n_hash_insert(htconf, n_strdup(sectnam), arr_sect);
    }

    return ht_sect;
}

static
tn_hash *do_ldconf(tn_hash *af_htconf,
                   const char *path, const char *parent_path,
                   const char *sectnam_inc, unsigned flags)
{
    struct    afile *af;
    int       nline = 0, is_err = 0;
    tn_hash   *ht, *ht_sect;
    tn_array  *arr_sect;
    char      buf[1024], *sectnam;
    int       validate = 1, update = 0;
    
    
    if (flags & POLDEK_LDCONF_FOREIGN)
        validate = 0;

    if (flags & POLDEK_LDCONF_UPDATE)
        update = 1;

    if (n_hash_exists(af_htconf, path)) {
        logn(LOGERR, "%s: included twice", path);
        return NULL;
    }

    sectnam = (char*)global_tag;

    ht = n_hash_new(23, (tn_fn_free)n_array_free);
    arr_sect = n_array_new(4, (tn_fn_free)n_hash_free, NULL);
    
    ht_sect = n_hash_new(11, (tn_fn_free)copt_free);
    n_array_push(arr_sect, ht_sect);
    
    n_hash_insert(ht, sectnam, arr_sect);

    af = afile_open(path, parent_path, sectnam_inc, update);
    if (af == NULL) {
        is_err = 1;
        goto l_end;
    }

    n_hash_insert(af_htconf, af->path, NULL);
    
    while (n_stream_gets(af->vf->vf_tnstream, buf, sizeof(buf) - 1)) {
        char *name, *p;
        
        p = buf;
        while (isspace(*p))
            p++;
        
        if (*p == '#' || *p == '\0') {
            nline++;
            continue;
        }

        
        if (strncmp(p, include_tag, strlen(include_tag)) == 0) {
            tn_hash *inc_ht;
            char   *inc_sectnam = NULL, ipath[PATH_MAX];
            
            
            p = include_path(ipath, sizeof(ipath), p, &inc_sectnam,
                             ht_sect, ht);
            if (p == NULL || *p == '\0') {
                logn(LOGERR, _("%s:%d: wrong %%include"), af->path, nline);
                is_err = 1;
                goto l_end;
            }
            
            DBGF("open %s %s, i %s\n", p, sectnam, inc_sectnam);
            inc_ht = do_ldconf(af_htconf, p, af->path, inc_sectnam, flags);
            if (inc_ht == NULL) {
                is_err = 1;
                goto l_end;
            }
            continue;
        }
        
        while (1) {
            char *q;
            nline++;
            q = strrchr(buf, '\0'); /* eat trailing ws */
            n_assert(q);
            q--;
            while (isspace(*q))
                *q-- = '\0';
            
            if (*q != '\\')
                break;
            
            *q = '\0';
            n_stream_gets(af->vf->vf_tnstream, q, sizeof(buf) - (q - buf) - 1);
        }


        if (*p == '%')
            continue;

        if (*p == '[') {
            const struct section *sect = NULL;
            int  len;
            
            p++;
            name = p;
            
            while (isalnum(*p) || *p == '-')
                p++;
            *p = '\0';

            if (validate && (sect = find_section(name)) == NULL) {
                logn(LOGERR, _("%s:%d: '%s': invalid section name"),
                     af->path, nline, name);
                is_err++;
                goto l_end;
            }
            
            len = strlen(name) + 1;
            sectnam = alloca(len);
            memcpy(sectnam, name, len);
            
            if (af->sectnam_inc == NULL || strcmp(af->sectnam_inc, sectnam) == 0)
                ht_sect = open_section_ht(ht, sect, sectnam);
            else
                ht_sect = NULL;
            continue;
        }

        name = p;
        while (isalnum(*p) || *p == '_' || *p == '-' || isspace(*p))
            p++;
        
        if (p == name || *p != '=') {
            logn(LOGERR, _("%s:%d: missing '='"), af->path, nline);
            //printf("%s\n", buf);
            is_err++;
            goto l_end;
            
        } else {
            char *q = p - 1;
            while (isspace(*q))
                *q-- = '\0';
        }

        *p++ = '\0';
        while (isspace(*p))
            p++;

        if (*p != '\0') {
            char *q = strchr(p, '\0') - 1;
            while (isspace(*q))
                *q-- = '\0';
        }
        
        if (ht_sect)
            add_param(ht_sect, sectnam, name, p, validate, af->path, nline);
        else
            msgn(1, "%s: skipped %s::%s", af->path, sectnam, name);
    }
    

    msgn(3, "-- %s EOF --", af->path);
    

    if (ht) {
        if (!poldek_conf_postsetup(ht)) {
            DBGF("ERR %s\n", af->path);
            is_err = 1;
        } else {
            n_hash_replace(af_htconf, af->path, ht);
            DBGF("Loaded %s %p\n", af->path, n_hash_get(af_htconf, af->path));
        }
        
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

        if (strcmp(key, global_tag) == 0)
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


tn_hash *poldek_conf_load(const char *path, unsigned flags) 
{
    tn_hash   *af_htconf, *htconf = NULL;

    af_htconf = n_hash_new(23, (tn_fn_free)n_hash_free);

    if (do_ldconf(af_htconf, path, NULL, NULL, flags) != NULL) {
        tn_array *paths;
        int i;
        
        htconf = n_hash_get(af_htconf, path);
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
    if (htconf)
        htconf = n_ref(htconf);
    
    n_hash_free(af_htconf);
    return htconf;
}


tn_hash *poldek_conf_loadefault(unsigned flags)
{
    char *homedir;
    char *sysconfdir = "/etc";
    char confpath[PATH_MAX], newconfpath[PATH_MAX];
    int  confpath_exists = 0, newconfpath_exists = 0;
    
#ifdef SYSCONFDIR
    if (access(SYSCONFDIR, R_OK) == 0)
        sysconfdir = SYSCONFDIR;
#endif

    if ((homedir = getenv("HOME")) != NULL) {
        char path[PATH_MAX];
        
        snprintf(path, sizeof(path), "%s/.poldekrc", homedir);
        if (access(path, R_OK) == 0)
            return poldek_conf_load(path, flags);
    }
    DBGF("%s\n", sysconfdir);
    
    n_snprintf(confpath, sizeof(confpath), "%s/poldek.conf", sysconfdir);
    confpath_exists = (access(confpath, R_OK) == 0);

    n_snprintf(newconfpath, sizeof(newconfpath), "%s/poldek/poldek.conf",
               sysconfdir);

    newconfpath_exists = (access(newconfpath, R_OK) == 0);

    if (confpath_exists && newconfpath_exists) {
        logn(LOGNOTICE, _("There are two configuration files available, using legacy "
                          "%s (consider removing it)."), confpath);
        return poldek_conf_load(confpath, 0);

    } else if (confpath_exists)
        return poldek_conf_load(confpath, flags);
    
    return poldek_conf_load(newconfpath, flags);
}

tn_array *poldek_conf_get_section_arr(const tn_hash *htconf, const char *name)
{
    return n_hash_get(htconf, name);
}

tn_hash *poldek_conf_get_section_ht(const tn_hash *htconf, const char *name)
{
    tn_array *arr_sect;
    
    if ((arr_sect = n_hash_get(htconf, name)))
        return n_array_nth(arr_sect, 0);
    
    return NULL;
}

static struct copt *do_conf_get(const tn_hash *htconf, const char *name)
{
    struct copt *opt;
    char fc = '_', tc = ' ';
    const char *name1;
    
    n_assert(htconf);

    name1 = name + 1;           /* first '_' char is allowed */
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


char *poldek_conf_get(const tn_hash *htconf, const char *name, int *is_multi)
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
    char *vs;
    int  v;
    
    if ((vs = poldek_conf_get(htconf, name, NULL)) == NULL)
        return default_v;

    if (sscanf(vs, "%d", &v) != 1)
        return default_v;

    return v;
}


int poldek_conf_get_bool(const tn_hash *htconf, const char *name, int default_v)
{
    char *v;
    
    if ((v = poldek_conf_get(htconf, name, NULL)) == NULL)
        return default_v;

    if (strcasecmp(v, "yes") == 0 || strcasecmp(v, "y") == 0 ||
        strcasecmp(v, "true") == 0 || strcasecmp(v, "t") == 0 ||
        strcasecmp(v, "on") == 0 || strcasecmp(v, "enabled") == 0)
        return 1;

    if (strcasecmp(v, "no") == 0 || strcasecmp(v, "n") == 0 ||
        strcasecmp(v, "false") == 0 || strcasecmp(v, "f") == 0 ||
        strcasecmp(v, "off") == 0 || strcasecmp(v, "disabled") == 0)
        return 0;
    
    logn(LOGERR, _("invalid value ('%s') of option '%s'"), v, name);

    return default_v;
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
