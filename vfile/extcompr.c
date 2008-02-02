/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* $Id$ */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>

#include "i18n.h"

#include "vfile.h"
#include "vfile_intern.h"
#include "p_open.h"


#define COMPRESST_GZIP   (1 << 0)
#define COMPRESST_BZIP2  (1 << 1)

struct uncompr {
    uint16_t  type;
    char      *cmd;
    char      *ext;
};

#ifndef PKGLIBDIR
# error "PKGLIBDIR must be defined"
#endif

struct uncompr uncompr_tab[] = {
    {  COMPRESST_BZIP2, PKGLIBDIR "/vfcompr", "bz2" },
    {  COMPRESST_GZIP,  PKGLIBDIR "/vfcompr", "gz"   },
    {  0, NULL, NULL }
};

static void process_output(struct p_open_st *st, const char *prefix) 
{
    int endl = 1, cnt = 0;

    
    if (prefix == NULL)
        prefix = st->cmd;
    
    while (1) {
        struct timeval to = { 1, 0 };
        fd_set fdset;
        int rc;
        
        FD_ZERO(&fdset);
        FD_SET(st->fd, &fdset);
        if ((rc = select(st->fd + 1, &fdset, NULL, NULL, &to)) < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            
            break;
            
        } else if (rc > 0) {
            char  buf[2048];
            int   n, i;
            
            if ((n = read(st->fd, buf, sizeof(buf) - 1)) <= 0)
                break;

            if (*vfile_verbose < 2)
                continue;
            
            buf[n] = '\0';
            for (i=0; i < n; i++) {
                int c = buf[i];
                
                if (endl) {
                    vf_loginfo("%s: ", prefix);
                    endl = 0;
                }

                if (c == '\n')
                    vf_loginfo("_\n");
                else
                    vf_loginfo("_%c", c);
                
                if (c == '\n' && cnt > 0)
                    endl = 1;
                
                cnt++;
            }
        }
    }
}

static
int vf_do_compr(struct uncompr *uncompr, const char *param,
                const char *src, const char *dst)
{
    char              **argv;
    struct p_open_st  pst;
    int               n, ec, verbose;
    unsigned          p_open_flags = 0;

    
    argv = alloca(sizeof(*argv) * 10);
    n = 0;
    argv[n++] = uncompr->cmd;
    if (param)
        argv[n++] = (char*)param;
    argv[n++] = (char*)src;
    argv[n++] = (char*)dst;
    argv[n++] = NULL;
    

    p_st_init(&pst);

    verbose = *vfile_verbose;
    if (p_open(&pst, p_open_flags, uncompr->cmd, argv) == NULL) {
        vf_logerr("p_open: %s\n", pst.errmsg);
        return 0;
    }
    
    process_output(&pst, n_basenam(uncompr->cmd));
    
    if ((ec = p_close(&pst)) != 0)
        vf_logerr("%s\n", pst.errmsg ? pst.errmsg :
                  _("program exited with non-zero value"));
    
    p_st_destroy(&pst);
    *vfile_verbose = verbose;
    
    return ec == 0;
}

int vf_decompressable(const char *path, char *dest, int size) 
{
    char *p;
    int i;

    if ((p = strrchr(path, '.')) == NULL)
        return 0;
    
    p++;
    i = 0; 
    while (uncompr_tab[i].type > 0) {
        if (strcmp(p, uncompr_tab[i].ext) == 0) {
            if (dest) {
                snprintf(dest, size, "%s", path);
                p = strrchr(dest, '.');
                n_assert(p);
                *p = '\0';
            }
            return uncompr_tab[i].type;
        }
        i++;
    }
    

    return 0;
}


int vf_extdecompress(const char *path, const char *destpath) 
{
    struct uncompr *uncompr;
    char *p;
    int i;
    
    
    if ((p = strrchr(path, '.')) == NULL)
        return 0;
    
    p++;
    uncompr = NULL;
    i = 0;
    while (uncompr_tab[i].type > 0) {
        if (strcmp(p, uncompr_tab[i].ext) == 0) 
            uncompr = &uncompr_tab[i];
        i++;
    }
    	
    
    if (uncompr == NULL)
        return -1;

    if (*vfile_verbose > 0) 
        vf_loginfo(_("Decompressing %s...\n"), n_basenam(path));
    return vf_do_compr(uncompr, "-d", path, destpath);
}

int vf_extcompress(const char *path, const char *ext) 
{
    struct uncompr *uncompr;
    char destpath[PATH_MAX];
    int i;
    
    
    uncompr = NULL;
    i = 0;
    while (uncompr_tab[i].type > 0) {
        if (strcmp(ext, uncompr_tab[i].ext) == 0) 
            uncompr = &uncompr_tab[i];
        i++;
    }
    
    if (uncompr == NULL)
        return -1;

    snprintf(destpath, sizeof(destpath), "%s.%s", path, ext);
    unlink(destpath);
    if (*vfile_verbose) 
        vf_loginfo(_("Compressing %s...\n"), n_basenam(path));
    return vf_do_compr(uncompr, NULL, path, destpath);
}
    


