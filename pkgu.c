#include <stdio.h>
#include <stdlib.h>


#include <trurl/trurl.h>
#include <rpm/rpmlib.h>

#include "rpmhdr.h"
#include "log.h"
#include "pkgu.h"
#include "h2n.h"

void pkguinf_free(struct pkguinf *pkgu) 
{
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
    
    if (pkgu->hdr)
        headerFree(pkgu->hdr);
    
    if (pkgu->rawhdr) 
            free(pkgu->rawhdr);
    
    free(pkgu);
}


void *pkguinf_tag(struct pkguinf *pkgu, int32_t tag)
{
    struct rpmhdr_ent ent;
    
    if (pkgu->hdr == NULL && pkgu->rawhdr != NULL) {
        if ((pkgu->hdr = headerLoad(pkgu->rawhdr)) == NULL)
            return 0;
        free(pkgu->rawhdr);
        pkgu->rawhdr = NULL;
    }

    if (!rpmhdr_ent_get(&ent, pkgu->hdr, tag))
        ent.val = NULL;

    return ent.val;
}

int32_t pkguinf_int32_tag(struct pkguinf *pkgu, int32_t tag)
{
    int32_t *v;
    
    v = pkguinf_tag(pkgu, tag);
    return *v;
}


int pkguinf_store(struct pkguinf *pkgu, FILE *stream) 
{
    uint16_t nsize = hton16(pkgu->rawhdr_size);
    uint16_t nlangs = hton16(pkgu->nlangs);
    n_assert(pkgu->rawhdr_size);
    
    fwrite(&nlangs, sizeof(nlangs), 1, stream);
    fwrite(&nsize, sizeof(nsize), 1, stream);
    return fwrite(pkgu->rawhdr, pkgu->rawhdr_size, 1, stream);
}


struct pkguinf *pkguinf_restore(FILE *stream, off_t offset)
{
    struct pkguinf *pkgu = NULL;
    uint16_t nsize, nlangs;
    void *rawhdr;
    Header hdr;

    
    if (offset > 0)
        fseek(stream, SEEK_SET, offset);
    
    if (fread(&nlangs, sizeof(nlangs), 1, stream) != 1) {
        log(LOGERR, "pkguinf_restore: read error at %ld\n", ftell(stream));
        return NULL;
    }
    nlangs = ntoh16(nlangs);
    
    if (fread(&nsize, sizeof(nsize), 1, stream) != 1) {
        log(LOGERR, "pkguinf_restore: read error at %ld\n", ftell(stream));
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
        pkgu->hdr = hdr;
        pkgu->flags = 0;
        pkgu->nlangs = nlangs;
        pkgu->rawhdr = NULL;
        pkgu->rawhdr_size = 0;
        pkgu->license = NULL;
        pkgu->url = NULL;
        pkgu->summary = NULL;
        pkgu->description = NULL;
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

struct pkguinf *pkguinf_ldhdr(Header h) 
{
    char       **langs, **summs, **groups, **descrs;
    int        nsumms, ngroups, ndescrs;
    struct rpmhdr_ent hdrent;
    struct pkguinf *pkgu;
    int        i, n, nlangs = 0;
    Header     hdr;
    void       *hdr_ptr;
    unsigned   hdr_size;
    
    
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
        headerAddI18NString(hdr, RPMTAG_SUMMARY, descrs[i], langs[i]);
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
    hdr_ptr = headerUnload(hdr);
    headerFree(hdr);

    if (hdr_size > UINT16_MAX) {
        log(LOGERR, "hdr size too big: %d\n");
        pkgu = NULL;
    } else {
        pkgu = malloc(sizeof(*pkgu));
        pkgu->flags = 0;
        pkgu->nlangs = nlangs;
        pkgu->rawhdr = hdr_ptr;
        pkgu->rawhdr_size = hdr_size;
        pkgu->hdr = NULL;
        pkgu->license = NULL;
        pkgu->url = NULL;
        pkgu->summary = NULL;
        pkgu->description = NULL;
    }
    	
    return pkgu;
}


