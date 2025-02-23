/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#include <sys/param.h>
#include <time.h>

#include <vfile/vfile.h>

#include "compiler.h"
#include "sigint/sigint.h"
#include "poldek_util.h"
#include "i18n.h"
#include "pkgu.h"
#include "cli.h"
#include "log.h"

static int clean(struct cmdctx *cmdctx);

struct poclidek_cmd command_clean = {
    COMMAND_NOARGS | COMMAND_NOOPTS | COMMAND_BATCH,
    "clean", NULL, N_("Delete all files from local cache"),
    NULL, NULL, NULL, clean,
    NULL, NULL, NULL, NULL, NULL, 0, 0,
    NULL
};

/* TODO: clean summary with -v */
static int clean(struct cmdctx *cmdctx)
{
    (void)cmdctx;
    return vfile_cachedir_clean() == 0;
}
