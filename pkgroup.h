/* $Id$ */
#ifndef POLDEK_PKGROUP_IDX_H
#define POLDEK_PKGROUP_IDX_H

#include <rpm/rpmlib.h>

extern const char *pkgroups_tag;

struct pkgroup_idx;

struct pkgroup_idx *pkgroup_idx_new(void);
void pkgroup_idx_free(struct pkgroup_idx *idx);
struct pkgroup_idx *pkgroup_idx_link(struct pkgroup_idx *idx);

int pkgroup_idx_store(struct pkgroup_idx *idx, FILE *stream);
struct pkgroup_idx *pkgroup_idx_restore(FILE *stream, unsigned flags);
int pkgroup_idx_update(struct pkgroup_idx *idx, Header h);
const char *pkgroup(struct pkgroup_idx *idx, int groupid);

int pkgroup_idx_remap_groupid(struct pkgroup_idx *idx_to,
                              struct pkgroup_idx *idx_from,
                              int groupid);

#endif
