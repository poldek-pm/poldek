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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include <zlib.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmio.h>
#include <rpm/rpmurl.h>
#include <rpm/rpmmacro.h>
#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nhash.h>
#include <trurl/narray.h>

#include "i18n.h"

#define VFILE_INTERNAL
#include "vfile.h"

#ifdef ENABLE_VFILE_CURL
extern struct vf_module vf_mod_curl;
#endif

extern struct vf_module vf_mod_vftp;

struct vf_module *vfmod_tab[] = {
    &vf_mod_vftp,
#ifdef ENABLE_VFILE_CURL    
    &vf_mod_curl,
#endif
    NULL
};

static int          vfile_err_no = 0;
static const char   *vfile_err_ctx = NULL;

static int          verbose = 0; 
int                 *vfile_verbose = &verbose;


static void vfmsg(const char *fmt, ...);

void (*vfile_msgtty_fn)(const char *fmt, ...) = vfmsg;
void (*vfile_msg_fn)(const char *fmt, ...) = vfmsg;
void (*vfile_err_fn)(const char *fmt, ...) = vfmsg;


struct vfile_conf_s {
    char *cachedir;
    unsigned flags;
};

static struct vfile_conf_s vfile_conf = { "/tmp", 0};



void vfile_configure(const char *cachedir, int flags) 
{
    int n, len = 0;
    
    if (cachedir) {
        vfile_conf.cachedir = strdup(cachedir);
        len = strlen(vfile_conf.cachedir);
        if (vfile_conf.cachedir[len - 1] == '/')
            vfile_conf.cachedir[len - 1] = '\0';
    }
    
    if (flags >= 0)
        vfile_conf.flags = flags;

    n = 0;
    while (vfmod_tab[n] != NULL)
        vfmod_tab[n++]->init();
}


#define ZLIB_TRACE 0

#ifdef HAVE_FOPENCOOKIE
#if __GNUC_PREREQ (2,2)
int gzfseek(void *stream, _IO_off64_t *offset, int whence)
{
    z_off_t off = *offset;
    int rc;
    
    rc = gzseek(stream, off, whence);
    if (rc >= 0)
        rc = 0;
#if ZLIB_TRACE
    printf("zfseek (%p, %ld, %lld, %d) = %d\n", stream, off, *offset, whence, rc);
#endif    
    return rc;
}
#else
int gzfseek(void *stream, _IO_off_t offset, int whence) 
{
    int rc;
    
    rc = gzseek(stream, offset, whence);
    if (rc >= 0)
        rc = 0;

    return rc;
}
#endif /* __GNUC_PREREQ */

#if ZLIB_TRACE
int gzread_wrap(void *stream, char *buf, size_t size)
{
    int rc;
    rc = gzread(stream, buf, size);
    printf("zread (%p, %d) = %d (%m)\n", stream, size, rc);
    return rc;
}
#endif

cookie_io_functions_t gzio_cookie = {
#if ZLIB_TRACE    
    (cookie_read_function_t*)gzread_wrap,
#else
    (cookie_read_function_t*)gzread,
#endif    
    (cookie_write_function_t*)gzwrite,
    gzfseek,
    (cookie_close_function_t*)gzclose
};

#endif /* HAVE_FOPENCOOKIE */

static
const struct vf_module *find_vf_module(int urltype) 
{
    int n = 0;
    
    n = 0;
    while (vfmod_tab[n] != NULL) {
        if (vfmod_tab[n]->vf_protocols & urltype)
            return vfmod_tab[n];
        n++;
    }

    return NULL;
}

static
const struct vf_module *select_vf_module(int urltype) 
{
    const struct vf_module *mod = NULL;

    
    switch (urltype) {
        case VFURL_FTP:
            if ((vfile_conf.flags & VFILE_USEXT_FTP) == 0)
                mod = find_vf_module(urltype);
            break;
            
        case VFURL_HTTP:
            if ((vfile_conf.flags & VFILE_USEXT_HTTP) == 0)
                mod = find_vf_module(urltype);
            break;

        case VFURL_HTTPS:
            mod = find_vf_module(urltype);
            break;

        default:
            mod = NULL;
    }

    if (mod == NULL && vfile_configured_handlers() == 0)
        mod = find_vf_module(urltype);
    
    return mod;
}

int vfile_fetcha(const char *destdir, tn_array *urls, int urltype) 
{
    const struct vf_module *mod = NULL;
    int rc = 1;
    
    
    n_assert(urltype > 0);
    if (urltype == VFURL_UNKNOWN) 
        urltype = vfile_url_type(n_array_nth(urls, 0));
    
    if ((mod = select_vf_module(urltype)) == NULL) {
        rc = vfile_fetcha_ext(destdir, urls, urltype);
        
    } else {
        int i;
        for (i=0; i<n_array_size(urls); i++) {
            char destpath[PATH_MAX], *url;

            url = n_array_nth(urls, i);
            snprintf(destpath, sizeof(destpath), "%s/%s", destdir,
                     n_basenam(url));
            vfile_msg_fn(_("Retrieving %s...\n"), url);
            if (!mod->fetch(destpath, url, VFMOD_INFINITE_RETR)) {
                rc = 0;
                break;
            }
        }
    }
    
    return rc;
}


int vfile_fetch(const char *destdir, const char *path, int urltype) 
{
    const struct vf_module *mod = NULL;
    int rc;

    if (!vfile_mkdir(destdir))
        return 0;

    n_assert(urltype > 0);
    if (urltype == VFURL_UNKNOWN) 
        urltype = vfile_url_type(path);

    if ((mod = select_vf_module(urltype)) == NULL)
        rc = vfile_fetch_ext(destdir, path, urltype);
    
    else {
        char destpath[PATH_MAX];

        snprintf(destpath, sizeof(destpath), "%s/%s", destdir,
                 n_basenam(path));
        vfile_msg_fn(_("Retrieving %s...\n"), path);
        rc = mod->fetch(destpath, path, VFMOD_INFINITE_RETR);
    }
    
    
    return rc;
}


/* RET: bool */
static int openvf(struct vfile *vf, const char *path, int vfmode) 
{ 
    int rc = 0;

    switch (vf->vf_type) {
        case VFT_IO: {
            int flags = 0;
            
            if (vfmode & VFM_RW) {
                flags |= O_RDWR;
                if (vfmode & VFM_APPEND)
                    flags |= O_APPEND;
                
            } else {
                flags |= O_RDONLY;
            }
            
            
            if ((vf->vf_fd = open(path, flags)) == -1) 
                vfile_err_fn("open %s: %m\n", path);
            else
                rc = 1;
        }
        break;
        
        case VFT_STDIO: {
            char *p, *mode;
            gzFile gzstream;

            if (vfmode & VFM_APPEND)
                mode = "a+";
            
            else if (vfmode & VFM_RW)
                mode = "w";
            
            else
                mode = "r";

#ifdef HAVE_FOPENCOOKIE            
            if ((p = strrchr(path, '.')) && strcmp(p, ".gz") == 0) {
                if ((gzstream = gzopen(path, mode)) == NULL) {
                    rc = 0;
                    if (errno) 
                        vfile_err_fn("%s: %m\n", path);
                    else if (Z_MEM_ERROR) 
                        vfile_err_fn("gzopen %s: insufficient memory\n", path);
                    else 
                        vfile_err_fn("gzopen %s: unknown error\n", path);
                    break;
                }

                vf->vf_stream = fopencookie(gzstream, mode, gzio_cookie);
                if (vf->vf_stream != NULL) {
                    rc = 1;
                    fseek(vf->vf_stream, 0, SEEK_SET); /* glibc BUG (?) */
                } else
                    vfile_err_fn("fopencookie %s: hgw error\n", path);

            } else {
#endif                
                if ((vf->vf_stream = fopen(path, mode)) != NULL) 
                    rc = 1;
                else 
                    vfile_err_fn("%s: %m\n", path);
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
                    vfile_err_fn("%s: %m\n", path);
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

static int file_ok(const char *path, int vfmode) 
{
    struct stat st;
    
    return access(path, R_OK) == 0 && stat(path, &st) == 0 &&
        ((vfmode & VFM_NOEMPTY)? st.st_size > 0 : 1);
}


struct vfile *do_vfile_open(const char *path, int vftype, int vfmode)
{
    struct vfile vf, *rvf = NULL;
    int opened, urltype;
    char buf[PATH_MAX];
    char *tmpdir;
    int len;

    
    vf.vf_fdt = NULL;
    vf.vf_path = NULL;
    vf.vf_tmpath = NULL;
    vf.vf_type = vftype;
    vf.vf_mode = vfmode;
    vf.vf_flags = 0;
    
    urltype = vfile_url_type(path);
    opened = 0;

    if (urltype == VFURL_PATH) {
        if (openvf(&vf, path, vfmode)) 
            opened = 1;
        goto l_end;
    }
    
    if (vfmode & VFM_RW) {
        vfile_err_fn("%s: cannot open remote file for writing\n", path);
        return 0;
    }

    len = snprintf(buf, sizeof(buf), "%s/", vfile_conf.cachedir);
    
    
    vfile_url_as_path(&buf[len], sizeof(buf) - len, path);
    if ((vfmode & VFM_CACHE) && file_ok(buf, vfmode) &&
        openvf(&vf, buf, vfmode)) {
        
        vf.vf_tmpath = strdup(buf);
        opened = 1;
        vf.vf_flags |= VF_FRMCACHE;
        
    } else {
        unlink(buf);
    }
    
    if (opened == 0) {      /* fetch */
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

        if (!vfile_mkdir(tmpdir))
            return 0;

        if (vfile_fetch(tmpdir, path, urltype)) {
            char tmpath[PATH_MAX];

            snprintf(tmpath, sizeof(tmpath), "%s/%s", tmpdir,
                     n_basenam(path));
                
            if (openvf(&vf, tmpath, VFM_RO)) {
                vf.vf_tmpath = strdup(tmpath);
                opened = 1;
                vf.vf_flags |= VF_FETCHED;
                    
            } else {
                //unlink(tmpath); wget && co sometimes badly returns non zero 
            }
        }
    }
    


l_end:    
    if (opened) {
        rvf = malloc(sizeof(*rvf));
        memcpy(rvf, &vf, sizeof(*rvf));
        rvf->vf_urltype = urltype;
    }
    
    return rvf;
}


struct vfile *vfile_open(const char *path, int vftype, int vfmode) 
{
    struct vfile *vf = NULL;
    int n = 0;

    
    while (1) {
        vfile_err_no = 0;
        
        if ((vf = do_vfile_open(path, vftype, vfmode))) {
            vf->vf_path = strdup(path);
            break;
        }

        if ((vfmode & VFM_STBRN) == 0)
            break;

        if (vfile_err_no == ENOENT)
            break;

        if (n > 100) {
            vfile_msg_fn(_("Give up (#%d)...\n"), ++n);
            break;
        }
        
        vfile_msg_fn(_("Retrying %s (#%d)...\n"), path, ++n);
        sleep(1);
    }
    
    return vf;
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

    if (vf->vf_path) {
        free(vf->vf_path);
        vf->vf_path = NULL;
    }
    
    if (vf->vf_tmpath) {        /* set for remote files only  */
        if ((vf->vf_mode & (VFM_NORM | VFM_CACHE)) == 0 &&
            vfile_valid_path(vf->vf_tmpath))
            unlink(vf->vf_tmpath);
        free(vf->vf_tmpath);
        vf->vf_tmpath = NULL;
    }

    free(vf);
}


int vfile_unlink(struct vfile *vf) 
{
    int rc = 1;
    
    if (vf->vf_tmpath)  /* set for remote files only  */
        if (vfile_valid_path(vf->vf_tmpath)) {
            rc = (unlink(vf->vf_tmpath) == 0);
            free(vf->vf_tmpath);
            vf->vf_tmpath = NULL;
        }

    return rc;
}


static void vfmsg(const char *fmt, ...)
{
    va_list args;
    
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    fflush(stdout);
    va_end(args);
}


void vfile_set_errno(const char *ctxname, int vf_errno) 
{
    vfile_err_no = vf_errno;
    vfile_err_ctx = ctxname;
}


int vfile_mksubdir(char *path, int size, const char *dirpath) 
{
    int n;

    n = snprintf(path, size, "%s/%s", vfile_conf.cachedir, dirpath);
    if (vfile_mkdir(path))
        return n;
    return 0;
}


int vfile_localpath(char *path, size_t size, const char *url) 
{
    int n;
    
    n = snprintf(path, size, "%s/", vfile_conf.cachedir);
    return vfile_url_as_path(&path[n], size - n, url);
}


void vfile_cssleep(int cs) 
{
    struct timespec ts;
    
    ts.tv_sec = 0;
    ts.tv_nsec = cs * 10000000;
    nanosleep(&ts, NULL);
}
