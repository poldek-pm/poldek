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
int vfile_vhttp_fetch(const char *dest, const char *url, unsigned flags);

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


static int do_fetch(const char *dest, const char *url, unsigned flags)
{
    struct stat             st;
    FILE                    *stream;
    int                     rc = 0, vf_errno = 0;
    int                     end = 1, ntry = 0;
    
    
    
    if ((stream = fopen(dest, "a+")) == NULL) {
        vfile_err_fn("%s: fopen %s: %m\n", vf_mod_vhttp.vfmod_name, dest);
        return 0;
    }
    
    if (fstat(fileno(stream), &st) != 0) {
        vfile_err_fn("%s: fstat %s: %m\n", vf_mod_vhttp.vfmod_name, dest);
        fclose(stream);
        return 0;
    }

    if (flags & VFMOD_INFINITE_RETR)
        end = 1000;

    while (end-- > 0) {
        struct vf_progress_bar  bar;

        if (ntry++ && (flags & VFMOD_INFINITE_RETR))
            vfile_msg_fn(_("Retrying...(#%d)\n"), ntry);
        
        vfile_progress_init(&bar);
        
        if ((rc = vhttp_retr(stream, st.st_size, url, &bar)))
            break;
        
        vf_errno = errno;
        vfile_err_fn("%s: %s\n", vf_mod_vhttp.vfmod_name, vhttp_errmsg());
        
        switch (vhttp_errno) {
            case ENOENT:
            case EINTR:
            case ENOSPC:
                goto l_endloop;
                break;

            default:
                errno = vhttp_errno;
                //printf("errno(%d) %m\n", end);
        }

        fflush(stream);
        
        if (fstat(fileno(stream), &st) != 0) {
            vfile_err_fn("%s: fstat %s: %m\n", vf_mod_vhttp.vfmod_name, dest);
            break;
        }
        
        vf_cssleep(90);
    }

 l_endloop:
    
    fclose(stream);
    
    errno = vf_errno;
    return rc;
}


int vfile_vhttp_fetch(const char *dest, const char *url, unsigned flags)
{
    if (!do_fetch(dest, url, flags)) {
        vfile_set_errno(vf_mod_vhttp.vfmod_name, errno);
        if (vf_valid_path(dest))
            unlink(dest);
        return 0;
    }

    return 1;
}
