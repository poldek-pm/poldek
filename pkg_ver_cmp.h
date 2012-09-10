/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_PKG_VER_CMP_H
#define POLDEK_PKG_VER_CMP_H

extern int pm_rpm_arch_score(const char *arch);
extern int pm_rpm_vercmp(const char *one, const char *two);

#define pkg_version_compare(v1, v2) pm_rpm_vercmp(v1, v2)
#define pm_architecture_score(arch) pm_rpm_arch_score(arch)

#endif
