/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
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

#include "log.h"
#include "conf.h"

#define TYPE_STR      (1 << 0)
#define TYPE_BOOL     (1 << 1)
#define TYPE_LIST     (1 << 2)
#define TYPE_MULTI    (1 << 3)

struct tag {
    char *name;
    int  flags;
};

static struct tag valid_tags[] = {
    { "source",        TYPE_STR | TYPE_MULTI },
    { "source?*",      TYPE_STR },
    { "prefix?*",      TYPE_STR },
    { "cachedir",      TYPE_STR },
    { "ftp_http_get",  TYPE_STR },
    { "ftp_get",       TYPE_STR },
    { "http_get",      TYPE_STR },
    { "https_get",     TYPE_STR },
    { "rsync_get",     TYPE_STR },
    { "cdrom_get",     TYPE_STR },
    { "ssh_get",       TYPE_STR },
    { "ignore_req",    TYPE_STR | TYPE_MULTI },
    { "ignore_pkg",    TYPE_STR | TYPE_MULTI },
    { "rpmdef",        TYPE_STR | TYPE_MULTI },
    { "rpm_install_opt",  TYPE_STR },
    { "rpm_uninstall_opt",  TYPE_STR },
    { "follow",         TYPE_BOOL },
    { "greedy",         TYPE_BOOL }, 
    { "use_sudo",       TYPE_BOOL },
    { "mercy",          TYPE_BOOL },
    { "hold",           TYPE_STR | TYPE_LIST | TYPE_MULTI },
    { "keep_downloads", TYPE_BOOL }, 
    {  NULL,           0 }, 
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

static int getvlist(tn_hash *ht, char *name, char *vstr, const char *path, int nline) 
{
    const char **v, **p;
    struct copt *opt;

    
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
            opt->val = strdup(*p);
        } else {
            if (n_array_size(opt->vals) == 0) {
                opt->flags |= COPT_MULTIPLE;
                n_array_push(opt->vals, strdup(opt->val)); 
            }
            n_array_push(opt->vals, strdup(*p));
        }
        p++;
    }
    n_str_tokl_free(v);
    return 1;
}

static char *getv(char *vstr, const char *path, int nline) 
{
    int quoted = 0;
    char *p, *q;
    
    p = vstr;
    while (isspace(*p))
        p++;
    
    if (*p == '"') {
        quoted = 1;
        p++;
    }
    
    if (!quoted) {
        q = p;
        while (!isspace(*q) && *q)
            q++;
        *q = '\0';
        
    } else {
        q = p;
        while (*q != '"' && *q)
            q++;
        
        if (*q == '\0') {
            log(LOGERR, "%s:%d: \" missing\n", path, nline);
            p = NULL;
        }
        
        *q = '\0';
        q++;
        while (*q) {
            if (!isspace(*q)) {
                log(LOGERR, "%s:%d: syntax error\n", path, nline);
                p = NULL;
                break;
            }
            q++;
        }
    }
    
    return p;
}

static int is_tag(const char *key, unsigned flags) 
{
    int i = 0;
    
    while (valid_tags[i].name) {
        if (strcmp(valid_tags[i].name, key) == 0)
            return valid_tags[i].flags & flags;
        
            
        if (fnmatch(valid_tags[i++].name, key, 0) == 0) {
            return valid_tags[i].flags & flags;
            break;
        }
    }
    return -1;
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
        log(LOGWARN, "%s: unknown option\n");
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
        log(LOGERR, "fopen %s: %m\n", path);
        return NULL;
    }

    ht = n_hash_new(23, (tn_fn_free)copt_free);
    n_hash_ctl(ht, TN_HASH_NOCPKEY);

    
    while (fgets(buf, sizeof(buf), stream)) {
        char *p = buf;
        char *name, *val;
        struct copt *opt;
        int is_mutliple, is_list;
        
        
        nline++;
        while (isspace(*p))
            p++;

        if (*p == '#' || *p == '\n' || *p == '\0')
            continue;
        
        name = p;

        while (isalnum(*p) || *p == '_')
            p++;
        
        if (!isspace(*p)) {
            log(LOGERR, "%s:%d: invalid parameter %c '%s'\n", path, nline, *p, name);
            continue;
        }
        *p++ = '\0';

        while (isspace(*p))
            p++;
        
        if (*p != '=') {
            log(LOGERR, "%s:%d: missing '='\n", path, nline);
            continue;
        }

        if (*p != '\0') {
            char *q = strchr(p, '\0') - 1;
            while (isspace(*q))
                *q-- = '\0';
        }
        
        if ((is_list = is_tag(name, TYPE_LIST)) < 0) {
            log(LOGWARN, "%s:%d unknown option '%s'\n", path, nline, name);
            continue;
            
        } else if (is_list) {
            getvlist(ht, name, ++p, path, nline);
            continue;
        }
        
        p++;
        val = getv(p, path, nline);
        if (val == NULL) {
            log(LOGERR, "%s:%d: no value for '%s'\n", path, nline, name);
            continue;
        }

        if ((is_mutliple = is_tag(name, TYPE_MULTI)) < 0) {
            log(LOGWARN, "%s:%d unknown option '%s'\n", path, nline, name);
            continue;
        }
        
        if (n_hash_exists(ht, name)) {
            opt = n_hash_get(ht, name);
        } else {
            opt = copt_new(name);
            n_hash_insert(ht, opt->name, opt);
        }
        
        if (opt->val == NULL) {
            opt->val = strdup(val);
            
        } else if (!is_mutliple) {
            log(LOGWARN, "%s:%d multiple '%s' not allowed\n", path, nline, name);
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
    char path[PATH_MAX];
    
    if (access(path, R_OK) == 0)
        return ldconf(path);

    if ((homedir = getenv("HOME")) == NULL)
        return NULL;

    snprintf(path, sizeof(path), "%s/.poldekrc", homedir);
    if (access(path, R_OK) != 0) {
        char *path = "/etc/poldek.conf";
        
        if (access(path, R_OK) == 0)
            return ldconf(path);
        return NULL;
    }
    
    return ldconf(path);
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

tn_array *conf_get_multi(tn_hash *htconf, const char *name)
{
    struct copt *opt;

    if (htconf && (opt = n_hash_get(htconf, name)))
        return opt->vals;
    
    return NULL;
}



