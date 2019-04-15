/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef PKGDIR_METADATA_LOAD_H
#define PKGDIR_METADATA_LOAD_H

void metadata_loadmod_init(void);
void metadata_loadmod_destroy(void);

struct vfile;

struct repomd_ent {
    char type[32];
    char checksum[128];
    char checksum_type[8];
    time_t ts;
    struct vfile *vf;
    char location[0];
};

/* name => repomd_ent */
tn_hash *metadata_load_repomd(const char *path);
tn_array *metadata_load_primary(struct pkgdir *pkgdir, const char *path);

#endif
