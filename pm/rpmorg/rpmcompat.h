/*
  Copyright (C) 2026 PLD Linux Team <feedback@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_PM_RPMCOMPAT_H
#define POLDEK_PM_RPMCOMPAT_H

#include <rpm/rpmds.h>

/* rpmdsUname() and rpmdsCpuinfo() come from PLD's rpm patches; rpm without
   them satisfies no uname() or cpuinfo() requirement, so these add nothing */
#ifndef HAVE_RPMDSUNAME
static inline int rpmdsUname(rpmds *dsp)
{
    (void)dsp;
    return 0;
}
#endif

#ifndef HAVE_RPMDSCPUINFO
static inline int rpmdsCpuinfo(rpmds *dsp)
{
    (void)dsp;
    return 0;
}
#endif

#endif /* POLDEK_PM_RPMCOMPAT_H */
