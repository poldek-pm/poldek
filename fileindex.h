/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_FILEINDEX_H
#define POLDEK_FILEINDEX_H

#include <trurl/nhash.h>
#include <trurl/nmalloc.h>

#include "pkgfl.h"

struct pkg_file_cnfl {
    uint8_t       shared;
    struct pkg    *pkg0;
    struct pkg    *pkg1;
    char          msg[0];
};

struct file_index {
    tn_hash   *dirs;             /* dirname => tn_array *files */
    tn_alloc  *na;
};

struct file_index *file_index_new(int nelem);
void file_index_free(struct file_index *fi);

void file_index_setup(struct file_index *fi);

void *file_index_add_dirname(struct file_index *fi, const char *dirname);


int file_index_add_basename(struct file_index *fi, void *fidx_dir,
                            struct flfile *flfile,
                            struct pkg *pkg);

void file_index_setup_idxdir(void *fdn);

int file_index_remove(struct file_index *fi, const char *dirname,
                      const char *basename,
                      struct pkg *pkg);

int file_index_lookup(const struct file_index *fi,
                      const char *apath, int apath_len,
                      struct pkg *pkgs[], int size);

int file_index_report_conflicts(const struct file_index *fi, tn_array *pkgs);
int file_index_report_orphans(const struct file_index *fi, tn_array *pkgs);

struct pkgset;
int file_index_report_semiorphans(struct pkgset *ps, tn_array *pkgs);
#endif /* POLDEK_FILEINDEX_H */
