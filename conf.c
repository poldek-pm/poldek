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
#include <trurl/nassert.h>

#include "i18n.h"
#include "log.h"
#include "conf.h"

#define TYPE_STR      (1 << 0)
#define TYPE_BOOL     (1 << 1)
#define TYPE_LIST     (1 << 2)
#define TYPE_MULTI    (1 << 3)
#define TYPE_ENUM     (1 << 4)

struct tag {
    char *name;
    int  flags;
    char *enums[8];
};

static struct tag valid_tags[] = {
    { "source",        TYPE_STR | TYPE_MULTI, { 0 } },
    { "source?*",      TYPE_STR , { 0 } },
    { "prefix?*",      TYPE_STR , { 0 } },
    { "cachedir",      TYPE_STR , { 0 } },
    { "ftp_http_get",  TYPE_STR , { 0 } },
    { "ftp_get",       TYPE_STR , { 0 } },
    { "http_get",      TYPE_STR , { 0 } },
    { "https_get",     TYPE_STR , { 0 } },
    { "rsync_get",     TYPE_STR , { 0 } },
    { "cdrom_get",     TYPE_STR , { 0 } },
    { "ssh_get",       TYPE_STR , { 0 } },
    { "ignore_req",    TYPE_STR | TYPE_MULTI , { 0 } },
    { "ignore_pkg",    TYPE_STR | TYPE_MULTI , { 0 } },
    { "rpmdef",        TYPE_STR | TYPE_MULTI , { 0 } },
    { "rpm_install_opt",  TYPE_STR , { 0 } },
    { "rpm_uninstall_opt",  TYPE_STR , { 0 } },
    { "follow",         TYPE_BOOL , { 0 } },
    { "greedy",         TYPE_BOOL , { 0 } }, 
    { "use_sudo",       TYPE_BOOL , { 0 } },
    { "mercy",          TYPE_BOOL , { 0 } },
    { "hold",           TYPE_STR | TYPE_LIST | TYPE_MULTI , { 0 } },
    { "keep_downloads", TYPE_BOOL , { 0 } },
    { "confirm_installs", TYPE_BOOL , { 0 } },
    { "choose_equivalents_manually", TYPE_BOOL , { 0 } },
    { "particle_install", TYPE_BOOL, { 0 } },
    { "ftp_sysuser_as_anon_passwd", TYPE_BOOL , { 0 } },
    {  NULL,           0, { 0 } }, 
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

    opt = malloc(sizeof(*opt) + strlen(name) + 1);
    strcpy(opt->name, name);
    opt->flags = 0;
    opt->val = NULL;
    opt->vals= NULL;
    return opt;
}  
                                                      
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
            opt->val = strdup(*p);
        } else {
            if (n_array_size(opt->vals) == 0) {
                opt->flags |= COPT_MULTIPLE;
                n_array_push(opt->vals, strdup(opt->val)); 
            }
            n_array_push(opt->vals, strdup(*p));
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

static const struct tag *find_tag(const char *key) 
{
    int i = 0;
    
    while (valid_tags[i].name) {
        if (strcmp(valid_tags[i].name, key) == 0)
            return &valid_tags[i];
        
        if (fnmatch(valid_tags[i].name, key, 0) == 0) 
            return &valid_tags[i];
        i++;
    }
    return NULL;
}


static void validate_tag(const char *key, void *unused) 
{
    int i = 0, found = 0;
    
    unused = unused;
    while (valid_tags[i].name) {
        if (fnmatch(valid_tags[i++].name, key, 0) == 0) {
            found = 1;
            break;
        }
    }
    
    if (!found) {
        logn(LOGWARN, _("%s: unknown option"), key);
        sleep(1);
    }
}


tn_hash *ldconf(const char *path) 
{
    FILE *stream;
    int nline = 0;
    tn_hash *ht;
    char buf[1024];
    
    if ((stream = fopen(path, "r")) == NULL) {
        logn(LOGERR, "fopen %s: %m", path);
        return NULL;
    }

    ht = n_hash_new(23, (tn_fn_free)copt_free);
    n_hash_ctl(ht, TN_HASH_NOCPKEY);

    
    while (fgets(buf, sizeof(buf), stream)) {
        char *p = buf;
        char *name, *val;
        struct copt *opt;
        const struct tag *tag;

        
        nline++;
        
        p = strrchr(buf, '\0'); /* eat trailing ws */
        n_assert(p);
        while (isspace(*p))
            *p-- = '\0';

        p = buf;
        while (isspace(*p))
            p++;

        if (*p == '#' || *p == '\0')
            continue;
        
        name = p;

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

        if ((tag = find_tag(name)) == NULL) {
            logn(LOGWARN, _("%s:%d unknown option '%s'"), path, nline, name);
            continue;
        }

        //printf("name = %s, v = %s\n", name, p);
        

        if (tag->flags & TYPE_LIST) {
            getvlist(ht, name, p, path, nline);
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
        
        if (n_hash_exists(ht, name)) {
            opt = n_hash_get(ht, name);
            
        } else {
            opt = copt_new(name);
            n_hash_insert(ht, opt->name, opt);
        }
        
        if (opt->val == NULL) {
            opt->val = strdup(val);
            
        } else if ((tag->flags & TYPE_MULTI) == 0) {
            logn(LOGWARN, _("%s:%d multiple '%s' not allowed"), path, nline, name);
            exit(0);
            
        } else if (opt->vals != NULL) {
            n_array_push(opt->vals, strdup(val));
            
        } else if (opt->vals == NULL) {
            opt->vals = n_array_new(2, free, (tn_fn_cmp)strcmp);
            /* put ALL opts to opt->vals */
            n_array_push(opt->vals, strdup(opt->val)); 
            n_array_push(opt->vals, strdup(val));
            opt->flags |= COPT_MULTIPLE;
        }
    }

    n_hash_map(ht, validate_tag);

    
    return ht;
}


tn_hash *ldconf_deafult(void)
{
    char *homedir;
    char *etcpath = "/etc/poldek.conf";
    
    if ((homedir = getenv("HOME")) != NULL) {
        char path[PATH_MAX];
        
        snprintf(path, sizeof(path), "%s/.poldekrc", homedir);
        if (access(path, R_OK) == 0)
            return ldconf(path);
    }
    
    if (access(etcpath, R_OK) == 0)
        return ldconf(etcpath);

    return NULL;
}


char *conf_get(tn_hash *htconf, const char *name, int *is_multi)
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

int conf_get_bool(tn_hash *htconf, const char *name, int default_v)
{
    char *v;
    
    if ((v = conf_get(htconf, name, NULL)) == NULL)
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


tn_array *conf_get_multi(tn_hash *htconf, const char *name)
{
    struct copt *opt;

    if (htconf && (opt = n_hash_get(htconf, name)))
        return opt->vals;
    
    return NULL;
}



