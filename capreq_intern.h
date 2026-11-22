/*
  Copyright (C) 2026 PLD Linux Team <feedback@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_CAPREQ_INTERN_H
#define POLDEK_CAPREQ_INTERN_H

#include <stddef.h>

#define CAPREQ_VR_MAX      128

/* capreq.c: true if version and release of vr_len characters (together) fit
   in CAPREQ_VR_MAX; logs a warning about name if not */
int capreq__check_vr_limit(const char *name, size_t vr_len);

#endif
