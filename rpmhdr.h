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

char *rpmhdr_strnvr(char *buf, int size, Header h);

struct rpmhdr_fl {
    char      **bnames;
    int       nbnames; 

    char      **dnames;
    int       ndnames;
    
    char      **symlinks;
    int       nsymlinks;
    
    int32_t   *diridxs;
    int       ndiridxs;
    
    uint32_t  *sizes;
    int       nsizes;
    
    uint16_t  *modes;
    int       nmodes;

    Header    h;
};

int rpmhdr_fl_ld(struct rpmhdr_fl *hdrfl, Header h, const char *pkgname);
void rpmhdr_fl_free(struct rpmhdr_fl *fl);

#endif
