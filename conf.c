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

#include "i18n.h"
#include "log.h"
#include "conf.h"
#include "misc.h"

#define TYPE_STR      (1 << 0)
#define TYPE_BOOL     (1 << 1)
#define TYPE_LIST     (1 << 2)
#define TYPE_MULTI    (1 << 3)
#define TYPE_ENUM     (1 << 4)

#define TYPE_W_ENV    (1 << 10)

struct tag {
    char      *name;
    unsigned  flags;
    char      *enums[8];
};

static struct tag global_tags[] = {
    { "source",        TYPE_STR | TYPE_MULTI | TYPE_W_ENV, { 0 } },
    { "source?*",      TYPE_STR | TYPE_W_ENV, { 0 } },
    { "prefix?*",      TYPE_STR | TYPE_W_ENV, { 0 } },
    { "cachedir",      TYPE_STR | TYPE_W_ENV, { 0 } },
    { "ftp_http_get",  TYPE_STR , { 0 } },
    { "ftp_get",       TYPE_STR , { 0 } },
    { "http_get",      TYPE_STR , { 0 } },
    { "https_get",     TYPE_STR , { 0 } },
    { "rsync_get",     TYPE_STR , { 0 } },
    { "cdrom_get",     TYPE_STR , { 0 } },
    { "ssh_get",       TYPE_STR , { 0 } },
    { "ignore_req",    TYPE_STR | TYPE_MULTI , { 0 } },
    { "ignore_pkg",    TYPE_STR | TYPE_MULTI , { 0 } },
    { "rpmdef",        TYPE_STR | TYPE_MULTI | TYPE_W_ENV, { 0 } },
    { "rpm_install_opt",  TYPE_STR , { 0 } },
    { "rpm_uninstall_opt",  TYPE_STR , { 0 } },
    { "follow",         TYPE_BOOL , { 0 } },
    { "greedy",         TYPE_BOOL , { 0 } }, 
    { "use_sudo",       TYPE_BOOL , { 0 } },
    { "mercy",          TYPE_BOOL , { 0 } },
    { "default_fetcher", TYPE_STR | TYPE_LIST | TYPE_MULTI , { 0 } },
    { "hold",           TYPE_STR | TYPE_LIST | TYPE_MULTI , { 0 } },
    { "ignore",         TYPE_STR | TYPE_LIST | TYPE_MULTI , { 0 } },
    { "downloader",     TYPE_STR | TYPE_MULTI | TYPE_W_ENV, { 0 } }, 
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
    { "proto",      TYPE_STR, { 0 } },
    { "cmd",        TYPE_STR | TYPE_W_ENV, { 0 } },
};

static struct tag source_tags[] = {
    { "name",        TYPE_STR, { 0 } },
    { "url",         TYPE_STR | TYPE_W_ENV, { 0 } },
    { "prefix",      TYPE_STR | TYPE_W_ENV, { 0 } },
    { "pri",         TYPE_STR , { 0 } },
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
    {  NULL,  NULL, 0 },
};

#define COPT_MULTIPLE (1 << 0)
struct copt {
    unsigned flags;
    
    tn_array *vals;
    char     *val;
    char     name[0];
};


struct copt *copt_new(const char *name)
{
    struct copt *opt;

    opt = n_malloc(sizeof(*opt) + strlen(name) + 1);
    strcpy(opt->name, name);
    opt->flags = 0;
    opt->val = NULL;
    opt->vals= NULL;
    return opt;
}

static                                                      
void copt_free(struct copt *opt)
{
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
    struct tag     *tags = NULL;
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
        const char *vv, *v, *var;
        char val[256];
        int  v_len;
        
        
        v = *tl;
        DBGF("token: %s\n", *tl);
        tl++;
        
        if (*v != '{') {
            n += n_snprintf(&dest[n], size - n, "%%%s", v);
            continue;
        }
        
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
            return str;
        
        n_snprintf(val, v_len + 1, v);
        DBGF("var %s\n", val);
        
        if ((var = poldek_conf_get(ht, val, NULL)) == NULL) {
            n += n_snprintf(&dest[n], size - n, "%s", v);
            
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


tn_hash *poldek_ldconf(const char *path) 
{
    FILE     *stream, *main_stream;
    int      nline = 0;
    tn_hash  *ht, *ht_sect;
    tn_array *arr_sect;
    char     buf[1024], *section, *include = "%include";
    
    
    if ((stream = fopen(path, "r")) == NULL) {
        logn(LOGERR, "fopen %s: %m", path);
        return NULL;
    }
    main_stream = NULL;

    ht = n_hash_new(23, (tn_fn_free)n_array_free);
    arr_sect = n_array_new(4, (tn_fn_free)n_hash_free, NULL);
    
    ht_sect = n_hash_new(11, (tn_fn_free)copt_free);
    n_array_push(arr_sect, ht_sect);
    section = "global";

    n_hash_insert(ht, section, arr_sect);

 l_loop:
    
    while (fgets(buf, sizeof(buf) - 1, stream)) {
        char *p = buf;
        char *name, *val, expanded_val[PATH_MAX], expanded_val2[PATH_MAX];
        struct copt *opt;
        const struct tag *tag;

        
        p = buf;
        while (isspace(*p))
            p++;

        if (*p == '#' || *p == '\0') {
            nline++;
            continue;
        }

        
        if (strncmp(p, include, strlen(include)) == 0) {
            FILE *st;
            
            p += strlen(include);
            p = eat_wws(p);
            p = (char*)expand_env_vars(expanded_val, sizeof(expanded_val), p);
            
            if ((st = fopen(p, "r")) == NULL) {
                logn(LOGERR, "fopen %s: %m", p);
                return NULL;
            }
            
            main_stream = stream;
            stream = st;
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
            fgets(p, sizeof(buf) - (p - buf) - 1, stream);
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

            
            if ((sect = find_section(name)) == NULL) {
                logn(LOGERR, _("%s:%d: '%s': invalid section name"),
                     path, nline, name);
                
                return NULL;
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
        
        while (isalnum(*p) || *p == '_')
            p++;
        
        if (*p != '=' && !isspace(*p)) {
            logn(LOGERR, _("%s:%d: '%s': invalid parameter"), path, nline, name);
            continue;
        }
        
        while (isspace(*p))
            *p++ = '\0';
        
        if (*p == '=') {
            *p++ = '\0';
            while (isspace(*p))
                p++;

        } else {
            logn(LOGERR, _("%s:%d: missing '='"), path, nline);
            continue;
        }

        if (*p != '\0') {
            char *q = strchr(p, '\0') - 1;
            while (isspace(*q))
                *q-- = '\0';
        }

        if ((tag = find_tag(section, name)) == NULL) {
            logn(LOGWARN, _("%s:%d unknown option '%s:%s'"), path, nline,
                 section, name);
            continue;
        }

        //printf("name = %s, v = %s\n", name, p);
        

        if (tag->flags & TYPE_LIST) {
            getvlist(ht_sect, name, p, path, nline);
            continue;
        }
        
        val = getv(p, path, nline);
        //printf("Aname = %s, v = %s\n", name, val);
        if (val == NULL) {
            logn(LOGERR, _("%s:%d: no value for '%s'"), path, nline, name);
            continue;
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
                logn(LOGWARN, _("%s:%d invalid value '%s' of option '%s'"),
                    path, nline, val, name);
                continue;
            }
            
        }
        
        if (n_hash_exists(ht_sect, name)) {
            opt = n_hash_get(ht_sect, name);
            
        } else {
            opt = copt_new(name);
            n_hash_insert(ht_sect, opt->name, opt);
        }

        if (tag->flags & TYPE_W_ENV)
            val = (char*)expand_env_vars(expanded_val, sizeof(expanded_val), val);

        
        val = (char*)expand_vars(expanded_val2, sizeof(expanded_val2),
                                 val, ht_sect);
        
        if (opt->val == NULL) {
            opt->val = n_strdup(val);
            //printf("ADD %p %s -> %s\n", ht_sect, name, val);
            
        } else if ((tag->flags & TYPE_MULTI) == 0) {
            logn(LOGWARN, _("%s:%d multiple '%s' not allowed"), path, nline, name);
            exit(0);
            
        } else if (opt->vals != NULL) {
            n_array_push(opt->vals, n_strdup(val));
            
        } else if (opt->vals == NULL) {
            opt->vals = n_array_new(2, free, (tn_fn_cmp)strcmp);
            /* put ALL opts to opt->vals */
            n_array_push(opt->vals, n_strdup(opt->val)); 
            n_array_push(opt->vals, n_strdup(val));
            opt->flags |= COPT_MULTIPLE;
        }
    }

    if (main_stream) {
        fclose(stream);
        stream = main_stream;
        main_stream = NULL;
        goto l_loop;
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
            return poldek_ldconf(path);
    }
    
    if (access(etcpath, R_OK) == 0)
        return poldek_ldconf(etcpath);

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

    if (htconf && (opt = n_hash_get(htconf, name)))
        return opt->vals;
    
    return NULL;
}



