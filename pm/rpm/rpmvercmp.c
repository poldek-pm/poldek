/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>
  Copyright (C) 2010 - 2012 Marcin Banasiak <marcin.banasiak@gmail.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif 

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_RPM_5
# include <rpm/rpmtag.h>
#endif

#define _RPMEVR_INTERNAL
#include <rpm/rpmevr.h>

#ifndef HAVE_RPM_5
static void parse(const char *evrstr, EVR_t evr)
{
    rpmEVRparse(evrstr, evr);
    
    if (evr->E == NULL) {
	evr->E = "0";
	evr->Elong = 0;
    }
    
    if (evr->R == NULL)
	evr->R = "0";
}
#endif

int main(int argc, char *argv[])
{
    int cmprc;
    const char *v1, *v2;
    EVR_t evr1, evr2;
    
    if (argc < 3) {
        printf("Usage: rpmvercmp VERSION1 VERSION2\n");
        exit(EXIT_SUCCESS);
    }

    if (argc == 3) {
        v1 = argv[1];
        v2 = argv[2];
    
    } else {
        printf("Usage: rpmvercmp VERSION1 VERSION2\n");
        exit(1);
    }

    evr1 = malloc(sizeof(struct EVR_s));
    evr2 = malloc(sizeof(struct EVR_s));

#ifdef HAVE_RPM_5    
    rpmEVRparse(v1, evr1);
    rpmEVRparse(v2, evr2);
#else
    parse(v1, evr1);
    parse(v2, evr2);
#endif

    cmprc = rpmEVRcompare(evr1, evr2);
    
    printf("%s %s %s\n", v1, cmprc == 0 ?  "==" : cmprc > 0 ? ">" : "<", v2);
    
    if (cmprc < 0)
        cmprc = 2;

    free((char *)evr1->str);
    free((char *)evr2->str);
    free(evr1);
    free(evr2);

    exit(cmprc);
}
