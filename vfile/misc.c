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

#ifdef HAVE_FOPENCOOKIE
# define _GNU_SOURCE 1
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
#include <pwd.h>

#include <trurl/nstr.h>

#define VFILE_INTERNAL
#include "vfile.h"

#if 0                           /* not nessecary  */
void vf_cssleep(int cs) 
{
    struct timespec ts;
    
    ts.tv_sec = 0;
    ts.tv_nsec = cs * 10000000;
    nanosleep(&ts, NULL);
}
#endif

int vf_valid_path(const char *path) 
{
    const char *p;
    int  ndots;
    

    if (*path != '/') {
        vfile_err_fn("%s: path must must begin with a /\n", _purl(path));
        return 0;
    }
    
    p = path;
    p++;
    ndots = 0;
    
    while (*p) {
        switch (*p) {
            case '/':
                if (ndots == 2) {
                    vfile_err_fn("%s: relative paths not allowed\n", path);
                    return 0;
                }
                ndots = 0;
                break;

            case '.':
                ndots++;
                break;

            default:
                ndots = 0;
                
                if (!isalnum(*p) && strchr("-+/._@", *p) == NULL) {
                    vfile_err_fn("%s:%c non alphanumeric characters not allowed\n",
                                 path, *p);
                    return 0;
                }
                
                if (isspace(*p)) {
                    vfile_err_fn("%s: whitespaces not allowed\n", path);
                    return 0;
                }
        }
        p++;
    }
    
    return 1;
}


int vf_mkdir(const char *path) 
{
    struct stat st;
    
    if (!vf_valid_path(path))
        return 0;

    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & S_IRWXU))
        return 1;
    
    if (mkdir(path, 0750) != 0) {
        vfile_err_fn("%s: mkdir %m\n", path);
        return 0;
    }
    
    return 1;
}

int vf_unlink(const char *path) 
{
    if (vf_valid_path(path))
        return unlink(path) == 0;
    
    return 0;
}


int vf_userathost(char *buf, int size) 
{
    uid_t uid;
    int n;
    struct passwd *passwd;
    

    
    uid = getuid();
    if ((passwd = getpwuid(uid)) == NULL)
        return 0;

    n = 0;

    n += n_snprintf(buf, size, "%s@", passwd->pw_name);
    
    if (gethostname(&buf[n], size - n) != 0)
        return 0;
    
    buf[size - 1] = '\0';
    
    return strlen(buf);
}


int vf_cleanpath(char *buf, int size, const char *path) 
{
    const char **tl, **tl_save;
    const char *p; 
    int n = 0, startsl = 0, i;

    p = path;
    startsl = (*path == '/');
    
    if (vf_url_type(path) != VFURL_PATH) {
        if ((p = strstr(path, "://")) != NULL)
            p += 2;             /* not 3 -> '/%s' below */
        else 
            return 0;
        startsl = 1;            /* add second '/' to PROTO:/ */
    }
    
    if (p != path) {
        int len = p - path + 1;
        
        if (len > size)
            return 0;
        
        n = n_snprintf(buf, len, "%s", path);
    }

    
    tl = tl_save = n_str_tokl(p, "/");

    i = 0;
    while (*tl) {
        //printf("t = (%s), %s[%d]\n", *tl, buf, n);
        if (**tl) {
            if (i == 0 && startsl == 0)
                n += n_snprintf(&buf[n], size - n, "%s", *tl);
            else 
                n += n_snprintf(&buf[n], size - n, "/%s", *tl);
        }
        
        tl++;
        i++;
    }
    //printf("%s ==> %s\n", path, buf);
    n_str_tokl_free(tl_save);
    
    return n;
}
