/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <stdlib.h>
#include <string.h>

#include <trurl/nhash.h>
#include <trurl/nassert.h>

#include "i18n.h"
#include "depdirs.h"

struct depdir {
    char *dir;
    int  len;
    char endch;
};

static struct depdir *depdirs = NULL;
#if 0
static struct depdir not_depdirs[] = {
    { "usr/share/doc", 0, 0, }, 
    { "usr/share/info", 0, 0, }, 
    { "usr/share/man", 0, 0, }, 
    { "usr/share/locale", 0, 0, }, 
    { "usr/src", 0, 0 }, 
};
#endif

void init_depdirs(tn_array *dirnames) 
{
    int i, n = 0;

    depdirs = malloc((n_array_size(dirnames)+1) * sizeof(*depdirs));
    for (i=n_array_size(dirnames)-1; i >= 0; i--) {
        depdirs[n].dir = strdup(((char*)n_array_nth(dirnames, i)) + 1);
        depdirs[n].len = strlen(depdirs[n].dir);
        depdirs[n].endch = *(depdirs[n].dir + (depdirs[n].len - 1));
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

    while (depdirs[i].dir) {
        if (dirlen >= depdirs[i].len &&
            depdirs[i].endch == *(dir + depdirs[i].len - 1) && 
            strncmp(depdirs[i].dir, dir, depdirs[i].len - 1) == 0)
            return 1;
        i++;
        
    }
    return 0;
}


int in_depdirs(const char *dir) 
{
    register int i = 0;

    if (depdirs == NULL)
        return 1;

    if (*dir == '\0')
        return 1;

    while (depdirs[i].dir) {
        if (*depdirs[i].dir != *dir) {
            i++;
            continue;
        }
        
        if (strncmp(depdirs[i].dir, dir, depdirs[i].len) == 0)
            return 1;
        i++;
        
    }

    return 0;
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

