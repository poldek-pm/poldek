/* 
  Copyright (C) 2000 - 2004 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>
#include <trurl/nstream.h>
#include <trurl/n_snprintf.h>
#include <trurl/nmalloc.h>

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "pkgdir.h"
#include "pkg.h"
#include "pkgu.h"

#include "pndir.h"


tn_hash *pndir_db_dscr_h_new(void)
{
    return n_hash_new(16, (tn_fn_free)tndb_close);
}

static
struct tndb *do_db_dscr_open(const char *path) 
{
    struct tndb *db;

    db = tndb_creat(path, PNDIR_COMPRLEVEL, TNDB_SIGN_DIGEST);
    if (db == NULL) {
        logn(LOGERR, "%s: %m\n", path);
        return 0;
    }
    return db;
}

const char *pndir_db_dscr_idstr(const char *lang,
                                const char **idstr, const char **langstr) 
{
    const char *id = "i18n";

    *langstr = id;
    *idstr = id;
    
    if (lang == NULL || strcmp(lang, "C") == 0) {
        *langstr = "";
        *idstr = "C";
    }
    
    return *idstr;
}


static
struct tndb *do_db_dscr_get(tn_hash *db_dscr_h, const char *pathtmpl,
                            const char *lang, int open) 
{
    char path[PATH_MAX], *dot = ".";
    const char *idstr, *langstr;
    struct tndb *db;

    
    pndir_db_dscr_idstr(lang, &idstr, &langstr);    
    if (*langstr == '\0')
        dot = "";
    
    if ((db = n_hash_get(db_dscr_h, idstr)) != NULL)
        return db;
    
    if (open == 0)
        return NULL;

    n_assert(pathtmpl);
    snprintf(path, sizeof(path), pathtmpl, dot, langstr);
    db = do_db_dscr_open(path);
    if (db == NULL)
        return NULL;
    
    n_hash_insert(db_dscr_h, idstr, db);
    return db;
}

struct tndb *pndir_db_dscr_h_dbcreat(tn_hash *db_dscr_h, const char *pathtmpl,
                                     const char *lang)
{
    return do_db_dscr_get(db_dscr_h, pathtmpl, lang, 1);
}


int pndir_db_dscr_h_insert(tn_hash *db_dscr_h,
                           const char *lang, struct tndb *db)
{
    const char *idstr, *langstr;
    pndir_db_dscr_idstr(lang, &idstr, &langstr);
    return n_hash_insert(db_dscr_h, idstr, db) != NULL;
}


struct tndb *pndir_db_dscr_h_get(tn_hash *db_dscr_h, const char *lang) 
{
    return do_db_dscr_get(db_dscr_h, NULL, lang, 0);
}


struct pkguinf *pndir_load_pkguinf(tn_alloc *na, tn_hash *db_dscr_h,
                                   const struct pkg *pkg, tn_array *langs)
{
    struct pkguinf   *pkgu = NULL;
    struct tndb      *dbC;
    tn_buf           *nbuf = NULL;
    char             key[TNDB_KEY_MAX], val[4096];
    int              klen, vlen;
    
    if ((dbC = pndir_db_dscr_h_get(db_dscr_h, "C")) == NULL) 
        return NULL;

    klen = pndir_make_pkgkey(key, sizeof(key), pkg);
    if (klen <= 0)
        return NULL;

    if ((vlen = tndb_get(dbC, key, klen, val, sizeof(val))) > 0) {
        tn_buf_it  it;
        nbuf = n_buf_new(0);
        n_buf_init(nbuf, val, vlen);
        n_buf_it_init(&it, nbuf);
        pkgu = pkguinf_restore(na, &it, "C");
    }

    if (pkgu && langs) {
        int i;
                
        for (i = n_array_size(langs) - 1; i >= 0; i--) {
            struct tndb *db;
            const char *lang;
            char dkey[512];
            int  dklen;

            lang = n_array_nth(langs, i);
            if (strcmp(lang, "C") == 0)
                continue;
                
            if ((db = pndir_db_dscr_h_get(db_dscr_h, lang)) == NULL)
                continue;

            dklen = n_snprintf(dkey, sizeof(dkey), "%s%s", key, lang);
            vlen = tndb_get(db, dkey, dklen, val, sizeof(val));
            //printf("ld %s: %s (%d)\n", pkg_snprintf_s(pkg), lang, vlen);
            if (vlen > 0) {
                tn_buf_it it;
                n_buf_clean(nbuf);
                n_buf_init(nbuf, val, vlen);
                n_buf_it_init(&it, nbuf);
                pkguinf_restore_i18n(pkgu, &it, lang);
            }
        }
    }
    
    if (nbuf)
        n_buf_free(nbuf);
    return pkgu;
}
