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

#include <string.h>
#include <sys/types.h>
#include <unistd.h>


#include <trurl/nassert.h>
#include <trurl/nstr.h>

#include <vfile/vfile.h>
#include "i18n.h"
#include "pkgset.h"
#include "pkgset-load.h"
#include "misc.h"
#include "log.h"

struct src_option {
    char      *name;
    unsigned  flag;
};


static struct src_option source_options[] = {
    { "noauto",   PKGSOURCE_NOAUTO     }, 
    { "noautoup", PKGSOURCE_NOAUTOUP   }, 
    { "gpg",      PKGSOURCE_VER_GPG    },
    { "pgp",      PKGSOURCE_VER_PGP    },
    {  NULL,      0 }, 
};


struct source *source_new(const char *pathspec, const char *pkg_prefix)
{
    struct source   *src;
    struct stat     st;
    const char      *path, *p;
    char            *name, *q;
    int             len;
    char            clpath[PATH_MAX], clprefix[PATH_MAX];
    int             n;
    
    
    p = pathspec;
    
    while (*p && *p != '|' && !isspace(*p))
        p++;

    if (*p == '\0') {           /* path only */
        path = pathspec;
        name = "anon";
        
    } else {
        path = p + 1;
        while (isspace(*path))
            path++;
        
        len = p - pathspec;
        name = alloca(len + 1);
        memcpy(name, pathspec, len);
        name[len] = '\0';
        
        if (*name == '[') 
            name++;
        
        if ((q = strrchr(name, ']')))
            *q = '\0';
        if (*name == '\0')
            name = "anon";
    }

    

    if ((n = vf_cleanpath(clpath, sizeof(clpath), path)) == 0 ||
        n == sizeof(clpath))
        return NULL;
    
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        clpath[n++] = '/';
    
    else {
        int l = strlen(path);
        if (path[l - 1] == '/')
            clpath[n++] = '/';
    }
    clpath[n] = '\0';

    if (pkg_prefix) {
        int l = strlen(pkg_prefix);
        
        if ((n = vf_cleanpath(clprefix, sizeof(clprefix), pkg_prefix)) == 0 ||
            n == sizeof(clprefix))
            return NULL;
        
        if (pkg_prefix[l - 1] == '/')
            clprefix[n++] = '/';

        clprefix[n] = '\0';
    }
    
    src = malloc(sizeof(*src));
    src->flags = 0;
    src->source_path = strdup(clpath);
    if (pkg_prefix)
        src->pkg_prefix = strdup(clprefix);
    else
        src->pkg_prefix = NULL;
    src->ldmethod = PKGSET_LD_NIL;
    
    
    
    if ((q = strchr(name, ','))) {
        const char **tl, **t;
        
        *q++ = '\0';
        tl = t = n_str_tokl(q, ",");
        n_assert(tl);

        while (*t) {
            int n = 0;
            while (source_options[n].name != NULL) {
                if (strcmp(*t, source_options[n].name) == 0) {
                    src->flags |= source_options[n].flag;
                    break;
                }
                n++;
            }
            if (source_options[n].name == NULL)
                logn(LOGWARN, _("%s: %s unknown option"), name, *t);
            t++;
        }
        n_str_tokl_free(tl);
    }

    src->source_name = strdup(name);
    return src;
}

void source_free(struct source *src)
{
    free(src->source_path);
    if (src->pkg_prefix)
        free(src->pkg_prefix);
    if (src->source_name)
        free(src->source_name);
    free(src);
}


int source_cmp(struct source *s1, struct source *s2)
{
    return strcmp(s1->source_path, s2->source_path);
}

int source_cmp_name(struct source *s1, struct source *s2)
{
    return strcmp(s1->source_name, s2->source_name);
}


int source_snprintf_flags(char *str, int size, struct source *src) 
{
    int n, i;
    
    n_assert(size > 0);
    
    *str = '\0';

    i = n = 0;
    while (source_options[i].name != NULL) {
        if (src->flags & source_options[i].flag)
            n += n_snprintf(&str[n], size - n, "%s,", source_options[i].name);
        i++;
    }
    
    if (n > 0)
        str[n - 1] = '\0';      /* eat last comma */
    
    return n;
}


int source_update(struct source *src)
{
    if (src->ldmethod == PKGSET_LD_HDL) {
        logn(LOGWARN, _("%s: this type of source is not updateable"),
             src->source_path);
        return 0;
    }
    
    return update_whole_pkgdir(src->source_path);
}

int pkgset_load(struct pkgset *ps, int ldflags, tn_array *sources)
{
    int i, j, iserr = 0;
    struct pkgdir *pkgdir = NULL;


    for (i=0; i < n_array_size(sources); i++) {
        struct source *src = n_array_nth(sources, i);

        if (src->flags & PKGSOURCE_NOAUTO)
            continue;

        if (src->ldmethod == PKGSET_LD_NIL) 
            src->ldmethod = PKGSET_LD_IDX;
        
        switch (src->ldmethod) {
            case PKGSET_LD_IDX:
                pkgdir = pkgdir_new(src->source_name, src->source_path,
                                    src->pkg_prefix, PKGDIR_NEW_VERIFY);
                if (pkgdir != NULL) 
                    break;
                
                if (is_dir(src->source_path)) 
                    src->ldmethod = PKGSET_LD_DIR; /* no break */
                else
                    break;
                
            case PKGSET_LD_DIR:
                msg(1, _("Loading %s..."), src->source_path);
                pkgdir = pkgdir_load_dir(src->source_name, src->source_path);
                break;

            case PKGSET_LD_HDL:
                msgn(1, _("Loading %s..."), src->source_path);
                pkgdir = pkgdir_load_hdl(src->source_name, src->source_path,
                                         src->pkg_prefix);
                break;

            default:
                n_assert(0);
        }

        if (pkgdir == NULL) {
            if (n_array_size(sources) > 1)
                logn(LOGWARN, _("%s: load failed, skipped"), src->source_path);
            continue;
        }

        if (src->flags & PKGSOURCE_VER_GPG)
            pkgdir->flags |= PKGDIR_VER_GPG;

        if (src->flags & PKGSOURCE_VER_PGP)
            pkgdir->flags |= PKGDIR_VER_PGP;
        
        n_array_push(ps->pkgdirs, pkgdir);
    }


    /* merge pkgdis depdirs into ps->depdirs */
    for (i=0; i<n_array_size(ps->pkgdirs); i++) {
        pkgdir = n_array_nth(ps->pkgdirs, i);
        
        if (pkgdir->depdirs) {
            for (j=0; j<n_array_size(pkgdir->depdirs); j++)
                n_array_push(ps->depdirs, n_array_nth(pkgdir->depdirs, j));
        }
    }

    n_array_sort(ps->depdirs);
    n_array_uniq(ps->depdirs);

    
    for (i=0; i<n_array_size(ps->pkgdirs); i++) {
        pkgdir = n_array_nth(ps->pkgdirs, i);

        if (pkgdir->flags & PKGDIR_LDFROM_IDX) {
            msgn(1, _("Loading %s..."), pkgdir->idxpath);
            if (!pkgdir_load(pkgdir, ps->depdirs, ldflags)) {
                logn(LOGERR, _("%s: load failed"), pkgdir->idxpath);
                iserr = 1;
            }
        }
    }
    
    if (!iserr) {
        /* merge pkgdirs packages into ps->pkgs */
        for (i=0; i<n_array_size(ps->pkgdirs); i++) {
            pkgdir = n_array_nth(ps->pkgdirs, i);
            for (j=0; j<n_array_size(pkgdir->pkgs); j++)
                n_array_push(ps->pkgs, pkg_link(n_array_nth(pkgdir->pkgs, j)));
        }
    }
    
    if (n_array_size(ps->pkgs)) {
        int n = n_array_size(ps->pkgs);
        msgn(1, ngettext("%d package read",
                        "%d packages read", n), n);
    }
    
    return n_array_size(ps->pkgs);
}
