/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <trurl/nhash.h>
#include <trurl/nassert.h>
#include <trurl/nmalloc.h>

#define ENABLE_TRACE 0
#include "log.h"
#include "i18n.h"
#include "depdirs.h"

struct depdir {
    char *dir;
    int  len;
    char endch;
};

static struct depdir *depdirs = NULL;


void init_depdirs(tn_array *dirnames) 
{
    int i, n = 0;

    depdirs = n_malloc((n_array_size(dirnames)+1) * sizeof(*depdirs));
    for (i = n_array_size(dirnames)-1; i >= 0; i--) {
        const char *dir = n_array_nth(dirnames, i);
        
        depdirs[n].len = strlen(dir);
        depdirs[n].dir = n_strdupl(dir, depdirs[n].len);
        depdirs[n].endch = *(depdirs[n].dir + (depdirs[n].len - 1));
        DBGF("%s\n", depdirs[n].dir);
        n++;
    }
    depdirs[n].dir = NULL;
}


void destroy_depdirs(void) 
{
    int i = 0;

    while (depdirs[i].dir) 
        free(depdirs[i].dir);
    free(depdirs);
    depdirs = NULL;
}


int in_depdirs_l(const char *dir, int dirlen) 
{
    register int i = 0;

    if (depdirs == NULL)
        return 1;
    
    if (*dir == '\0')
        return 1;

    DBGF("in_depdirs_l %s -> ", dir);
    while (depdirs[i].dir) {
        register struct depdir *ddir = &depdirs[i++];
        
        if (dirlen < ddir->len)
            continue;
        
        if (ddir->endch != *(dir + ddir->len - 1))
            continue;
        
        if (strncmp(ddir->dir, dir, ddir->len - 1) == 0) {
            DBG("YES (%d)\n", i);
            return 1;
        }
        	
    }
    
    DBG("NO\n");
    return 0;
}

int in_depdirs(const char *dir) 
{
    return in_depdirs_l(dir, strlen(dir));
}


char *path2depdir(char *path) 
{
    char *p;
    
    n_assert(*path == '/');

    if (*(path + 1) == '\0')
        return path;
    
    p = strrchr(path, '/');

    if (p != path)
        *p = '\0';
    
    return path + 1;
}

