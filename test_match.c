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
#include <trurl/nmalloc.h>

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "misc.h"
#include "capreq.h"
#include "rpm/rpm.h"

int test_match(int argc, char *argv[])
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
                
                pkg = pkg_new("poldek", epoch, versions[i], relases[j], NULL, NULL);
                cap = capreq_new("poldek", epoch, versions[i++], relases[j++],
                                 REL_EQ, 0);
                k = 0;
                while (rels[k] > 0) {
                    int rc1, rc2;
                    char evr[255];

                    snprintf(evr, sizeof(evr), "1:1.1");
                    req = capreq_new_evr("poldek", n_strdup(evr), rels[k++], 0);

                    rc1 = pkg_match_req(pkg, req, 1) ? 1:0;
                    rc2 = cap_match_req(cap, req, 1) ? 1:0;
                    printf("P %s[%d] match %s -> %s\n", pkg_snprintf_s(pkg),
                           pkg->epoch, capreq_snprintf_s(req),
                           rc1 ? "YES" : "NO");

                    printf("C %s match %s -> %s\n\n", capreq_snprintf_s(cap),
                           capreq_snprintf_s0(req),
                           rc2 ? "YES" : "NO");
                    n_assert(rc1 == rc2);
                }
            }
        }
    }
    return 0;
}

int test_match_(int epoch, char *ver, char *rel, char *evr, int relation) 
{
    struct pkg *pkg;
    struct capreq *req;
    int rc1;
    
    pkg = pkg_new("poldek", epoch, ver, rel,  NULL, NULL);

    req = capreq_new_evr("poldek", n_strdup(evr), relation, 0);

    rc1 = pkg_match_req(pkg, req, 1) ? 1:0;
    printf("P %s[%d] match %s ? %s\n", pkg_snprintf_s(pkg),
           pkg->epoch, capreq_snprintf_s(req),
           rc1 ? "YES" : "NO");
    return 1;
}


int test_match2(void) 
{
    printf("\n");
    test_match_(0, "1.2", "1", "0:1.1", REL_GT);
    test_match_(3, "1.2", "1", "0:1.1", REL_GT);
    test_match_(0, "1.2", "1", "3:1.1", REL_GT);
    printf("\n");
    test_match_(0, "1.2", "1", "1:1.1", REL_GT);
    test_match_(1, "1.2", "1", "0:1.1", REL_GT);

    printf("\n");
    test_match_(0, "1.2", "1", "0:1.2", REL_EQ);
    test_match_(3, "1.2", "1", "0:1.2", REL_EQ);
    test_match_(0, "1.2", "1", "3:1.2", REL_EQ);
    printf("\n");
    test_match_(0, "1.2", "1", "1:1.2", REL_EQ);
    test_match_(1, "1.2", "1", "0:1.2", REL_EQ);

    printf("\n");
    test_match_(0, "1.2", "1", "1:2.2", REL_EQ | REL_GT);
    test_match_(1, "1.2", "1", "1:1.2", REL_EQ | REL_GT);
    test_match_(1, "1.2", "1", "2:1.2", REL_EQ | REL_GT);
    test_match_(2, "1.2", "1", "2:1.2", REL_EQ | REL_GT);
    test_match_(3, "1.2", "1", "2:1.2", REL_EQ | REL_GT);
    return 0;
}


int test_expand_env(int argc, char *argv[])
{
    int i;

    for (i=1; i<argc; i++) {
        char buf[PATH_MAX]; 
        printf("%s -> %s\n", argv[i], expand_env_vars(buf, sizeof(buf), argv[i]));
    }
        
    return 0;
}


int main(int argc, char *argv[]) 
{
    rpmdb db;
    //test_expand_env(argc, argv);
    //test_match2();
    log_init(NULL, stdout, "aa");
    rpm_initlib(NULL);
    db = rpm_opendb("/var/lib/rpm", "/", O_RDONLY);
    sleep(20);
    rpm_closedb(db);
    sleep(30);
    return 0;
}
