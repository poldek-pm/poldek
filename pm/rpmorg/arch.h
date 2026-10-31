/*
  Copyright (C) 2026 PLD Linux Team <feedback@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_PM_RPMORG_ARCH_H
#define POLDEK_PM_RPMORG_ARCH_H

/* Canonical machine architecture as reported by rpm.
   The returned string is freshly allocated and must be freed by the
   caller. Returns NULL only if rpm initialization fails. */
char *pm_rpm_arch(void);

#endif /* POLDEK_PM_RPMORG_ARCH_H */
