/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_CAPREQ_IDX_H
#define POLDEK_CAPREQ_IDX_H

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
    int32_t _size;
    union {
        struct pkg *pkg;
        struct pkg **pkgs;       /* pkgs list */
    } capreq_idx_ent_pkg;
};

#define	crent_pkg    capreq_idx_ent_pkg.pkg
#define	crent_pkgs   capreq_idx_ent_pkg.pkgs

int capreq_idx_init(struct capreq_idx *idx, unsigned type, int nelem);
void capreq_idx_destroy(struct capreq_idx *idx);

int capreq_idx_add(struct capreq_idx *idx, const char *capname,
                   struct pkg *pkg);

void capreq_idx_remove(struct capreq_idx *idx, const char *capname,
                       struct pkg *pkg);

const struct capreq_idx_ent *capreq_idx_lookup(struct capreq_idx *idx,
                                               const char *capname);

#endif /* POLDEK_CAPREQIDX_H */
    
    
