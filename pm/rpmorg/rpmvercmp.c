/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>
  Copyright (C) 2010 - 2013 Marcin Banasiak <marcin.banasiak@gmail.com>

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

#include <rpm/rpmlib.h>

int main(int argc, char *argv[])
{
    int cmprc;
    const char *v1, *v2;
    rpmver evr1, evr2;

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

    if (rpmReadConfigFiles(NULL, NULL) < 0) {
	fprintf(stderr, "Failed to read configuration files\n");
	exit(2);
    }

    evr1 = rpmverParse(v1);
    evr2 = rpmverParse(v2);

    cmprc = rpmverCmp(evr1, evr2);

    printf("%s %s %s\n", v1, cmprc == 0 ?  "==" : cmprc > 0 ? ">" : "<", v2);

    if (cmprc < 0)
        cmprc = 2;

    rpmverFree(evr1);
    rpmverFree(evr2);

    exit(cmprc);
}
