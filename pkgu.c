/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include <trurl/trurl.h>
#include <rpm/rpmlib.h>

#include "rpmhdr.h"
#include "log.h"
#include "pkgu.h"
#include "h2n.h"


static Header make_pkguinf_hdr(Header h, int *langs_cnt);
static void *pkguinf_tag(struct pkguinf *pkgu, int32_t tag);

#if 0
/* headerLoad bug hunt func  */
void check_hdr(char *s, struct pkguinf *pkgu) 
{
    void *p;

    printf("%s\n", s);
    p = headerUnload(pkgu->_hdr);
    //n_assert(memcmp(pkgu->_hdr, sav, savsize) == 0);
}
#endif 

void pkguinf_free(struct pkguinf *pkgu)
{

    if (pkgu->_refcnt > 0) {
        pkgu->_refcnt--;
        return;
    }
    
    if (pkgu->flags & PKGUINF_MEMB_MALLOCED) {
        if (pkgu->license)
            free(pkgu->license);
        
        if (pkgu->url)
            free(pkgu->url);
        
        if (pkgu->summary)
            free(pkgu->summary);
        
        if (pkgu->description)
            free(pkgu->description);
    }
    
    if (pkgu->_hdr)
        headerFree(pkgu->_hdr);
    
    pkgu->_hdr = NULL;
    free(pkgu);
}

struct pkguinf *pkguinf_link(struct pkguinf *pkgu)
{
    pkgu->_refcnt++;
    return pkgu;
}

struct pkguinf *pkguinf_touser(struct pkguinf *pkgu) 
{
    pkgu->license = pkguinf_tag(pkgu, RPMTAG_COPYRIGHT);
    pkgu->url = pkguinf_tag(pkgu, RPMTAG_URL);
    pkgu->summary = pkguinf_tag(pkgu, RPMTAG_SUMMARY);
    pkgu->description = pkguinf_tag(pkgu, RPMTAG_DESCRIPTION);

    
    return pkgu;
}


static int is_empty(const char *s) 
{
    int is_empty = 1;
    
    while (*s) {
        if (!isspace(*s)) {
            is_empty = 0;
            break;
        }
        s++;
    }
    return is_empty;
}

static void *pkguinf_tag(struct pkguinf *pkgu, int32_t tag)
{
    struct rpmhdr_ent ent;

    n_assert(pkgu->_hdr);
    
    if (!rpmhdr_ent_get(&ent, pkgu->_hdr, tag))
        ent.val = NULL;

    if (ent.val && ent.type == RPM_STRING_TYPE) 
        if (is_empty(ent.val))
            ent.val = NULL;

    return ent.val;
}

#if 0                           /* currently unused  */
static int32_t pkguinf_int32_tag(struct pkguinf *pkgu, int32_t tag)
{
    int32_t *v;
    
    v = pkguinf_tag(pkgu, tag);
    return *v;
}
#endif

int pkguinf_store(struct pkguinf *pkgu, FILE *stream) 
{
    uint16_t nsize, nlangs;
    Header   hdr;
    void     *rawhdr;
    int      rawhdr_size;
    
    int rc;

    /* headerUnload(pkgu->_hdr) gives diffrent a bit raw header(!), so copy tags by hand */

    hdr = make_pkguinf_hdr(pkgu->_hdr, NULL);
    rawhdr_size = headerSizeof(hdr, HEADER_MAGIC_NO);
    rawhdr = headerUnload(hdr);

#if 0    
    printf("> %ld\t%d\n", ftell(stream), headerSizeof(pkgu->_hdr, HEADER_MAGIC_NO));
    headerDump(pkgu->_hdr, stdout, HEADER_DUMP_INLINE, rpmTagTable);
#endif     
    
    nsize = hton16(rawhdr_size);
    nlangs = hton16(pkgu->nlangs);
    
    fwrite(&nlangs, sizeof(nlangs), 1, stream);
    fwrite(&nsize, sizeof(nsize), 1, stream);
    
    rc = fwrite(rawhdr, rawhdr_size, 1, stream);
    
    free(rawhdr);
    
    return rc;
}


struct pkguinf *pkguinf_restore(FILE *stream, off_t offset)
{
    struct pkguinf *pkgu = NULL;
    uint16_t nsize, nlangs;
    void *rawhdr;
    Header hdr;

    
    if (offset > 0)
        if (fseek(stream, offset, SEEK_SET) != 0) {
            log(LOGERR, "pkguinf_restore: fseek %ld: %m\n", offset);
            return NULL;
        }
    
    if (fread(&nlangs, sizeof(nlangs), 1, stream) != 1) {
        log(LOGERR, "pkguinf_restore: read error (%m) at %ld\n", ftell(stream));
        return NULL;
    }
    nlangs = ntoh16(nlangs);
    
    if (fread(&nsize, sizeof(nsize), 1, stream) != 1) {
        log(LOGERR, "pkguinf_restore: read error (%m) at %ld\n", ftell(stream));
        return NULL;
    }
    
    nsize = ntoh16(nsize);
    rawhdr = alloca(nsize);
    
    if (fread(rawhdr, nsize, 1, stream) != 1) {
        log(LOGERR, "pkguinf_restore: read error at %ld\n", ftell(stream));
        return NULL;
    }

    
    if ((hdr = headerLoad(rawhdr)) != NULL) {
        pkgu = malloc(sizeof(*pkgu));
        pkgu->_hdr = headerCopy(hdr); /* propably headerLoad() bug  */
        headerFree(hdr);
        
        pkgu->flags = 0;
        pkgu->nlangs = nlangs;
        pkgu->license = NULL;
        pkgu->url = NULL;
        pkgu->summary = NULL;
        pkgu->description = NULL;
        pkgu->_refcnt = 0;
        
#if 0    
        printf("< %ld\t%d\n", ftell(stream), headerSizeof(pkgu->_hdr, HEADER_MAGIC_NO));
        headerDump(pkgu->_hdr, stdout, HEADER_DUMP_INLINE, rpmTagTable);
#endif        
    }

    return pkgu;
}


int pkguinf_skip(FILE *stream) 
{
    uint16_t nsize, nlangs;

    fseek(stream, sizeof(nlangs), SEEK_CUR);
    if (fread(&nsize, sizeof(nsize), 1, stream) != 1) {
        nsize = 0;
    } else {
        nsize = ntoh16(nsize);
        fseek(stream, nsize, SEEK_CUR);
    }
    
    return nsize;
}


static Header make_pkguinf_hdr(Header h, int *langs_cnt) 
{
    struct rpmhdr_ent  hdrent;
    char               **langs, **summs, **groups, **descrs;
    int                nsumms, ngroups, ndescrs;
    int                i, n, nlangs = 0;
    Header             hdr;
    unsigned           hdr_size;
    
    
    langs = headerGetLangs(h);
    
    headerGetRawEntry(h, RPMTAG_SUMMARY, 0, (void*)&summs, &nsumms);
    headerGetRawEntry(h, RPMTAG_GROUP, 0, (void*)&groups, &ngroups);
    headerGetRawEntry(h, RPMTAG_DESCRIPTION, 0, (void*)&descrs, &ndescrs);

    n = ngroups;
    if (n > nsumms)
        n = nsumms;
    if (n > ndescrs)
        n = ndescrs;

    hdr = headerNew();
    i = 0;
    for (i=0; i<n; i++) {
        if (langs[i] == NULL)
            break;
        headerAddI18NString(hdr, RPMTAG_GROUP, groups[i], langs[i]);
        headerAddI18NString(hdr, RPMTAG_SUMMARY, summs[i], langs[i]);
        headerAddI18NString(hdr, RPMTAG_DESCRIPTION, descrs[i], langs[i]);
    }
    nlangs = n;
    
    free(langs);
    free(groups);
    free(summs);
    free(descrs);

    rpmhdr_ent_cp(&hdrent, h, RPMTAG_VENDOR, hdr);
    rpmhdr_ent_cp(&hdrent, h, RPMTAG_COPYRIGHT, hdr);
    rpmhdr_ent_cp(&hdrent, h, RPMTAG_URL, hdr);
    rpmhdr_ent_cp(&hdrent, h, RPMTAG_DISTRIBUTION, hdr);
    
    
    hdr_size = headerSizeof(hdr, HEADER_MAGIC_NO);
    
    if (hdr_size > UINT16_MAX) {
        log(LOGERR, "hdr size too big: %d\n", hdr_size);
        headerFree(hdr);
        hdr = NULL;
    }

    if (langs_cnt)
        *langs_cnt = nlangs;

    return hdr;
}


struct pkguinf *pkguinf_ldhdr(Header h) 
{
    struct pkguinf *pkgu = NULL;
    int            nlangs = 0;
    Header         hdr;

    if ((hdr = make_pkguinf_hdr(h, &nlangs)) == NULL)
        return NULL;
    
    pkgu = malloc(sizeof(*pkgu));
    pkgu->_hdr = hdr;
    pkgu->flags = 0;
    pkgu->nlangs = nlangs;
    pkgu->license = NULL;
    pkgu->url = NULL;
    pkgu->summary = NULL;
    pkgu->description = NULL;
    	
    return pkgu;
}


