/*
  Copyright (C) 2002 Pawel A. Gajda <mis@k2.net.pl>

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
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>

#include "i18n.h"

#define VFILE_INTERNAL
#include "vfile.h"
#include "p_open.h"


#define COMPRESST_GZIP   (1 << 0)
#define COMPRESST_BZIP2  (1 << 1)

struct uncompr {
    uint16_t  type;
    char      *cmd;
    char      *ext;
};

struct uncompr uncompr_tab[] = {
    {  COMPRESST_BZIP2, "/usr/bin/vfuncompr", "bz2" },
    {  COMPRESST_GZIP, "/usr/bin/vfuncompr", "gz"   },
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
                    vfile_msg_fn("%s: ", prefix);
                    endl = 0;
                }

                vfile_msg_fn("_%c", c);
                if (c == '\n' && cnt > 0)
                    endl = 1;
                
                cnt++;
            }
        }
    }
}


static
int vf_do_uncompr(struct uncompr *uncompr, const char *src, const char *dst)
{
    char              **argv;
    struct p_open_st  pst;
    int               n, ec, verbose;
    unsigned          p_open_flags = 0;

    
    argv = alloca(sizeof(*argv) * 10);
    n = 0;
    argv[n++] = uncompr->cmd;
    argv[n++] = (char*)src;
    argv[n++] = (char*)dst;
    argv[n++] = NULL;
        
    if (*vfile_verbose) 
        vfile_msg_fn(_("Uncompressing %s...\n"), n_basenam(src));
    
    p_st_init(&pst);


    verbose = *vfile_verbose;
    if (p_open(&pst, p_open_flags, uncompr->cmd, argv) == NULL) {
        vfile_err_fn("p_open: %s\n", pst.errmsg);
        return 0;
    }
    
    process_output(&pst, n_basenam(uncompr->cmd));
    
    if ((ec = p_close(&pst)) != 0)
        vfile_err_fn("%s\n", pst.errmsg ? pst.errmsg :
                     _("program exited with non-zero value"));
    
    p_st_destroy(&pst);
    *vfile_verbose = verbose;
    
    return ec == 0;
}

int vf_uncompr_able(const char *path) 
{
    char *p;
    int i;
    
    if ((p = strrchr(path, '.')) == NULL)
        return 0;
    
    p++;
    i = 0; 
    while (uncompr_tab[i].type > 0) {
        if (strcmp(p, uncompr_tab[i].ext) == 0) 
            return uncompr_tab[i].type;
        i++;
    }
    

    return 0;
}


int vf_uncompr_do(const char *path, const char *destpath) 
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

    vf_localunlink(destpath);
    
    return vf_do_uncompr(uncompr, path, destpath);
}

    


