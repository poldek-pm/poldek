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

#include "log.h"
#include "conf.h"

struct conf_tag {
    char *name;
    int  namlen;
    char *val;                  /* if  */
};

#define TAG_SOURCE      0
#define TAG_CACHEDIR    1
#define TAG_PREFIX      2
#define TAG_FTPHTTPGET  3
#define TAG_FTPGET      4
#define TAG_HTTPGET     5
#define TAG_HTTPSGET    6
#define TAG_RSYNCGET    7
#define TAG_SSHGET      8


static struct conf_tag tags[] = {
    { "source",    0, 0    },
    { "cachedir",  0, 0    },
    { "prefix",    0, 0    }, 
    { "ftp_http_get", 0, 0 },
    { "ftp_get",   0, 0    },
    { "http_get",  0, 0    },
    { "https_get", 0, 0    },
    { "rsync_get", 0, 0    },
    { "ssh_get",   0, 0    },
    {  NULL,       0, 0    }, 
};


static void init_tags(void)
{
    int n = 0;

    while (tags[n].name != NULL) {
        tags[n].namlen = strlen(tags[n].name);
        tags[n].val = NULL;
        n++;
    }
}


static void free_tags_values(void)
{
    int n = 0;

    while (tags[n].name != NULL) {
        if (tags[n].val != NULL) {
            free(tags[n].val);
            tags[n].val = NULL;
        }
        n++;
    }
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


static char *getagv(char *buf, const char *tag, int taglen,
                  const char *path, int nline) 
{
    char *p = NULL;
    
    if (strncmp(buf, tag, taglen) == 0) {
        p = buf + taglen;
        while (isspace(*p))
            p++;
        
        if (*p != '=') {
            log(LOGERR, "%s:%d: missing '='\n", path, nline);
            p = NULL;
            
        } else {
            p++;
            p = getv(p, path, nline);
        }
    }
    
    return p;
}


struct conf_s *ldconf_deafult(void)
{
    char *homedir;
    char path[PATH_MAX];
    
    if ((homedir = getenv("HOME")) == NULL)
        return NULL;

    snprintf(path, sizeof(path), "%s/.poldekrc", homedir);
    if (access(path, R_OK) != 0)
        return NULL;

    return ldconf(path);
}


struct conf_s *ldconf(const char *path) 
{
    FILE *stream;
    int n, nline = 0, nvals = 0, nerrs = 0;
    struct conf_s *cnf = NULL;
    tn_array *rpmacros = NULL;
    char buf[1024];
    
    if ((stream = fopen(path, "r")) == NULL) {
        log(LOGERR, "fopen %s: %m", path);
        return NULL;
    }
    
    init_tags();
    
    while (fgets(buf, sizeof(buf), stream)) {
        char *p = buf;

        nline++;
        while (isspace(*p))
            p++;
        
        if (*p == '#')
            continue;

        if (strncmp(p, "rpmdef", 6) == 0) {
            char *v;
            
            p += 6;
            if (!isspace(*p)) {
                log(LOGERR, "%s:%d: invalid parameter (%s)\n", path, nline,
                    buf);
            }
            while (isspace(*p))
                p++;

            if (*p != '=') {
                log(LOGERR, "%s:%d: missing '='\n", path, nline);
                nerrs++;
                continue;
            }
            p++;
            v = getv(p, path, nline);
            if (v == NULL) {
                nerrs++;
            } else {
                if (rpmacros == NULL)
                    rpmacros = n_array_new(2, free, (tn_fn_cmp)strcmp);
                    
                n_array_push(rpmacros, strdup(v));
            }
            continue;
        }
        
        
        n = 0;
        while (tags[n].name != NULL) {
            if (tags[n].val == NULL) {
                char *v;
                
                v = getagv(p, tags[n].name, tags[n].namlen, path, nline);
                if (v != NULL) {
                    tags[n].val = strdup(v);
                    nvals++;
                    goto l_continue;
                }
            }
            n++;
        }
    l_continue:
    }
    
    
    fclose(stream);
    
    if (nerrs) {
        free_tags_values();
        if (rpmacros)
            n_array_free(rpmacros);
        
    } else if (nvals || rpmacros != NULL) {
        cnf = malloc(sizeof(*cnf));
        cnf->source = tags[TAG_SOURCE].val;
        cnf->cachedir = tags[TAG_CACHEDIR].val;
        cnf->prefix   = tags[TAG_PREFIX].val;
        
        cnf->ftp_http_get = tags[TAG_FTPHTTPGET].val;
        cnf->ftp_get   = tags[TAG_FTPGET].val;
        cnf->http_get  = tags[TAG_HTTPGET].val;
        cnf->https_get = tags[TAG_HTTPSGET].val;
        cnf->rsync_get = tags[TAG_RSYNCGET].val;

        cnf->rpmacros = rpmacros;
    }
    
    return cnf;
}


void conf_s_free(struct conf_s *cnf) 
{
    if (cnf->source)
        free(cnf->source);
    
    if (cnf->cachedir)
        free(cnf->cachedir);

    if (cnf->prefix)
        free(cnf->cachedir);
    
    if (cnf->rpm_path)
        free(cnf->rpm_path);

    if (cnf->rpm_args)
        free(cnf->rpm_args);

    free(cnf);
}



