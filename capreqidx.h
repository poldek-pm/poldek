/* $Id$ */
#ifndef POLDEK_PROVREQIDX_H
#define POLDEK_PROVREQIDX_H

#include <stdint.h>
#include <obstack.h>
#include <trurl/nhash.h>

#include "pkg.h"

struct capreq_idx {
    tn_hash  *ht;         /* name => *pkgs[] */
    struct   obstack obs;
};

struct capreq_idx_ent {
    int16_t items;
    int16_t _size;
    struct pkg *pkgs[0];       /* pkgs list */
};

int capreq_idx_init(struct capreq_idx *idx, int nelem);
void capreq_idx_destroy(struct capreq_idx *idx);

int capreq_idx_add(struct capreq_idx *idx,
                    const char *prname,
                    struct pkg *pkg, int isprov);
const
struct capreq_idx_ent *capreq_idx_lookup(struct capreq_idx *idx,
                                         const char *prname);

#endif /* POLDEK_CAPREQIDX_H */
    
    
