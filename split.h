/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_SPLIT_H
#define POLDEK_SPLIT_H

int packages_set_priorities(tn_array *pkgs, const char *priconf_path);

int packages_split(tn_array *pkgs, unsigned split_size,
                   unsigned first_free_space, const char *outprefix);

#endif
