/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <trurl/nassert.h>
#include "i18n.h"
#include "pkg.h"


int main(int argc, char *argv[])
{
    struct pkg *pkg;
    struct capreq *req;
    int epoch = 0;
    unsigned rels[] = { REL_EQ, REL_EQ | REL_GT, REL_EQ | REL_LT,
                           REL_GT, REL_LT, 0};
    char *versions[] = {"1.0", "1.1", NULL};
    char *relases[] = {"1", "2", NULL};
    int i, j, k;
        
    for (epoch=0; epoch<1; epoch++) {
        printf("\nEpoch: %d\n", epoch);
        i = 0;
        while (versions[i]) {
            j = 0;
            while (relases[j]) {
                struct capreq *cap;
                
                pkg = pkg_new("poldek", epoch, versions[i], relases[j], NULL, NULL, 0, 0, 0);
                cap = capreq_new("poldek", epoch, versions[i++], relases[j++], REL_EQ, 0);
                k = 0;
                while (rels[k] > 0) {
                    int rc1, rc2;
                    
                    req = capreq_new_evr("poldek", strdup("1.1"), rels[k++], 0);

                    rc1 = pkg_match_req(pkg, req, 1) ? 1:0;
                    rc2 = cap_match_req(cap, req, 1) ? 1:0;
                    printf("P %s match %s -> %s\n", pkg_snprintf_s(pkg), capreq_snprintf_s(req),
                           rc1 ? "YES" : "NO");

                    printf("C %s match %s -> %s\n\n", capreq_snprintf_s(cap), capreq_snprintf_s0(req),
                           rc2 ? "YES" : "NO");
                    n_assert(rc1 == rc2);
                }
            }
        }
    }
    return 0;
}
