/* $Id$ */

#ifndef POLDEK_PKGFL_H
#define POLDEK_PKGFL_H

#include <stdint.h>

#include <trurl/narray.h>
#include <trurl/nstream.h>
#include <trurl/nbuf.h>
#include <trurl/ntuple.h>
#include <trurl/nmalloc.h>

struct flfile {
    uint32_t  size;
    uint16_t  mode;
    char      basename[0];
};

struct flfile *flfile_new(tn_alloc *na, uint32_t size, uint16_t mode, 
                          const char *basename, int blen, 
                          const char *slinkto, int slen);


int flfile_cmp(const struct flfile *f1, const struct flfile *f2);
int flfile_cmp_qsort(const struct flfile **f1, const struct flfile **f2);
#if 0

/*
  both functions returns true(non-zero) if given files are conflicted
  WARN: basenames aren't compared! 
 */
int flfile_cnfl(const struct flfile *f1, const struct flfile *f2, int strict);
int flfile_cnfl2(const struct flfile *f1, uint32_t size, uint16_t mode,
                 const char *slinkto, int strict);

#endif

struct pkgfl_ent {
    char     *dirname; /* dirname without leading '/' if strlen(dirname) > 1 */
    int32_t  items;
    struct flfile *files[0];
};

struct pkgfl_ent *pkgfl_ent_new(tn_alloc *na,
                                char *dirname, int dirname_len, int nfiles);

int pkgfl_ent_cmp(const void *a, const void *b);

#define PKGFL_ALL         0
#define PKGFL_DEPDIRS     1
#define PKGFL_NOTDEPDIRS  2

tn_tuple *pkgfl_array_store_order(tn_tuple *fl);

struct pkg;

int pkgfl_store(tn_tuple *fl, tn_buf *nbuf, tn_array *exclpath,
                tn_array *depdirs, int which);

int pkgfl_restore(tn_alloc *na, tn_tuple **fl, 
                  tn_buf_it *nbufi, tn_array *dirs, int include);

int pkgfl_restore_st(tn_alloc *na, tn_tuple **fl, 
                     tn_stream *st, tn_array *dirs, int include);

int pkgfl_skip_st(tn_stream *st);

tn_array *pkgfl_array_new(int size);

void pkgfl_dump(tn_tuple *fl);


#endif /* POLDEK_PKGFL_H */



