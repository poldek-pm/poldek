/* $Id$ */
#ifndef POLDEK_PROVREQIDX_H
#define POLDEK_PROVREQIDX_H

#include <stdint.h>
#include <trurl/nhash.h>
#include <trurl/nmalloc.h>

#define CAPREQ_IDX_CAP (1 << 0)
#define CAPREQ_IDX_REQ (1 << 1)



struct capreq_idx {
    unsigned flags;
    tn_hash  *ht;       /* name => *pkgs[] */
    tn_alloc *na;
};

struct pkg;
struct capreq_idx_ent {
    int16_t items;
    int16_t _size;
    union {
        struct pkg *pkg;
//    struct pkg *pkgs[0];       /* pkgs list */
        struct pkg **pkgs;       /* pkgs list */
    } capreq_idx_ent_pkg;
};

#define	crent_pkg    capreq_idx_ent_pkg.pkg
#define	crent_pkgs   capreq_idx_ent_pkg.pkgs

int capreq_idx_init(struct capreq_idx *idx, unsigned type, int nelem);
void capreq_idx_destroy(struct capreq_idx *idx);

int capreq_idx_add(struct capreq_idx *idx, const char *capname,
                   struct pkg *pkg, int isprov);

void capreq_idx_remove(struct capreq_idx *idx, const char *capname,
                       struct pkg *pkg);

const struct capreq_idx_ent *capreq_idx_lookup(struct capreq_idx *idx,
                                               const char *capname);

#endif /* POLDEK_CAPREQIDX_H */
    
    
