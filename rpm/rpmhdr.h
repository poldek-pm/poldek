/* $Id$ */

#ifndef POLDEK_RPMHDR_H
#define POLDEK_RPMHDR_H

#include <stdint.h>
#include <rpm/rpmlib.h>


int rpmhdr_loadfdt(FD_t fdt, Header *hdr, const char *path);
int rpmhdr_loadfile(const char *path, Header *hdr);
int rpmhdr_nevr(Header h, char **name,
                uint32_t **epoch, char **version, char **release);
char **rpmhdr_langs(Header h);
void rpmhdr_free_entry(void *e, int type);

#define rpmhdr_issource(h) headerIsEntry((h), RPMTAG_SOURCEPACKAGE)

char *rpmhdr_snprintf(char *buf, size_t size, Header h);

struct rpmhdr_ent {
    int32_t tag;
    int32_t type;
    void *val;
    int32_t cnt;
};

#define rpmhdr_ent_as_int32(ent) (*(int32_t*)(ent)->val)
#define rpmhdr_ent_as_int16(ent) (*(int16_t*)(ent)->val)
#define rpmhdr_ent_as_str(ent) (char*)(ent)->val
#define rpmhdr_ent_as_strarr(ent) (char**)(ent)->val

int rpmhdr_ent_get(struct rpmhdr_ent *ent, Header h, int32_t tag);
void rpmhdr_ent_free(struct rpmhdr_ent *ent);
int rpmhdr_ent_cp(struct rpmhdr_ent *ent, Header h, int32_t tag, Header toh);

#endif
