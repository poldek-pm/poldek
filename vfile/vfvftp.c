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
#include <trurl/nmalloc.h>
#include <sigint/sigint.h>

#define VFILE_INTERNAL
#include "i18n.h"
#include "vfile.h"

#include "vftp/vftp.h"

static int vfile_vftp_init(void);
static void vfile_vftp_destroy(void);
static int vfile_vftp_fetch(struct vf_request *req);

struct vf_module vf_mod_vftp = {
    "vftp",
    VFURL_FTP,
    vfile_vftp_init,
    vfile_vftp_destroy, 
    vfile_vftp_fetch,
    0
};


static int vfile_vftp_init(void) 
{
    vftp_msg_fn = vfile_msg_fn;
    //printf("VERBOSE = %d\n", *vfile_verbose);
    vftp_anonpasswd = vfile_anonftp_passwd;
    return vftp_init(vfile_verbose, vfile_progress);
}


static void vfile_vftp_destroy(void)
{
    vftp_destroy();
}


static void set_vftp_anonpasswd(void)
{
    static int isset = 0;
    
    if (isset == 0) {
        char buf[256];

        if (vf_userathost(buf, sizeof(buf)) > 0) 
            vftp_anonpasswd = n_strdup(buf);
        isset = 1;
    }
}

static int vfile_vftp_fetch(struct vf_request *req)
{
    int rc = 1;
    
    if (req->flags & VF_REQ_SYSUSER_AS_ANONPASSWD)
        set_vftp_anonpasswd();

    req->req_errno = 0;

    if (!vftp_retr(req)) {
        vfile_err_fn("%s: %s\n", vf_mod_vftp.vfmod_name, vftp_errmsg());
        req->req_errno = vftp_errno;
        rc = 0;
    }

    return rc;
}
