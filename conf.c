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

#include "log.h"
#include "conf.h"

#define TYPE_STR      (1 << 0)
#define TYPE_BOOL     (1 << 1)
#define TYPE_LIST     (1 << 2)
#define TYPE_ISMULTI  (1 << 3)

struct tag {
    char *name;
    int  is_mutliple;
    int  type;
};

static struct tag valid_tags[] = {
    { "source",       1, TYPE_STR },
    { "source?*",     0, TYPE_STR },
    { "prefix?*",     0, TYPE_STR },
    { "cachedir",     0, TYPE_STR },
    { "ftp_http_get", 0, TYPE_STR },
    { "ftp_get",      0, TYPE_STR },
    { "http_get",     0, TYPE_STR },
    { "https_get",    0, TYPE_STR },
    { "rsync_get",    0, TYPE_STR },
    { "cdrom_get",    0, TYPE_STR },
    { "ssh_get",      0, TYPE_STR },
    { "ignore_req",   1, TYPE_STR },
    { "ignore_pkg",   1, TYPE_STR },
    { "rpmdef",       1, TYPE_STR },
    { "rpm_install_opt", 0, TYPE_STR },
    { "rpm_uninstall_opt", 0, TYPE_STR },
    { "follow",       0, TYPE_BOOL },
    { "greedy",       0, TYPE_BOOL }, 
    { "use_sudo",     0, TYPE_BOOL },
    { "hold",         1, TYPE_STR | TYPE_LIST },
    {  NULL,          0, 0 }, 
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

#if 0
static char *getvlist(char *vstr, const char *path, int nline) 
{
    const char **v, **p;
    
    p = v = n_str_tokl(vstr, " \t");
    return p;
}
#endif

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

static int is_multi_tag(const char *key) 
{
    int i = 0;
    
    while (valid_tags[i].name) {
        if (strcmp(valid_tags[i].name, key) == 0)
            return valid_tags[i].is_mutliple;
        
            
        if (fnmatch(valid_tags[i++].name, key, 0) == 0) {
            return valid_tags[i].is_mutliple;
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
        int is_mutliple;
        
        
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
        
        p++;
        val = getv(p, path, nline);
        if (val == NULL) {
            log(LOGERR, "%s:%d: no value for '%s'\n", path, nline, name);
            continue;
        }

        if ((is_mutliple = is_multi_tag(name)) < 0) {
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



