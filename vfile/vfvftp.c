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

#define VFILE_INTERNAL
#include "i18n.h"
#include "vfile.h"

#include "vftp/vftp.h"

static int vfile_vftp_init(void);
static void vfile_vftp_destroy(void);
int vfile_vftp_fetch(const char *dest, const char *url, unsigned flags);

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

    
static int do_fetch(const char *dest, const char *url, unsigned flags)
{
    struct stat             st;
    FILE                    *stream;
    int                     rc = 0, vf_errno = 0;
    int                     end = 1, ntry = 0;
    void                    *sigint_fn = NULL;
    
    if ((stream = fopen(dest, "a+")) == NULL) {
        vfile_err_fn("%s: fopen %s: %m\n", vf_mod_vftp.vfmod_name, dest);
        return 0;
    }
    
    if (fstat(fileno(stream), &st) != 0) {
        vfile_err_fn("%s: fstat %s: %m\n", vf_mod_vftp.vfmod_name, dest);
        fclose(stream);
        return 0;
    }

    if (flags & VFMOD_USER_AS_ANONPASSWD)
        set_vftp_anonpasswd();

    if (flags & VFMOD_INFINITE_RETR)
        end = 1000;
    
    sigint_fn = sigint_establish();
    
    while (end-- > 0) {
        struct vf_progress_bar  bar;

        if (sigint_reached()) {
            vf_errno = EINTR;
            break;
        }
        
        if (ntry++ && (flags & VFMOD_INFINITE_RETR)) {
            vfile_msg_fn(_("Retrying...(#%d)\n"), ntry);
            sleep(1);
        }
        
        vfile_progress_init(&bar);
        
        if ((rc = vftp_retr(stream, st.st_size, url, &bar)))
            break;
        
        vf_errno = vftp_errno;
        vfile_err_fn("%s: %s\n", vf_mod_vftp.vfmod_name, vftp_errmsg());
        
        switch (vftp_errno) {
            case ENOENT:
            case EINTR:
            case ENOSPC:
                goto l_endloop;
                break;

            default:
                errno = vftp_errno;
                //printf("errno(%d) %m\n", end);
        }
        
        fflush(stream);
        
        if (fstat(fileno(stream), &st) != 0) {
            vfile_err_fn("%s: fstat %s: %m\n", vf_mod_vftp.vfmod_name, dest);
            break;
        }
        
    }

 l_endloop:
    
    fclose(stream);
    sigint_restore(sigint_fn);
    errno = vf_errno;
    return rc;
}


int vfile_vftp_fetch(const char *dest, const char *url, unsigned flags)
{
    if (!do_fetch(dest, url, flags)) {
        vfile_set_errno(vf_mod_vftp.vfmod_name, errno);
        if (vf_valid_path(dest))
            unlink(dest);
        return 0;
    }

    return 1;
}
