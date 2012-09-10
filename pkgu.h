/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_PKGUINF_H
#define POLDEK_PKGUINF_H

#include <sys/types.h>          /* for off_t */

#include <trurl/trurl.h>
#include <trurl/nstream.h>

#ifndef EXPORT
# define EXPORT extern
#endif

#define PKGUINF_LICENSE      'l'
#define PKGUINF_URL          'u'
#define PKGUINF_SUMMARY      's'
#define PKGUINF_DESCRIPTION  'd'
#define PKGUINF_VENDOR       'v'
#define PKGUINF_BUILDHOST    'b'
#define PKGUINF_DISTRO       'D'
#define PKGUINF_LEGACY_SOURCERPM    'S'
#define PKGUINF_CHANGELOG    'C'

struct pkguinf;

EXPORT struct pkguinf *pkguinf_new(tn_alloc *na);
EXPORT struct pkguinf *pkguinf_link(struct pkguinf *pkgu);
EXPORT void pkguinf_free(struct pkguinf *pkgu);


EXPORT const char *pkguinf_get(const struct pkguinf *pkgu, int tag);
EXPORT int pkguinf_set(struct pkguinf *pkgu, int tag, const char *val,
                const char *lang);

EXPORT const char *pkguinf_get_changelog(struct pkguinf *inf, time_t since);
EXPORT int pkguinf_changelog_with_security_fixes(struct pkguinf *inf, time_t since);

EXPORT tn_array *pkguinf_langs(struct pkguinf *pkgu);

#ifndef SWIG

EXPORT int pkguinf_store_rpmhdr(struct pkguinf *pkgu, tn_buf *nbuf);
EXPORT int pkguinf_store_rpmhdr_st(struct pkguinf *pkgu, tn_stream *st);
EXPORT int pkguinf_skip_rpmhdr(tn_stream *st);
EXPORT struct pkguinf *pkguinf_restore_rpmhdr_st(tn_alloc *na,
                                          tn_stream *st, off_t offset);

EXPORT struct pkguinf *pkguinf_ldrpmhdr(tn_alloc *na, void *hdr, tn_array *loadlangs);

EXPORT tn_buf *pkguinf_store(const struct pkguinf *pkgu, tn_buf *nbuf,
                      const char *lang);
EXPORT struct pkguinf *pkguinf_restore(tn_alloc *na, tn_buf_it *it, const char *lang);
EXPORT int pkguinf_restore_i18n(struct pkguinf *pkgu, tn_buf_it *it, const char *lang);

#endif /* ndef SWIG */

#endif
        
