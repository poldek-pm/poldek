/* 
  Copyright (C) 2000 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <zlib.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmio.h>
#include <rpm/rpmurl.h>
#include <rpm/rpmmacro.h>
#include <trurl/nassert.h>
#include <trurl/nstr.h>

#include "log.h"
#include "vfile.h"

int vfile_verbose = 0;
int (*vfile_msg_fn)(const char *fmt, ...) = printf;
int (*vfile_err_fn)(const char *fmt, ...) = printf;

struct vfile_conf_s {
    char *cachedir;
    unsigned flags;
};

static struct vfile_conf_s vfile_conf = {
    "/tmp", VFILE_USEXT_FTP | VFILE_USEXT_HTTP
};

#ifdef HAVE_FOPENCOOKIE
cookie_io_functions_t gzio_cookie = {
    (cookie_read_function_t*)gzread,
    (cookie_write_function_t*)gzwrite,
    (cookie_seek_function_t*)gzseek,
    (cookie_close_function_t*)gzclose
};
#endif

void vfile_configure(const char *cachedir, int flags) 
{
    int len = 0;
    
    if (cachedir) {
        vfile_conf.cachedir = strdup(cachedir);
        len = strlen(vfile_conf.cachedir);
        if (vfile_conf.cachedir[len - 1] == '/')
            vfile_conf.cachedir[len - 1] = '\0';
    }
    
    if (flags >= 0)
        vfile_conf.flags = flags;
}

#ifdef ENABLE_VFILE_RPMIO
static void *fetch_cb(const unsigned long amount, const unsigned long total) 
{
    static unsigned long prev_v = 0, vv = 0, tvv = 0;
    
    
    if (amount && amount == total) /* last notification */
        vfile_msg_fn("_.done (%ld kB)\n", total/1024);

    else if (amount == 0) {
        vfile_msg_fn("_%6ld kB .", tvv*4032);
        vv = 0;
    }
    else if (total == 0) {
        vv++;
        if (++vv % 63 == 0) {
            tvv++;
            vfile_msg_fn("_\n%6ld kB .", tvv*4032);
        } else
            vfile_msg_fn("_.");
        
            
    }
    else {
        unsigned long i;
        unsigned long v = amount * 60 / total;
        for (i=prev_v; i<v; i++)
            vfile_msg_fn("_.");
        prev_v = v;
    }
    return NULL;
}


static void *fetch_cb_wrapper(const Header h __attribute__((unused)), 
                              const rpmCallbackType t __attribute__((unused)), 
                              const unsigned long amount, 
                              const unsigned long total,
                              const void *pkgKey __attribute__((unused)),
                              void * data __attribute__((unused)))
{
    return fetch_cb(amount, total);
}

static
int fetch_file_internal(const char *destdir, const char *path) 
{
    char tmpath[PATH_MAX];
    int rpmrc = -1;
    
    snprintf(tmpath, sizeof(tmpath), "%s/%s", destdir, n_basenam(path));
    vfile_msg_fn("Retrieving %s as %s\n", path, tmpath);
    
    urlSetCallback(fetch_cb_wrapper, NULL, 65536);
    if ((rpmrc = urlGetFile(path, tmpath)) < 0) {
        vfile_err_fn("vfile: %s transfer failed: %s\n", path,
            ftpStrerror(rpmrc));
    }

    return rpmrc == 0;
}


static
int fetch_file(const char *destdir, const char *path, int urltype) 
{
    int rc, internal = 1;
    
    
    mkdir(destdir, 0755);

    switch (urltype) {
        case VFURL_FTP:
            if (vfile_conf.flags & VFILE_USEXT_FTP)
                internal = 0;
            break;
            
        case VFURL_HTTP:
            if (vfile_conf.flags & VFILE_USEXT_HTTP)
                internal = 0;
            break;

        default:
            internal = 0;
    }

    if (vfile_configured_handlers() == 0)
        internal = 1;

    n_assert(destdir);
    if (internal) {
        rc = fetch_file_internal(destdir, path);
        
    } else if ((rc = vfile_fetch(destdir, path, urltype)) == 0 &&
               (urltype & (VFURL_HTTP | VFURL_FTP))) {
        vfile_err_fn("Transfer failed, trying internal client...\n");
        rc = fetch_file_internal(destdir, path);
    }
    
    return rc;
}

#else /* ENABLE_VFILE_RPMIO */

static
int fetch_file(const char *destdir, const char *path, int urltype) 
{
    int rc;
    
    mkdir(destdir, 0755);
    return vfile_fetch(destdir, path, urltype);
}

#endif /* ENABLE_VFILE_RPMIO */

static int isdir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 &&
        S_ISDIR(st.st_mode) && (st.st_mode & S_IRWXU);
}



/* RET: bool */
static int openvf(struct vfile *vf, const char *path, int vfmode) 
{ 
    int rc = 0;

    switch (vf->vf_type) {
        case VFT_IO: {
            int flags = 0;
            if (vfmode & VFM_RW)
                flags |= O_RDWR;
            else
                flags |= O_RDONLY;
            
            if ((vf->vf_fd = open(path, flags)) == -1) 
                vfile_err_fn("open %s: %m\n", path);
            else
                rc = 1;
        }
        break;
        
        case VFT_STDIO: {
            char *p, mode[2];
            gzFile gzstream;

            if (vfmode & VFM_RW)
                *mode = 'w';
            else
                *mode = 'r';
            *(mode+1) = '\0';
#ifdef HAVE_FOPENCOOKIE            
            if ((p = strrchr(path, '.')) && strcmp(p, ".gz") == 0) {
                if ((gzstream = gzopen(path, mode)) == NULL) {
                    rc = 0;
                    if (errno) 
                        vfile_err_fn("gzopen %s: %m\n", path);
                    else if (Z_MEM_ERROR) 
                        vfile_err_fn("gzopen %s: insufficient memory\n", path);
                    else 
                        vfile_err_fn("gzopen %s: unknown error\n", path);
                    break;
                }
                
                vf->vf_stream = fopencookie(gzstream, mode, gzio_cookie);
                if (vf->vf_stream != NULL)
                    rc = 1;
                else
                    vfile_err_fn("fopencookie %s: hgw error\n", path);

                
            } else {
#endif                
                if ((vf->vf_stream = fopen(path, mode)) != NULL) 
                    rc = 1;
                else 
                    vfile_err_fn("fopen %s: %m\n", path);
#ifdef HAVE_FOPENCOOKIE                
            }
#endif            
        }
        break;
        
        case VFT_GZIO: {
            char mode[2];
            
            if (vfmode & VFM_RW)
                *mode = 'w';
            else
                *mode = 'r';
            *(mode+1) = '\0';

            if ((vf->vf_gzstream = gzopen(path, mode)) != NULL) {
                rc = 1;

            } else {
                if (errno) 
                    vfile_err_fn("gzopen %s: %m\n", path);
                else if (Z_MEM_ERROR) 
                    vfile_err_fn("gzopen %s: insufficient memory\n", path);
                else 
                    vfile_err_fn("gzopen %s: unknown error\n", path);
            }
        }
        break;

#ifdef ENABLE_VFILE_RPMIO
        case VFT_RPMIO:
            if (vfmode & VFM_RW) {
                vfile_err_fn("%s: cannot open rw rpm\n", path);
                return 0;
            }

            vf->vf_fdt = Fopen(path, "r.fdio");
            if (vf->vf_fdt == NULL || Ferror(vf->vf_fdt)) {
                vfile_err_fn("open %s: %s\n", path, Fstrerror(vf->vf_fdt));
                if (vf->vf_fdt) {
                    Fclose(vf->vf_fdt);
                    vf->vf_fdt = NULL;
                }
                
            } else 
                rc = 1;
            
            break;
#endif            
            
        default:
            vfile_err_fn("vfile_open %s: type %d not supported\n",
                path, vf->vf_type);
            n_assert(0);
            rc = 0;
    }
    
    return rc;
}


struct vfile *vfile_open(const char *path, int vftype, int vfmode)
{
    struct vfile vf, *rvf = NULL;
    int opened, urltype;
    
    vf.vf_fdt = NULL;
    vf.vf_tmpath = NULL;
    vf.vf_type = vftype;
    vf.vf_mode = vfmode;

    urltype = vfile_url_type(path);
    opened = 0;

    if (urltype == VFURL_PATH) {
        if (openvf(&vf, path, vfmode))
            opened = 1;
        
    } else {
        char buf[PATH_MAX];
        char *tmpdir;
        int len;
            
        if (vfmode & VFM_RW) {
            vfile_err_fn("%s: cannot open remote file for writing\n", path);
            return 0;
        }

        len = snprintf(buf, sizeof(buf), "%s/", vfile_conf.cachedir);

        if (vfmode & VFM_CACHE) {
            vfile_url_as_path(&buf[len], sizeof(buf) - len, path);
            if (access(buf, R_OK) == 0 && openvf(&vf, buf, vfmode)) 
                opened = 1;
        }
        
        if (opened == 0) {
            char *p = NULL, *tmpath = NULL;

            if ((p = strrchr(path, '/')) == NULL) {
                tmpath = (char*)path;
                
            } else {
                int len = p - path;
                
                tmpath = alloca(len + 1);
                memcpy(tmpath, path, len);
                tmpath[len] = '\0';
            } 
            
            vfile_url_as_dirpath(&buf[len], sizeof(buf) - len, tmpath);
            tmpdir = buf;
            
            if (!isdir(tmpdir))
                mkdir(tmpdir, 0755);

            if (fetch_file(tmpdir, path, urltype)) {
                char tmpath[PATH_MAX];
                snprintf(tmpath, sizeof(tmpath), "%s/%s", tmpdir,
                         n_basenam(path));
                if (openvf(&vf, tmpath, VFM_RO)) {
                    vf.vf_tmpath = strdup(tmpath);
                    opened = 1;
                    
                } else {
                    //unlink(tmpath);
                }
            }
        }
    }

    if (opened) {
        rvf = malloc(sizeof(*rvf));
        memcpy(rvf, &vf, sizeof(*rvf));
    }
    
    return rvf;
}


void vfile_close(struct vfile *vf) 
{
    switch (vf->vf_type) {
        case VFT_IO:
            close(vf->vf_fd);
            vf->vf_fd = -1;
            break;
        
        case VFT_STDIO:
            fclose(vf->vf_stream);
            vf->vf_stream = NULL;
            break;
            
        case VFT_GZIO:
            gzclose(vf->vf_gzstream);
            vf->vf_gzstream = NULL;
            break;
            
#ifdef ENABLE_VFILE_RPMIO            
        case VFT_RPMIO:
            if (vf->vf_fdt) {
                Fclose(vf->vf_fdt);
                vf->vf_fdt = NULL;
            }
            break;
#endif
        default:
            vfile_err_fn("vfile_close: type %d not supported\n",
                vf->vf_type);
            n_assert(0);
    }
    
    if (vf->vf_tmpath) {        /* set for remote files only  */
        if ((vf->vf_mode & (VFM_NORM | VFM_CACHE)) == 0)
            unlink(vf->vf_tmpath);
        free(vf->vf_tmpath);
        vf->vf_tmpath = NULL;
    }
    
    free(vf);
}
