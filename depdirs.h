/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_DEPDIRS_H
#define POLDEK_DEPDIRS_H

#include <trurl/narray.h>

void init_depdirs(tn_array *dirnames);
void destroy_depdirs(void);

int in_depdirs(const char *dir);
int in_depdirs_l(const char *dir, int dirlen);

char *path2depdir(char *path);

#endif /* POLDEK_DEPDIRS_H */
