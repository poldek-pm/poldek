/* $Id$ */

#ifndef POLDEK_PKGFL_H
#define POLDEK_PKGFL_H

#include <stdint.h>

#include <trurl/narray.h>
#include <rpm/rpmlib.h>

int pkgflmodule_init(void);
void pkgflmodule_destroy(void);
void pkgflmodule_free_unneeded(void);


struct flfile {
    uint32_t  size;
    uint16_t  mode;
    char      basename[0];
};

struct flfile *flfile_new(uint32_t size, uint16_t mode, 
                          const char *basename, int blen, 
                          const char *slinkto, int slen);

int flfile_cmp(const struct flfile *f1, const struct flfile *f2, int strict);


struct pkgfl_ent {
    char   *dirname;
    int    items;
    struct flfile *files[0];
};

struct pkgfl_ent *pkgfl_ent_new(char *dirname, int dirname_len, int nfiles);
void pkgfl_ent_free(struct pkgfl_ent *e);

int pkgfl_ent_cmp(const void *a, const void *b);

#define PKGFL_ALL         0
#define PKGFL_DEPDIRS     1
#define PKGFL_NOTDEPDIRS  2

int pkgfl_asftag(tn_array *fl, char **ftag, int which);

#define pkgfl_array_new(size) n_array_new(size, NULL, pkgfl_ent_cmp)

int pkgfl_ldhdr(tn_array *fl, Header h, const char *errprefix);

#endif /* POLDEK_PKGFL_H */



