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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <vfile/vfile.h>

#include "log.h"
#include "usrset.h"
#include "rpmadds.h"
#include "misc.h"


void pkgdef_free(struct pkgdef *pdef)
{
    if (pdef->pkg)
        pkg_free(pdef->pkg);
    pdef->pkg = NULL;
    free(pdef);
}

int pkgdef_cmp(struct pkgdef *pdef1, struct pkgdef *pdef2)
{
    int cmprc = 0;
    
    if (pdef1->pkg && pdef2->pkg) 
        if ((cmprc = pkg_cmp_name(pdef1->pkg, pdef2->pkg)))
            return cmprc;
        
    if ((cmprc = pdef1->tflags - pdef2->tflags)) 
        return cmprc;
        
    if (pdef1->tflags & PKGDEF_VIRTUAL)
        cmprc = strcmp(pdef1->virtname, pdef2->virtname);
    
    return cmprc;
}



__inline__
static int argvlen(char **argv) 
{
    int i = 0, len = 0;
    while (argv[i])
        len += strlen(argv[i++]) + 1;
    return len;
}

static 
int pkgdef_new_str(struct pkgdef **pdefp, char *buf, int buflen,
                   const char *fpath, int nline)
{
    struct pkgdef *pdef = NULL;
    char *q, *p, *s[1024];
    int i, n, tflags = 0;
    
    n = buflen;
    *pdefp = NULL;
    
    s[0] = NULL;

    while (n && isspace(buf[n - 1]))
        buf[--n] = '\0';
    
    p = buf;
    while(isspace(*p))
        p++;
        
    if (*p == '\0' || *p == '#')
        return 0;

    while (*p && !isalnum(*p)) {
        switch (*p) {
            case '~':
            case '!':           /* for backward compatybility */
                tflags |= PKGDEF_OPTIONAL;
                break;
                
            case  '@': 
                tflags |= PKGDEF_VIRTUAL;
        }
        p++;
    }
    
    
            
                
    if (!isalnum(*p)) {
        if (nline > 0)
            log(LOGERR, "%s: syntax error at line %d\n", fpath, nline);
        else 
            log(LOGERR, "syntax error in package definition\n");
        return -1;
    }
    

    s[0] = p;
    i = 1;
    if ((q = strchr(p, ' '))) {
        *q++ = '\0';
        while(isspace(*q))
            q++;
        s[i++] = q;
        p = q;
        
        while ((q = strchr(p, ' '))) {
            *q++ = '\0';
            while(isspace(*q))
                q++;
            s[i++] = p;
            p = q;
        }
    }
    
    s[i] = NULL;
    
    if (s[0]) {
        char *evrstr = NULL, *name = NULL, *virtname = NULL;
        char *version = NULL, *release = NULL;
        
        int32_t epoch = 0;
            
        if (tflags & PKGDEF_VIRTUAL) {
            virtname = s[0];
            name = s[1];
            if (name) 
                evrstr = s[2];
                
        } else {
            virtname = NULL;
            name = s[0];
            evrstr = s[1];
        }
        
        //     printf("name = %s, evrstr = %s\n", name, evrstr);

        if (name && !validstr(name)) {
            if (nline) 
                log(LOGERR, "%s: invalid name (%s) at line %d\n",
                    name, fpath, nline);
            else 
                log(LOGERR, "invalid package name (%s)\n", name);
            return -1;
        }
        
        pdef = malloc(sizeof(*pdef) +
                      (virtname ? strlen(virtname) + 1 : 0));
        pdef->tflags = tflags;

        if (name == NULL) {
            pdef->pkg = NULL;
            
        } else {
            if (evrstr) 
                parse_evr(evrstr, &epoch, &version, &release);

            if (version == NULL)
                version = "";

            if (release == NULL)
                release = "";
                
            pdef->pkg = pkg_new(name, epoch, version, release, architecture(),
                                0, 0, NULL);
        }

        if (virtname) 
            strcpy(pdef->virtname, virtname);
        
        *pdefp = pdef;
    }
    
    return pdef ? 1 : 0;
}


static 
int pkgdef_new_pkgfile(struct pkgdef **pdefp, const char *path)
{
    struct pkg *pkg;
    struct pkgdef *pdef;
    

    if ((pkg = pkg_ldrpm(path, PKG_LDNEVR)) == NULL)
        return -1;
    
    
    pdef = malloc(sizeof(*pdef));
    pdef->tflags = 0;
    pdef->pkg = pkg;
    *pdefp = pdef;

    return pdef ? 1 : 0;
}


struct usrpkgset *usrpkgset_new(void) 
{
    struct usrpkgset *ups;

    ups = malloc(sizeof(*ups));
    ups->pkgdefs = n_array_new(64, (tn_fn_free)pkgdef_free,
                               (tn_fn_cmp) pkgdef_cmp);
    ups->path = NULL;
    return ups;
}


void usrpkgset_free(struct usrpkgset *ups) 
{
    if (ups->path)
        free(ups->path);

    if (ups->pkgdefs)
        n_array_free(ups->pkgdefs);
    free(ups);
}


int usrpkgset_add_list(struct usrpkgset *ups, const char *fpath)
{
    char buf[1024];
    struct vfile *vf;
    int nline, rc = 1;
    
    if ((vf = vfile_open(fpath, VFT_STDIO, VFM_RO)) == NULL) 
        return 0;

    nline = 0;
    while (fgets(buf, sizeof(buf), vf->vf_stream)) {
        struct pkgdef *pdef;
        
        nline++;
        
        switch (pkgdef_new_str(&pdef, buf, strlen(buf), fpath, nline)) {
            case 0:
                break;
                
            case 1:
                n_array_push(ups->pkgdefs, pdef);
                break;
                
            case -1:
                log(LOGERR, "%s: give up at %d\n", fpath, nline);
                rc = 0;
                break;
                
            default:
                n_assert(0);
                abort();
        }
        
        if (rc == 0)
            break;
    }
    
    vfile_close(vf);
    
    if (rc) {
        n_array_sort(ups->pkgdefs);
        ups->path = strdup(fpath);
    }

    return rc;
}

int usrpkgset_add_str(struct usrpkgset *ups, char *def, int deflen) 
{
    struct pkgdef *pdef = NULL;

    if ((pkgdef_new_str(&pdef, def, deflen, NULL, -1)) > 0) {
        n_array_push(ups->pkgdefs, pdef);
        return 1;
    }
    
    return 0;
}

int usrpkgset_add_file(struct usrpkgset *ups, const char *path) 
{
    struct pkgdef *pdef = NULL;
    
    if ((pkgdef_new_pkgfile(&pdef, path)) > 0) {
        n_array_push(ups->pkgdefs, pdef);
        return 1;
    }
    
    return 0;
}


int usrpkgset_setup(struct usrpkgset *ups) 
{
    if (ups->pkgdefs) {
        int n;

        n = n_array_size(ups->pkgdefs);
        n_array_sort(ups->pkgdefs);
        n_array_uniq(ups->pkgdefs);

        if (n != n_array_size(ups->pkgdefs)) {
            msg(1, "Removed %d duplicates from given packages\n",
                n - n_array_size(ups->pkgdefs));
        }
    }

    return 1;
}

