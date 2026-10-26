/*
  Copyright (C) 2026 PLD Linux Team <feedback@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_PM_HOOKS_H
#define POLDEK_PM_HOOKS_H

struct poldek_ts;

/* Runs <hooks dir>/pre-install.d before the first package manager run of
   ts; sudo is how that run is made (NULL: directly). Returns 0 if a hook
   failed, then and on every later call of the same ts. */
int poldek__ts_hooks_pre(struct poldek_ts *ts, const char *sudo);

/* true if the pre-install hooks of ts failed */
int poldek__ts_hooks_failed(const struct poldek_ts *ts);

/* Runs <hooks dir>/post-install.d if the pre-install hooks of ts ran. */
void poldek__ts_hooks_post(struct poldek_ts *ts);

#endif
