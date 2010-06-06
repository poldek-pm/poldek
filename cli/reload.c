/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
  $Id$
*/

#include <string.h>
#include <sys/param.h>
#include <time.h>

#include "compiler.h"
#include "sigint/sigint.h"
#include "poldek_util.h"
#include "i18n.h"
#include "pkgu.h"
#include "cli.h"
#include "log.h"


static int reload(struct cmdctx *cmdctx);

struct poclidek_cmd command_reload = {
    COMMAND_NOARGS | COMMAND_NOOPTS, 
    "reload", NULL, N_("Reload installed packages"), 
    NULL, NULL, NULL, reload,
    NULL, NULL, NULL, NULL, NULL, 0, 0
};

static int reload(struct cmdctx *cmdctx) 
{
    unsigned ldflags = POCLIDEK_LOAD_INSTALLED|POCLIDEK_LOAD_RELOAD;
    int rc;
    
    rc = poclidek_load_packages(cmdctx->cctx, ldflags);
    cmdctx->cctx->ts_dbpkgdir = time(0); /* touch */
    return rc;
}

