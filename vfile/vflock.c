/*
  Copyright (C) 2000 - 2004 Pawel A. Gajda <mis@k2.net.pl>

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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <trurl/nstr.h>

#include "i18n.h"
#include "sigint/sigint.h"

#define VFILE_INTERNAL
#include "vfile.h"


static
int vf_lockfile(const char *lockfile) 
{
    struct flock fl;
    int    fd;
    
    
    if ((fd = open(lockfile, O_RDWR | O_CREAT, 0644)) < 0) {
        vf_logerr("open %s: %m", lockfile);
        return 0;
    }

    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    
    if (fcntl(fd, F_SETLK, &fl) == -1) {
        if (errno != EAGAIN || errno != EACCES)
            vf_logerr("fcntl %s: %m", lockfile);

        close(fd);
        fd = 0;
        
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
        if (sigint_reached()) {
            fd = 0;
            break;
        }
        i++;
        if ((fd = vf_lockfile(path)) == 0) {
            vf_loginfo(_("Waiting for %s...\n"), path);
            sleep((int)((1 * i)/2));
        
        } else if (fd < 0) {
            fd = 0;
            break;
        }
    }
    //vf_loginfo("vf_lock_obtain %d (%s)\n", fd, path);
    return fd;
}

void vf_lock_release(int fd) 
{
    
    //vf_loginfo("vf_lock_release %d\n", fd);
    if (fd > 0)
        close(fd);
}

int vf_lockdir(const char *path) 
{
    char lockpath[PATH_MAX];
    int fd;
    
    n_snprintf(lockpath, sizeof(lockpath), "%s/.lock", path);
        
    if (!(fd = vf_lock_obtain(lockpath))) {
        vf_loginfo(_("Unable to obtain lock %s...\n"), lockpath);
        return 0;
    }
    //n_assert(fd < 10);
            
    return fd;
}


int vf_lock_mkdir(const char *path) 
{
    char tmp[PATH_MAX], *dn, *bn;
    int fd, lockfd = 0;
    struct stat st;

    if (!vf_valid_path(path))
        return 0;

    
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return vf_lockdir(path);

    printf("** vf_lock_mkdir %s\n", path);
    if (strcmp(path, "/home/mis/.poldek-cache") == 0)
        n_assert(0);
    
    n_snprintf(tmp, sizeof(tmp), "%s", path);
    n_basedirnam(tmp, &dn, &bn);

    if (!(fd = vf_lockdir(dn)))
        return 0;

    if (mkdir(path, 0750) != 0)
        vf_logerr("%s: mkdir: %m\n", path);
    else 
        lockfd = vf_lockdir(path);
        
    vf_lock_release(fd);
    return lockfd;
}

