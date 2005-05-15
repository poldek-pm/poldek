/* $Id$ */
#ifndef POLDEK_PKGUINF_H
#define POLDEK_PKGUINF_H

#include <sys/types.h>          /* for off_t */

#include <trurl/trurl.h>
#include <trurl/nstream.h>

#define PKGUINF_LICENSE      'l'
#define PKGUINF_URL          'u'
#define PKGUINF_SUMMARY      's'
#define PKGUINF_DESCRIPTION  'd'
#define PKGUINF_VENDOR       'v'
#define PKGUINF_BUILDHOST    'b'
#define PKGUINF_DISTRO       'D'

struct pkguinf;

const char *pkguinf_getstr(const struct pkguinf *pkgu, int tag);

struct pkguinf *pkguinf_link(struct pkguinf *pkgu);
void pkguinf_free(struct pkguinf *pkgu);


tn_array *pkguinf_langs(struct pkguinf *pkgu);

int pkguinf_store_rpmhdr(struct pkguinf *pkgu, tn_buf *nbuf);
int pkguinf_store_rpmhdr_st(struct pkguinf *pkgu, tn_stream *st);
int pkguinf_skip_rpmhdr(tn_stream *st);
struct pkguinf *pkguinf_restore_rpmhdr_st(tn_alloc *na,
                                          tn_stream *st, off_t offset);

struct pkguinf *pkguinf_ldrpmhdr(tn_alloc *na, void *hdr);

tn_buf *pkguinf_store(const struct pkguinf *pkgu, tn_buf *nbuf,
                      const char *lang);
struct pkguinf *pkguinf_restore(tn_alloc *na, tn_buf_it *it, const char *lang);
int pkguinf_restore_i18n(struct pkguinf *pkgu, tn_buf_it *it, const char *lang);


#endif
        
