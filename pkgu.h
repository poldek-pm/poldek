/* $Id$ */
#ifndef POLDEK_PKGUINF_H
#define POLDEK_PKGUINF_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <trurl/trurl.h>
#include <trurl/nstream.h>

#include <rpm/rpmlib.h>

#define PKGUINF_MEMB_MALLOCED (1 << 0)

struct pkguinf {
    char              *license;
    char              *url;
    char              *summary;
    char              *description;
    char              *vendor;
    char              *buildhost;
    char              *distro;
    
    tn_hash           *_ht;
    tn_array          *_langs;
    tn_array          *_langs_rpmhdr; /* v018x legacy: for preserving
                                         the langs order */
    int               _refcnt;
};

struct pkguinf *pkguinf_link(struct pkguinf *pkgu);

tn_array *pkguinf_langs(struct pkguinf *pkgu);

int pkguinf_store_rpmhdr(struct pkguinf *pkgu, tn_buf *nbuf);
int pkguinf_store_rpmhdr_st(struct pkguinf *pkgu, tn_stream *st);
struct pkguinf *pkguinf_restore_rpmhdr_st(tn_stream *st, off_t offset);
int pkguinf_skip_rpmhdr(tn_stream *st);

struct pkguinf *pkguinf_ldhdr(Header h);
void pkguinf_free(struct pkguinf *pkgu);



tn_buf *pkguinf_store(const struct pkguinf *pkgu, tn_buf *nbuf,
                      const char *lang);

struct pkguinf *pkguinf_restore(tn_buf_it *it, const char *lang);
int pkguinf_restore_i18n(struct pkguinf *pkgu, tn_buf_it *it, const char *lang);


#endif
        
