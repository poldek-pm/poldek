/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_PKGROUP_IDX_H
#define POLDEK_PKGROUP_IDX_H

#include <trurl/nstream.h>
#include <trurl/nbuf.h>

struct pkgroup_idx;

struct pkgroup_idx *pkgroup_idx_new(void);
void pkgroup_idx_free(struct pkgroup_idx *idx);
struct pkgroup_idx *pkgroup_idx_link(struct pkgroup_idx *idx);

int pkgroup_idx_store(struct pkgroup_idx *idx, tn_buf *nbuf);
struct pkgroup_idx *pkgroup_idx_restore(tn_buf_it *it, unsigned flags);
struct pkgroup_idx *pkgroup_idx_restore_st(tn_stream *st, unsigned flags);


int pkgroup_idx_add(struct pkgroup_idx *idx, const char *group);
int pkgroup_idx_add_i18n(struct pkgroup_idx *idx, int groupid,
                         const char *group, const char *lang);

int pkgroup_idx_update_rpmhdr(struct pkgroup_idx *idx, void *rpmhdr);
const char *pkgroup(struct pkgroup_idx *idx, int groupid);

int pkgroup_idx_remap_groupid(struct pkgroup_idx *idx_to,
                              struct pkgroup_idx *idx_from,
                              int groupid, int merge);

#endif
