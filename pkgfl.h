/* $Id$ */

#ifndef POLDEK_PKGFL_H
#define POLDEK_PKGFL_H

#include <stdint.h>

#include <trurl/narray.h>
#include <trurl/nstream.h>

#include <rpm/rpmlib.h>

int pkgflmodule_init(void);
void pkgflmodule_destroy(void);
void pkgflmodule_free_unneeded(void);

void *pkgflmodule_allocator_push_mark(void);
void pkgflmodule_allocator_pop_mark(void *ptr);

struct flfile {
    uint32_t  size;
    uint16_t  mode;
    char      basename[0];
};

struct flfile *flfile_new(uint32_t size, uint16_t mode, 
                          const char *basename, int blen, 
                          const char *slinkto, int slen);

/*
  both functions returns true(non-zero) if given files are conflicted
  WARN: basenames aren't compared! 
 */
int flfile_cnfl(const struct flfile *f1, const struct flfile *f2, int strict);
int flfile_cnfl2(const struct flfile *f1, uint32_t size, uint16_t mode,
                 const char *slinkto, int strict);


struct pkgfl_ent {
    char   *dirname;            /* dirname without leading '/' if strlen(dirname) > 1 */
    int    items;
    struct flfile *files[0];
};

struct pkgfl_ent *pkgfl_ent_new(char *dirname, int dirname_len, int nfiles);
void pkgfl_ent_free(struct pkgfl_ent *e);

int pkgfl_ent_cmp(const void *a, const void *b);

#define PKGFL_ALL         0
#define PKGFL_DEPDIRS     1
#define PKGFL_NOTDEPDIRS  2

tn_array *pkgfl_array_store_order(tn_array *fl);
int pkgfl_store(tn_array *fl, tn_buf *nbuf, tn_array *depdirs, int which);
int pkgfl_store_buf(tn_array *fl, tn_buf *nbuf, tn_array *depdirs, int which);
int pkgfl_store_f(tn_array *fl, tn_stream *st, tn_array *depdirs, int which);

tn_array *pkgfl_restore(tn_buf_it *nbufi, tn_array *dirs, int include);
tn_array *pkgfl_restore_f(tn_stream *st, tn_array *dirs, int include);
int pkgfl_skip_f(tn_stream *st);

tn_array *pkgfl_array_new(int size);

int pkgfl_ldhdr(tn_array *fl, Header h, int which, const char *pkgname);


void pkgfl_dump(tn_array *fl);
#endif /* POLDEK_PKGFL_H */



