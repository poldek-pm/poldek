/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef  POLDEK_PKGDIR_PKG_STORE_H
#define  POLDEK_PKGDIR_PKG_STORE_H

#include <stdint.h>

#include <trurl/trurl.h>

#define PKG_STORETAG_NAME  'N'
#define PKG_STORETAG_EVR   'V'
#define PKG_STORETAG_ARCH  'A'
#define PKG_STORETAG_OS    'O'
#define PKG_STORETAG_FN    'n'
#define PKG_STORETAG_SRCFN 's'
#define PKG_STORETAG_BINF  'f'
#define PKG_STORETAG_CAPS  'P'
#define PKG_STORETAG_REQS  'R'
#define PKG_STORETAG_SUGS  'S'
#define PKG_STORETAG_CNFLS 'C'
#define PKG_STORETAG_FL    'l'
#define PKG_STORETAG_DEPFL 'L'
#define PKG_STORETAG_CONT  '_'  /* continuation */
#define PKG_STORETAG_UINF  'U'  /* depreciated */

#define PKG_STORETAG_SIZENIL  0 /* ascii tag */
#define PKG_STORETAG_SIZE8   '1'
#define PKG_STORETAG_SIZE16  '2'
#define PKG_STORETAG_SIZE32  '4'

struct pkg_store_tag {
    int8_t tag;
    int8_t binsize;
    char   *descr;
};

int pkg_store_skiptag(int tag, int tag_binsize, tn_stream *st);

#define PKGSTORE_NODESC      (1 << 0)
#define PKGSTORE_NOEVR       (1 << 1)
#define PKGSTORE_NOARCH      (1 << 2)
#define PKGSTORE_NOOS        (1 << 3)
#define PKGSTORE_NOFL        (1 << 4)
#define PKGSTORE_NODEPFL     (1 << 5)
#define PKGSTORE_NOANYFL     (PKGSTORE_NOFL | PKGSTORE_NODEPFL)
#define PKGSTORE_NOTIMESTAMP (1 << 6) /* pdir only (0.18.x compat mode) */
#define PKGSTORE_RECNO       (1 << 7)

struct pkg;
int pkg_store(const struct pkg *pkg, tn_buf *nbuf, tn_array *depdirs,
              tn_array *exclpath, unsigned flags);

int pkg_store_st(const struct pkg *pkg, tn_stream *st, tn_array *depdirs,
                 unsigned flags);

struct pkg_offs {
    off_t  nodep_files_offs;  /* no dep files offset in index */
    off_t  pkguinf_offs;
};

struct pkg *pkg_restore_st(tn_stream *st, tn_alloc *na, struct pkg *pkg,
                           tn_array *depdirs, unsigned ldflags,
                           struct pkg_offs *pkgo, const char *fn);



#endif
