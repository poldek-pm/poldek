/*
  Copyright (C) 2026 PLD Linux Team <feedback@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef POLDEK_REPO_H
#define POLDEK_REPO_H

#include "compiler.h"

struct poldek_ctx;

#define POLDEK_REPO_FORCE           (1 << 0)

/* the built-in repos.d: `repo add URL` downloads a repository's
   configuration and stores it there as a managed .conf */
EXPORT int poldek_repo_add(struct poldek_ctx *ctx, const char *url, unsigned flags);
EXPORT int poldek_repo_del(struct poldek_ctx *ctx, const char *url);

#endif
