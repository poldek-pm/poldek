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

#include "vftp/vftp.h"

static int vfile_vftp_init(void);
static int vfile_vftp_fetch(const char *dest, const char *url);

struct vf_module vf_mod_vftp = {
    "vftp",
    VFURL_FTP,
    vfile_vftp_init,
    vfile_vftp_fetch,
    0
};



static int vfile_vftp_init(void) 
{
    vftp_msg_fn = vfile_msg_fn;
    return vftp_init(*vfile_verbose, vfile_progress);
}

static
int do_fetch(const char *dest, const char *url)
{
    struct vf_progress_bar  bar;
    struct stat             st;
    FILE                    *stream;
    int                     rc, vf_errno = 0;

    vfile_progress_init(&bar);
    
    if ((stream = fopen(dest, "a+")) == NULL) {
        vfile_err_fn("%s: fopen %s: %m\n", vf_mod_vftp.vfmod_name, dest);
        return -1;
    }
    
    if (fstat(fileno(stream), &st) != 0) {
        vfile_err_fn("%s: fstat %s: %m\n", vf_mod_vftp.vfmod_name, dest);
        fclose(stream);
        return -1;
    }

    if (!(rc = vftp_retr(stream, st.st_size, url, &bar))) {
        vf_errno = errno;
        vfile_err_fn("%s: %s\n", vf_mod_vftp.vfmod_name, vftp_errmsg());
        unlink(dest);
    }
    
    fclose(stream);
    
    errno = vf_errno;
    return rc;
}


static int vfile_vftp_fetch(const char *dest, const char *url)
{
    if (!do_fetch(dest, url)) {
        vfile_set_errno(vf_mod_vftp.vfmod_name, errno);
        return 0;
    }

    return 1;
}

