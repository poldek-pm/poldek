/*
  Package file list module

  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_PKGFL_H
#define POLDEK_PKGFL_H

#include <stdint.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <trurl/trurl.h>

#ifndef EXPORT
#  define EXPORT extern
#endif

struct flfile {
    uint32_t  size;
    uint16_t  mode;
    char      basename[0];
};

EXPORT struct flfile *flfile_new(tn_alloc *na, uint32_t size, uint16_t mode,
                          const char *basename, int blen,
                          const char *slinkto, int slen);

EXPORT struct flfile *flfile_clone(struct flfile *flfile);

EXPORT int flfile_cmp(const struct flfile *f1, const struct flfile *f2);
EXPORT int flfile_cmp_qsort(const struct flfile **f1, const struct flfile **f2);

/*
  both functions returns true(non-zero) if given files are conflicted
  WARN: basenames aren't compared! 
 */
EXPORT int flfile_cnfl(const struct flfile *f1, const struct flfile *f2, int strict);
EXPORT int flfile_cnfl2(const struct flfile *f1, uint32_t size, uint16_t mode,
                 const char *slinkto, int strict);

struct pkgfl_ent {
    char     *dirname; /* dirname without leading '/' if strlen(dirname) > 1 */
    int32_t  items;
    struct flfile *files[0];
};

EXPORT struct pkgfl_ent *pkgfl_ent_new(tn_alloc *na,
                                char *dirname, int dirname_len, int nfiles);

EXPORT int pkgfl_ent_cmp(const void *a, const void *b);

#define PKGFL_ALL         0
#define PKGFL_DEPDIRS     1
#define PKGFL_NOTDEPDIRS  2

EXPORT tn_tuple *pkgfl_array_store_order(tn_tuple *fl);

struct pkg;

/* legacy */
EXPORT tn_tuple *pkgfl_array_pdir_sort(tn_tuple *fl);

EXPORT int pkgfl_store(tn_tuple *fl, tn_buf *nbuf, tn_array *exclpath,
                tn_array *depdirs, int which);

EXPORT int pkgfl_restore_st(tn_alloc *na, tn_tuple **fl, 
                     tn_stream *st, tn_array *dirs, int include);

EXPORT int pkgfl_skip_st(tn_stream *st);

EXPORT tn_array *pkgfl_array_new(int size);

EXPORT void pkgfl_dump(tn_tuple *fl);

/* iterator */
struct pkgfl_it {
    tn_tuple *fl;
    struct pkgfl_ent *flent;
    int i, j;
    char path[PATH_MAX], *endp;
};

EXPORT struct pkgfl_it *pkgfl_it_new(tn_tuple *fl);
EXPORT void pkgfl_it_init(struct pkgfl_it *it, tn_tuple *fl);
EXPORT const char *pkgfl_it_get(struct pkgfl_it *it, struct flfile **flfile);

/* to simplify python wrapper */
EXPORT const char *pkgfl_it_get_rawargs(struct pkgfl_it *it, uint32_t *size, uint16_t *mode,
                                 const char **basename);

/* extract owned and required directories */
EXPORT int pkgfl_owned_and_required_dirs(tn_tuple *fl, tn_array **owned,
                                  tn_array **required);

#endif /* POLDEK_PKGFL_H */



