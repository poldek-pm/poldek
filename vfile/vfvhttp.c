/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@k2.net.pl>

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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include <trurl/nassert.h>


#define VFILE_INTERNAL
#include "i18n.h"
#include "vfile.h"

#include "vhttp/vhttp.h"

static int vfile_vhttp_init(void);
static void vfile_vhttp_destroy(void);
static int vfile_vhttp_fetch(struct vf_request *req);

struct vf_module vf_mod_vhttp = {
    "vhttp",
    VFURL_HTTP,
    vfile_vhttp_init,
    vfile_vhttp_destroy, 
    vfile_vhttp_fetch,
    0
};


static int vfile_vhttp_init(void) 
{
    vhttp_msg_fn = vfile_msg_fn;
    return vhttp_init(vfile_verbose, vfile_progress);
}


static void vfile_vhttp_destroy(void)
{
    vhttp_destroy();
}


static int vfile_vhttp_fetch(struct vf_request *req)
{
    int rc = 1;
    
    if (!vhttp_retr(req)) {
        req->req_errno = vhttp_errno;
        if ((req->flags & VF_REQ_INT_REDIRECTED) == 0)
            vfile_err_fn("%s: %s\n", vf_mod_vhttp.vfmod_name, vhttp_errmsg());
        rc = 0;
    }

    return rc;
}
