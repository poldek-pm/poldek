/* $Id$ */

#ifndef POLDEK_RPMHDR_H
#define POLDEK_RPMHDR_H

#include <stdint.h>
#include <rpm/rpmlib.h>

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
