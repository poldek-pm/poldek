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
#include "i18n.h"
#include "log.h"
#include "conf.h"
#include "misc.h"

#define TYPE_STR      (1 << 0)
#define TYPE_BOOL     (1 << 1)
#define TYPE_LIST     (1 << 2)
#define TYPE_MULTI    (1 << 3)
#define TYPE_ENUM     (1 << 4)

#define TYPE_F_ENV      (1 << 10)
#define TYPE_F_REQUIRED (1 << 11)
#define TYPE_F_ALIAS    (1 << 12)

struct tag {
    char      *name;
    unsigned  flags;
    char      *enums[8];
};

static struct tag unknown_tag = {
    NULL, TYPE_STR | TYPE_MULTI | TYPE_F_ENV, { 0 },
};

static struct tag global_tags[] = {
    { "source",        TYPE_STR | TYPE_MULTI | TYPE_F_ENV, { 0 } },
    { "source?*",      TYPE_STR | TYPE_F_ENV, { 0 } },
    { "prefix?*",      TYPE_STR | TYPE_F_ENV, { 0 } },
    { "cachedir",      TYPE_STR | TYPE_F_ENV, { 0 } },
    
    { "ftp_http_get",  TYPE_STR , { 0 } }, /* obsolete */
    { "ftp_get",       TYPE_STR , { 0 } }, /* obsolete */
    { "http_get",      TYPE_STR , { 0 } }, /* obsolete */
    { "https_get",     TYPE_STR , { 0 } }, /* obsolete */
    { "rsync_get",     TYPE_STR , { 0 } }, /* obsolete */
    { "cdrom_get",     TYPE_STR , { 0 } }, /* obsolete */

    { "ignore_req",    TYPE_STR | TYPE_MULTI , { 0 } },
    { "ignore_pkg",    TYPE_STR | TYPE_MULTI , { 0 } },

    { "rpmdef",        TYPE_STR | TYPE_MULTI | TYPE_F_ENV, { 0 } },
    { "rpm_install_opt",  TYPE_STR , { 0 } },
    { "rpm_uninstall_opt",  TYPE_STR , { 0 } },

    { "follow",         TYPE_BOOL , { 0 } },
    { "greedy",         TYPE_BOOL , { 0 } }, 
    { "use_sudo",       TYPE_BOOL , { 0 } },
    { "mercy",          TYPE_BOOL , { 0 } },
    { "default_fetcher", TYPE_STR | TYPE_MULTI , { 0 } },
    { "proxy",          TYPE_STR | TYPE_MULTI, { 0 } },
    { "hold",           TYPE_STR | TYPE_LIST | TYPE_MULTI , { 0 } },
    { "ignore",         TYPE_STR | TYPE_LIST | TYPE_MULTI , { 0 } },
    { "keep_downloads", TYPE_BOOL , { 0 } },
    { "confirm_installs", TYPE_BOOL , { 0 } }, /* backward compat */
    { "confirm_installation", TYPE_BOOL , { 0 } },
    { "confirm_removal", TYPE_BOOL , { 0 } },
    { "choose_equivalents_manually", TYPE_BOOL , { 0 } },
    { "particle_install", TYPE_BOOL, { 0 } },
    { "unique_package_names", TYPE_BOOL, { 0 } },
    { "ftp_sysuser_as_anon_passwd", TYPE_BOOL , { 0 } },
    {  NULL,           0, { 0 } }, 
};

static struct tag fetcher_tags[] = {
    { "name",       TYPE_STR,  { 0 } },
    { "proto",      TYPE_STR | TYPE_F_REQUIRED, { 0 } },
    { "cmd",        TYPE_STR | TYPE_F_ENV | TYPE_F_REQUIRED, { 0 } },
};

static struct tag proxy_tags[] = {
    { "name",       TYPE_STR,  { 0 } },
    { "proto",      TYPE_STR, { 0 } },
    { "url",        TYPE_STR | TYPE_F_ENV, { 0 } },
};


static struct tag source_tags[] = {
    { "name",        TYPE_STR, { 0 } },
    { "url",         TYPE_STR | TYPE_F_ENV | TYPE_F_REQUIRED, { 0 } },
    { "path",         TYPE_STR | TYPE_F_ENV | TYPE_F_ALIAS, { 0 } }, /* alias for url */
    { "prefix",      TYPE_STR | TYPE_F_ENV, { 0 } },
    { "pri",         TYPE_STR , { 0 } },
    { "dscr",        TYPE_STR | TYPE_F_ENV, { 0 } },
    { "type",        TYPE_STR , { 0 } },
    { "noauto",      TYPE_BOOL, { 0 } },
    { "noautoup",    TYPE_BOOL, { 0 } },
    { "unique_package_names", TYPE_BOOL, { 0 } },
    { "douniq",       TYPE_BOOL, { 0 } },
    { "signed",      TYPE_BOOL, { 0 } },
    { "hold",           TYPE_STR | TYPE_LIST | TYPE_MULTI , { 0 } },
    { "ignore",         TYPE_STR | TYPE_LIST | TYPE_MULTI , { 0 } },
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

static
struct copt *copt_link(struct copt *opt)
{
    opt->_refcnt++;
    return opt;
}

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
int getvlist(tn_hash *ht, char *name, char *vstr, const char *path, int nline)
{
    const char **v, **p;
    struct copt *opt;


    path = path; nline = nline;
    
    p = v = n_str_tokl(vstr, " \t,");
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
            //printf("Lname = %s, v = %s\n", name, *p);
            opt->val = n_strdup(*p);
        } else {
            if (n_array_size(opt->vals) == 0) {
                opt->flags |= COPT_MULTIPLE;
                n_array_push(opt->vals, n_strdup(opt->val)); 
            }
            n_array_push(opt->vals, n_strdup(*p));
            //printf("Lname = %s, v = %s\n", name, *p);
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
const struct tag *find_tag(const char *sectname, const char *key) 
{
    int i = 0;
    struct tag   *tags = NULL;
    const struct section *sect;

    if ((sect = find_section(sectname)) == NULL)
        return NULL;
    
    tags = sect->tags;
    i = 0;
    while (tags[i].name) {
        if (strcmp(tags[i].name, key) == 0)
            return &tags[i];
        
        if (fnmatch(tags[i].name, key, 0) == 0) 
            return &tags[i];
        i++;
    }
    return NULL;
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
        if (tags[i].flags & TYPE_F_REQUIRED)
            if (n_hash_get(ht, tags[i].name) == NULL) {
                struct copt *opt;
                struct tag  *t = NULL;

                if (tags[i + 1].name)
                    t = &tags[i + 1];
                
                if (t && (t->flags & TYPE_F_ALIAS) &&
                    (opt = n_hash_get(ht, t->name))) {
                    
                    n_hash_insert(ht, tags[i].name, copt_link(opt));
                    
                } else {
                    logn(LOGERR, "%s: missing required '%s'", sect->name,
                         tags[i].name);
                    nerr++;
                }
            }
        i++;
    }

    return nerr == 0;
}


static const char *do_expand_value(const char *val, tn_hash *ht, tn_hash *ht_global)
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
    
    if (val != new_val)
        val = n_strdup(new_val);
    return val;
}


static int expand_section_vars(tn_hash *ht, tn_hash *ht_global) /*  */
{
    const char *val;
    tn_array *keys, *vals;
    int i, j, rc = 1;

    keys = n_hash_keys(ht);
    for (i=0; i<n_array_size(keys); i++) {
        const char *key = n_array_nth(keys, i);
        struct copt *opt;
        
        if ((opt = n_hash_get(ht, key)) == NULL)
            continue;

        val = do_expand_value(opt->val, ht, ht_global);
        if (val != opt->val) {
            free(opt->val);
            opt->val = (char*)val;
        }
        
        if (opt->vals == NULL)
            continue;
        
        vals = n_array_clone(opt->vals);
        for (j=0; j < n_array_size(opt->vals); j++) {
            const char *v = n_array_nth(opt->vals, j);
            val = do_expand_value(v, ht, ht_global);
            if (val == v)
                val = n_strdup(v);
            
            n_array_push(vals, (char*)val);
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

    ht_global = poldek_conf_get_section_ht(ht, "global");
    expand_section_vars(ht_global, NULL);

    i = 0;
    while (sections[i].name) {
        if (strcmp(sections[i].name, "global")  != 0) {
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
    const struct tag *tag;
    struct copt *opt;

    
    if ((tag = find_tag(section, name)) == NULL) {
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
    
        
    msgn(3, "%s::%s = %s", section, name, value);
    
    
    if (tag->flags & TYPE_LIST) 
        return getvlist(ht_sect, name, value, path, nline);
        
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
    struct vfile *vf;
    char path[0];
};

static 
struct afile *afile_new(struct vfile *vf, const char *path)
{
    struct afile *af;
    int len;
    
    len  = strlen(path) + 1;
    af = n_malloc(sizeof(*af) + len);
    
    af->vf = vf;
    memcpy(af->path, path, len);
    return af;
}
 
void afile_free(struct afile *af)
{
    vfile_close(af->vf);
    af->vf = NULL;
    *af->path = '\0';
    free(af);
}

static struct afile *open_afile(tn_array *af_stack, const char *path) 
{
    char incpath[PATH_MAX];
    struct afile *af = NULL;
    struct vfile *vf;
    int is_local;
    
    if (n_array_size(af_stack))
        af = n_array_nth(af_stack, n_array_size(af_stack) - 1);

    is_local = (vf_url_type(path) == VFURL_PATH);
    
    if (af && is_local && *path != '/' && strrchr(af->path, '/') != NULL) {
        char *s;
        int n;
        
        n = n_snprintf(incpath, sizeof(incpath), "%s", af->path);
        s = strrchr(incpath, '/');
        n_assert(s);
        
        n_snprintf(s + 1, sizeof(incpath) - n, "%s", path);
        path = incpath;
    }

    if (af)                     /* included file */
        msgn_i(0, 2 * (n_array_size(af_stack) - 1), "** %%include %s", path);
    
    if ((vf = vfile_open(path, VFT_TRURLIO, VFM_RO)) == NULL) 
        return NULL;

    af = afile_new(vf, path);
    n_array_push(af_stack, af);
    return af;
}


static struct afile *close_afile(tn_array *af_stack) 
{
    struct afile *af;
    
    if (n_array_size(af_stack)) {
        af = n_array_pop(af_stack);
        afile_free(af);
    }

    af = NULL;
    if (n_array_size(af_stack))
        af = n_array_nth(af_stack, n_array_size(af_stack) - 1);

    return af;
}

    

tn_hash *poldek_ldconf(const char *path, unsigned flags) 
{
    struct   afile *af;
    tn_array *af_stack;
    int      nline = 0, is_err = 0;
    tn_hash  *ht, *ht_sect;
    tn_array *arr_sect;
    char     buf[1024], *section, *include = "%include";
    int      validate = 1;

    if (flags & POLDEK_LDCONF_NOVRFY)
        validate = 0;

    af_stack = n_array_new(4, (tn_fn_free)afile_free, NULL);
    if ((af = open_afile(af_stack, path)) == NULL) {
        is_err = 1;
        goto l_end;
    }

    ht = n_hash_new(23, (tn_fn_free)n_array_free);
    arr_sect = n_array_new(4, (tn_fn_free)n_hash_free, NULL);
    
    ht_sect = n_hash_new(11, (tn_fn_free)copt_free);
    n_array_push(arr_sect, ht_sect);
    section = "global";

    n_hash_insert(ht, section, arr_sect);

 l_loop:
    
    while (n_stream_gets(af->vf->vf_tnstream, buf, sizeof(buf) - 1)) {
        char *p = buf;
        char *name, expanded_val[PATH_MAX];

        p = buf;
        while (isspace(*p))
            p++;

        if (*p == '#' || *p == '\0') {
            nline++;
            continue;
        }

        
        if (strncmp(p, include, strlen(include)) == 0) {
            struct afile *tmp;
            
            p += strlen(include);
            p = eat_wws(p);
            p = (char*)expand_env_vars(expanded_val, sizeof(expanded_val), p);
            
            if (*p == '\0') {
                logn(LOGERR, _("%s:%d: wrong %%include param"), af->path, nline);
                is_err = 1;
                goto l_end;
            }

            
            if ((tmp = open_afile(af_stack, p))) {
                af = tmp;
                
            } else {
                is_err = 1;
                goto l_end;
            }
            continue;
        }
        
        while (1) {
            nline++;
            p = strrchr(buf, '\0'); /* eat trailing ws */
            n_assert(p);
            p--;
            while (isspace(*p))
                *p-- = '\0';
            
            if (*p != '\\')
                break;
            
            *p = '\0';
            n_stream_gets(af->vf->vf_tnstream, p, sizeof(buf) - (p - buf) - 1);
        }
        
        name = p = buf;

        if (*p == '[') {
            const struct section *sect;
            tn_array *arr_sect;
                
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
            
            arr_sect = n_hash_get(ht, name);

            if (arr_sect) {
                int len;
                
                if (sect->is_multi == 0) {
                    ht_sect = n_array_nth(arr_sect, 0);
                    
                } else {
                    ht_sect = n_hash_new(11, (tn_fn_free)copt_free);
                    n_hash_ctl(ht_sect, TN_HASH_NOCPKEY);
                    n_array_push(arr_sect, ht_sect);
                }

                len = strlen(name) + 1;
                section = alloca(len);
                memcpy(section, name, len);
                
            } else {
                section = n_strdup(name);
                //printf("section %s\n", section);
            
                ht_sect = n_hash_new(11, (tn_fn_free)copt_free);
                n_hash_ctl(ht_sect, TN_HASH_NOCPKEY);
                
                arr_sect = n_array_new(4, (tn_fn_free)n_hash_free, NULL);
                n_array_push(arr_sect, ht_sect);
                n_hash_insert(ht, section, arr_sect);
            }
            
            continue;
        }
        
        while (isalnum(*p) || *p == '_' || *p == '-')
            p++;
        
        if (*p != '=' && !isspace(*p)) {
            logn(LOGERR, _("%s:%d: '%s': invalid value name"), af->path, nline,
                 name);
            continue;
        }
        
        while (isspace(*p))
            *p++ = '\0';
        
        if (*p == '=') {
            *p++ = '\0';
            while (isspace(*p))
                p++;

        } else {
            logn(LOGERR, _("%s:%d: missing '='"), af->path, nline);
            continue;
        }

        if (*p != '\0') {
            char *q = strchr(p, '\0') - 1;
            while (isspace(*q))
                *q-- = '\0';
        }
        
        add_param(ht_sect, section, name, p, validate, af->path, nline);
    }

    if ((af = close_afile(af_stack)))
        goto l_loop;

    
    if (ht)
        if (!poldek_conf_postsetup(ht))
            is_err = 1;

 l_end:
    if (af_stack)
        n_array_free(af_stack);

    if (is_err) {
        logn(LOGERR, _("%s: load configuration failed"), path);
        n_hash_free(ht);
        ht = NULL;
    }
    
    return ht;
}


tn_hash *poldek_ldconf_default(void)
{
    char *homedir;
    char *etcpath = "/etc/poldek.conf";
    
    if ((homedir = getenv("HOME")) != NULL) {
        char path[PATH_MAX];
        
        snprintf(path, sizeof(path), "%s/.poldekrc", homedir);
        if (access(path, R_OK) == 0)
            return poldek_ldconf(path, 0);
    }
    
    if (access(etcpath, R_OK) == 0)
        return poldek_ldconf(etcpath, 0);

    return NULL;
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



char *poldek_conf_get(const tn_hash *htconf, const char *name, int *is_multi)
{
    struct copt *opt;
    char *v = NULL;

    if (is_multi)
        *is_multi = 0;
    
    if (htconf && (opt = n_hash_get(htconf, name))) {
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
    
    logn(LOGERR, _("invalid value ('%s') for option '%s'"), v, name);

    return default_v;
}


tn_array *poldek_conf_get_multi(const tn_hash *htconf, const char *name)
{
    struct copt *opt;
    tn_array    *list = NULL;

    if ((opt = n_hash_get(htconf, name)) == NULL)
        return NULL;

    if (opt->vals)
        list = n_ref(opt->vals);
    
    else {
        list = n_array_new(2, NULL, (tn_fn_cmp)strcmp);
        n_array_push(list, opt->val);
    }
    
    return list;
}



