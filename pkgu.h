/* $Id$ */
#ifndef POLDEK_PKGUINF_H
#define POLDEK_PKGUINF_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <trurl/trurl.h>
#include <rpm/rpmlib.h>

#define PKGUINF_MEMB_MALLOCED (1 << 0)

struct pkguinf {
    uint16_t          flags;
    uint16_t          nlangs;
    char              *license;
    char              *url;
    char              *summary;
    char              *description;
    char              *vendor;
    char              *buildhost;
    
    Header            _hdr;
    int               _refcnt;
};

struct pkguinf *pkguinf_link(struct pkguinf *pkgu);
struct pkguinf *pkguinf_touser(struct pkguinf *pkgu);

int pkguinf_store(struct pkguinf *pkgu, FILE *stream);
struct pkguinf *pkguinf_restore(FILE *stream, off_t offset);
int pkguinf_skip(FILE *stream);

struct pkguinf *pkguinf_ldhdr(Header h);
void pkguinf_free(struct pkguinf *pkgu);

#endif
        
