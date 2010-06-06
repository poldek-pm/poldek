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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <trurl/nstr.h>

#include "compiler.h"
#include "i18n.h"
#include "log.h"
#include "vfile.h"
#include "vfile_intern.h"

static
int vf_lockfile(const char *lockfile) 
{
    struct flock fl;
    int    fd;
    
    DBGF("%s\n", lockfile);
    if ((fd = open(lockfile, O_RDWR | O_CREAT, 0644)) < 0) {
        vf_logerr("open %s: %m\n", lockfile);
        return -1;
    }

    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    
    if (fcntl(fd, F_SETLK, &fl) == -1) {
        int an_errno = errno;
        if (errno != EAGAIN || errno != EACCES)
            if (*vfile_verbose > 1)
                vf_logerr("fcntl %s: %m\n", lockfile);
	
        close(fd);
        fd = 0;
        if (an_errno == ENOLCK)
            fd = -1;
        
    } else {
        char buf[64];
        
        ftruncate(fd, 0);
        snprintf(buf, sizeof(buf), "%d", getpid());
        write(fd, buf, strlen(buf));
    }
    
    return fd;
}

#if 0
static
pid_t vf_readlockfile(const char *lockfile) 
{
    char buf[256];
    int fd, nread;
    pid_t pid;
    
    fd = open(lockfile, O_RDONLY, 0444);
    if(fd < 0) 
        return -1;
    
    nread = read(fd, buf, sizeof(buf));
    close(fd);

    if (sscanf(buf, "%d", &pid) == 1)
        return pid;
    
    return -1;
}
#endif

static
int vf_lock_obtain(const char *path) 
{
    int fd = 0, i;

    i = 1;
    while (fd == 0) {
        if (vfile_sigint_reached(0)) {
            fd = 0;
            break;
        }
        i++;
        if ((fd = vf_lockfile(path)) == 0) {
            char *buf;
            n_strdupap(path, &buf);
            buf = n_dirname(buf);
            vf_loginfo(_("Waiting for lock %s...\n"), buf);
            sleep((int)((1 * i)/2));
        
        } else if (fd < 0) {
            fd = 0;
            break;
        }
    }
    //vf_loginfo("vf_lock_obtain %d (%s)\n", fd, path);
    return fd;
}

void vf_lock_release(struct vflock *vflock) 
{
    DBGF("%d %s\n", vflock->fd, vflock->path);
    if (vflock->fd > 0)
        close(vflock->fd);
    vf_unlink(vflock->path);
    free(vflock);
}

struct vflock *vf_lockdir(const char *path) 
{
    char lockpath[PATH_MAX], lockfile[PATH_MAX];
    struct vflock *vflock = NULL;
    int fd, n;

    vf_url_as_dirpath(lockfile, sizeof(lockfile), path);
    n = n_snprintf(lockpath, sizeof(lockpath), "%s/.vflock_%s", path, lockfile);
    n_assert(n > 10 && n < (int)sizeof(lockpath) - 5);
    if (!(fd = vf_lock_obtain(lockpath))) {
        vf_logerr(_("%s: unable to obtain lock\n"), path);
        return NULL;
    }
    vflock = n_malloc(sizeof(*vflock) + n + 1);
    vflock->fd = fd;
    memcpy(vflock->path, lockpath, n + 1);
    return vflock;
}


struct vflock *vf_lock_mkdir(const char *path) 
{
    char tmp[PATH_MAX], *dn, *bn;
    struct vflock *vflock = NULL, *subdir_vflock = NULL;
    struct stat st;
    

    if (!vf_valid_path(path))
        return NULL;
    
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return vf_lockdir(path);

    DBGF("** vf_lock_mkdir %s\n", path);
    
    n_snprintf(tmp, sizeof(tmp), "%s", path);
    n_basedirnam(tmp, &dn, &bn);

    if ((subdir_vflock = vf_lockdir(dn)) == NULL)
        return NULL;

    if (mkdir(path, 0750) != 0)
        vf_logerr("%s: mkdir: %m\n", path);
    else 
        vflock = vf_lockdir(path);
    
    vf_lock_release(subdir_vflock);
    return vflock;
}

