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
#include <trurl/nmalloc.h>

#include "i18n.h"

#include "sigint/sigint.h"

#define VFILE_INTERNAL
#include "vfile.h"


static int          vfile_err_no = 0;
static const char   *vfile_err_ctx = NULL;

static int          verbose = 0; 
int                 *vfile_verbose = &verbose;

static const char   default_anon_passwd[] = "poldek@znienacka.net";

struct vfile_configuration vfile_conf = {
    "/tmp", VFILE_CONF_STUBBORN_RETR, NULL, NULL, &verbose, 
    (char*)default_anon_passwd,
    NULL
};

static void set_anonpasswd(void)
{
    if (vfile_conf.anon_passwd != default_anon_passwd) {
        free(vfile_conf.anon_passwd);
        vfile_conf.anon_passwd = (char*)default_anon_passwd;
    }
    
    if (vfile_conf.flags & VFILE_CONF_SYSUSER_AS_ANONPASSWD) {
        char buf[256];

        if (vf_userathost(buf, sizeof(buf)) > 0) 
            vfile_conf.anon_passwd = n_strdup(buf);
    }
}

int vfile_configure(int param, ...) 
{
    va_list  ap;
    int      v, *vp, rc;
    char     *vs;
    void     *vv;

    if (vfile_conf.default_clients_ht == NULL) {
        vfile_conf.default_clients_ht = n_hash_new(7, free);
        vfile_conf.proxies_ht = n_hash_new(7, free);
    }
    
        
    rc = 1;
    va_start(ap, param);

    switch (param) {
        case VFILE_CONF_LOGCB:
            vp = va_arg(ap, void*);
            if (vp)
                vfile_conf.log = vv;
            break;
            
        case VFILE_CONF_VERBOSE:
            vp = va_arg(ap, int*);
            if (vp)
                vfile_verbose = vp;
            else
                vfile_verbose = &verbose;
            break;
            
                
        case VFILE_CONF_CACHEDIR:
            vs = va_arg(ap, char*);
            if (vs) {
                vfile_conf.cachedir = n_strdup(vs);
                v = strlen(vfile_conf.cachedir);
                if (vfile_conf.cachedir[v - 1] == '/')
                    vfile_conf.cachedir[v - 1] = '\0';
            }
            break;
            
        case VFILE_CONF_SYSUSER_AS_ANONPASSWD:
            v = va_arg(ap, int);
            if (v) 
                vfile_conf.flags |= VFILE_CONF_SYSUSER_AS_ANONPASSWD;
            else
                vfile_conf.flags &= ~VFILE_CONF_SYSUSER_AS_ANONPASSWD;
            set_anonpasswd();
            break;
            
        case VFILE_CONF_DEFAULT_CLIENT: {
            char *proto, *client;

            proto = va_arg(ap, char *);
            if (proto)
                client = va_arg(ap, char *);
            
            if (proto && client) {
                
                if (strcmp(client, "internal") == 0) {
                    if (n_hash_exists(vfile_conf.default_clients_ht, proto))
                        n_hash_remove(vfile_conf.default_clients_ht, proto);
                } else 
                    n_hash_replace(vfile_conf.default_clients_ht, proto,
                                   n_strdup(client));
            }
            break;    
        };

        case VFILE_CONF_PROXY: {
            char *proto, *url;
            
            proto = va_arg(ap, char *);
            if (proto)
                url = va_arg(ap, char *);
            
            if (proto && *proto && url && *url) {
                if (!n_hash_exists(vfile_conf.proxies_ht, proto))
                    n_hash_insert(vfile_conf.proxies_ht, proto, n_strdup(url));
                else 
                    n_hash_replace(vfile_conf.proxies_ht, proto, n_strdup(url));
            }
            
            break;    
        }
    }

    va_end(ap);
    return rc;
}



#define ZLIB_TRACE 0

#ifdef HAVE_FOPENCOOKIE

#ifndef __GLIBC_MINOR__
# error "glibc2 or later is required"
#endif

#ifndef __GLIBC_PREREQ
# if defined __GLIBC__ && defined __GLIBC_MINOR__
#  define __GLIBC_PREREQ(maj, min) \
       ((__GLIBC__ << 16) + __GLIBC_MINOR__ >= ((maj) << 16) + (min))
# else
#  define __GLIBC_PREREQ(maj, min) 0
# endif
#endif /* __GLIBC_PREREQ */

#if __GLIBC_PREREQ(2,2)
static int gzfseek(void *stream, _IO_off64_t *offset, int whence)
{
    z_off_t rc, off = *offset;
    
    rc = gzseek(stream, off, whence);
    if (rc >= 0)
        rc = 0;
#if ZLIB_TRACE
    printf("zfseek (%p, %ld, %lld, %d) = %d\n", stream, off, *offset, whence, rc);
#endif    
    return rc;
}

#else  /* glibc < 2.2 */

static int gzfseek(void *stream, _IO_off_t offset, int whence) 
{
    z_off_t rc;
    
    rc = gzseek(stream, offset, whence);
#if ! __GLIBC_PREREQ(2,1)       /* AFAIK glibc2.1.x cookie streams required
                                   offset to be returned */
    if (rc >= 0) 
        rc = 0;
#endif
    return rc;
}
#endif /* __GLIBC_PREREQ(2,2) */

#if ZLIB_TRACE
static int gzread_wrap(void *stream, char *buf, size_t size)
{
    int rc;
    rc = gzread(stream, buf, size);
    printf("zread (%p, %d) = %d (%m)\n", stream, size, rc);
    return rc;
}
#endif

static cookie_io_functions_t gzio_cookie = {
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
                vf_logerr("open %s: %m\n", CL_URL(path));
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
                        vf_logerr("%s: %m\n", CL_URL(path));
                    else if (Z_MEM_ERROR) 
                        vf_logerr("gzopen %s: insufficient memory\n",
                                   CL_URL(path));
                    else 
                        vf_logerr("gzopen %s: unknown error\n", CL_URL(path));
                    break;
                }

                vf->vf_stream = fopencookie(gzstream, mode, gzio_cookie);
                if (vf->vf_stream != NULL) {
                    rc = 1;
                    fseek(vf->vf_stream, 0, SEEK_SET); /* glibc BUG (?) */
                } else
                    vf_logerr("fopencookie %s: hgw error\n", CL_URL(path));

            } else {
#endif                
                if ((vf->vf_stream = fopen(path, mode)) != NULL) 
                    rc = 1;
                else 
                    vf_logerr("%s: %m\n", CL_URL(path));
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
                    vf_logerr("%s: %m\n", CL_URL(path));
                else if (Z_MEM_ERROR) 
                    vf_logerr("gzopen %s: insufficient memory\n",
                               CL_URL(path));
                else 
                    vf_logerr("gzopen %s: unknown error\n", CL_URL(path));
            }
        }
        break;
        
#ifdef ENABLE_VFILE_TRURLIO
        case VFT_TRURLIO: {
            char *mode = "";
            
            if (vfmode & VFM_RW)
                mode = "w";
            else if (vfmode & VFM_APPEND)
				mode = "a+";
			else 
                mode = "r";
			
				vf->vf_tnstream = n_stream_open(path, mode, TN_STREAM_UNKNOWN);
            if (vf->vf_tnstream != NULL)
                rc = 1;
            else 
                vf_logerr("%s: %m\n", CL_URL(path));
        }
        break;
#endif
        
#ifdef ENABLE_VFILE_RPMIO
        case VFT_RPMIO:
            if (vfmode & VFM_RW) {
                vf_logerr("%s: cannot open rw rpm\n", CL_URL(path));
                return 0;
            }

            vf->vf_fdt = Fopen(path, "r.fdio");
            if (vf->vf_fdt == NULL || Ferror(vf->vf_fdt)) {
                vf_logerr("open %s: %s\n", CL_URL(path),
                        Fstrerror(vf->vf_fdt));
                if (vf->vf_fdt) {
                    Fclose(vf->vf_fdt);
                    vf->vf_fdt = NULL;
                }
                
            } else 
                rc = 1;
            
            break;
#endif            
            
        default:
            vf_logerr("vfile_open %s: type %d not supported\n",
                    CL_URL(path), vf->vf_type);
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


static const char *vfuncompr(const char *path, char *dest, int size) 
{
    char *p, *tmpath;
    int  n;
    
    
    *dest = '\0';
    
    if (!vf_uncompr_able(path))
        return path;

    n = n_snprintf(dest, size, "%s/", vfile_conf.cachedir);

    if ((p = strrchr(path, '/')) == NULL) {
        tmpath = (char*)path;
                
    } else {
        int len = p - path;
        
        tmpath = alloca(len + 1);
        memcpy(tmpath, path, len);
        tmpath[len] = '\0';
    } 
            
    vf_url_as_dirpath(&dest[n], size - n, tmpath);
    
    //printf("DEST = %s\n", dest);
    if (!vf_mkdir(dest))
        return NULL;

    vf_url_as_path(&dest[n], size - n, path);

    n_assert((p = strrchr(dest, '.')) != NULL);

    *p = '\0';
    
    if (vf_uncompr_do(path, dest))
        return dest;
    
    return NULL;
}


struct vfile *do_vfile_open(const char *path, int vftype, int vfmode)
{
    struct vfile vf, *rvf = NULL;
    int opened, urltype;
    char buf[PATH_MAX];
    char *tmpdir;
    const char *rpath;
    int len;

    
    vf.vf_fdt = NULL;
    vf.vf_path = NULL;
    vf.vf_tmpath = NULL;
    vf.vf_type = vftype;
    vf.vf_mode = vfmode;
    vf.vf_flags = 0;
    
    urltype = vf_url_type(path);
    opened = 0;

    if (urltype == VFURL_PATH) {
        rpath = path;
        
        if (vfmode & VFM_UNCOMPR) {
            if ((rpath = vfuncompr(path, buf, sizeof(buf))) == NULL)
                return 0;
        }
        
        if (openvf(&vf, rpath, vfmode)) 
            opened = 1;
        goto l_end;
    }
    
    if (vfmode & VFM_RW) {
        vf_logerr("%s: cannot open remote file for writing\n", CL_URL(path));
        return 0;
    }
    
    len = n_snprintf(buf, sizeof(buf), "%s/", vfile_conf.cachedir);
    vf_url_as_path(&buf[len], sizeof(buf) - len, path);
    
    if ((vfmode & VFM_CACHE) && file_ok(buf, vfmode)) {
        char tmpath[PATH_MAX];
        
        rpath = buf;
        
        if ((vfmode & VFM_UNCOMPR)) {
            if ((rpath = vfuncompr(buf, tmpath, sizeof(tmpath))) == NULL)
                return 0;
        }
    
        if (openvf(&vf, rpath, vfmode)) {
            vf.vf_tmpath = n_strdup(rpath);
            opened = 1;
            vf.vf_flags |= VF_FRMCACHE;
        }
    }
    
    if (opened == 0) {      /* fetch */
        char *p = NULL, *tmpath = NULL;

        vf_localunlink(buf);
        
        if ((p = strrchr(path, '/')) == NULL) {
            tmpath = (char*)path;
                
        } else {
            int len = p - path;
            
            tmpath = alloca(len + 1);
            memcpy(tmpath, path, len);
            tmpath[len] = '\0';
        } 
            
        vf_url_as_dirpath(&buf[len], sizeof(buf) - len, tmpath);
        tmpdir = buf;

        if (!vf_mkdir(tmpdir))
            return 0;

        if (vfile_fetch(tmpdir, path)) {
            char tmpath[PATH_MAX], upath[PATH_MAX];;

            snprintf(tmpath, sizeof(tmpath), "%s/%s", tmpdir,
                     n_basenam(path));

            rpath = tmpath;

            if ((vfmode & VFM_UNCOMPR)) {
                if ((rpath = vfuncompr(tmpath, upath, sizeof(upath))) == NULL)
                    return 0;
            }
            
            
            if (openvf(&vf, rpath, VFM_RO)) {
                vf.vf_tmpath = n_strdup(rpath);
                opened = 1;
                vf.vf_flags |= VF_FETCHED;
                    
            } else {
                //vf_localunlink(tmpath); wget && co sometimes badly returns non zero 
            }
        }
    }
    


l_end:    
    if (opened) {
        rvf = n_malloc(sizeof(*rvf));
        memcpy(rvf, &vf, sizeof(*rvf));
        rvf->vf_urltype = urltype;
    }
    
    return rvf;
}


struct vfile *vfile_open(const char *path, int vftype, unsigned vfmode) 
{
    struct vfile *vf = NULL;

    vfile_err_no = 0;
    if ((vf = do_vfile_open(path, vftype, vfmode))) {
        vf->vf_path = n_strdup(path);
        vf->_refcnt = 0;
    }
    
    return vf;
}

struct vfile *vfile_incref(struct vfile *vf)
{
    vf->_refcnt++;
    //printf("vfile_incref %p %s [%d]\n", vf, vf->vf_path ? vf->vf_path : NULL, vf->_refcnt);
    return vf;
}

void vfile_close(struct vfile *vf) 
{
    //printf("vfile_close %p %s [%d]\n", vf, vf->vf_path ? vf->vf_path : NULL, vf->_refcnt);
    
    if (vf->_refcnt > 0) {
        vf->_refcnt--;
        return;
    }

    //printf("vfile_closeD %p %s [%d]\n", vf, vf->vf_path ? vf->vf_path : NULL, vf->_refcnt);

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
            
#ifdef ENABLE_VFILE_TRURLIO
        case VFT_TRURLIO:
            n_stream_close(vf->vf_tnstream);
            vf->vf_tnstream = NULL;
            break;
#endif            
            
#ifdef ENABLE_VFILE_RPMIO
        case VFT_RPMIO:
            if (vf->vf_fdt) {
                Fclose(vf->vf_fdt);
                vf->vf_fdt = NULL;
            }
            break;
#endif
        default:
            vf_logerr("vfile_close: type %d not supported\n", vf->vf_type);
            n_assert(0);
    }

    if (vf->vf_path) {
        free(vf->vf_path);
        vf->vf_path = NULL;
    }
    
    if (vf->vf_tmpath) {        /* set for remote files only  */
        if ((vf->vf_mode & (VFM_NORM | VFM_CACHE)) == 0)
            vf_localunlink(vf->vf_tmpath);
        free(vf->vf_tmpath);
        vf->vf_tmpath = NULL;
    }
    memset(vf, 0, sizeof(*vf));
    free(vf);
}


int vfile_unlink(struct vfile *vf) 
{
    int rc = 1;
    
    if (vf->vf_tmpath) { /* set for remote files only  */
        rc = vf_localunlink(vf->vf_tmpath);
        free(vf->vf_tmpath);
        vf->vf_tmpath = NULL;
    }

    return rc;
}


int vf_mksubdir(char *path, int size, const char *dirpath) 
{
    int n;

    n = n_snprintf(path, size, "%s/%s", vfile_conf.cachedir, dirpath);
    if (vf_mkdir(path))
        return n;
    return 0;
}

int vf_localpath(char *path, size_t size, const char *url) 
{
    int n;
    
    n = n_snprintf(path, size, "%s/", vfile_conf.cachedir);
    return vf_url_as_path(&path[n], size - n, url);
}

int vf_localdirpath(char *path, size_t size, const char *url) 
{
    int n;
    
    n = n_snprintf(path, size, "%s/", vfile_conf.cachedir);
    return vf_url_as_dirpath(&path[n], size - n, url);
}


int vf_localunlink(const char *path) 
{
    if (strncmp(path, vfile_conf.cachedir, strlen(vfile_conf.cachedir)) == 0 &&
        vf_valid_path(path))
        return unlink(path) == 0;
    
    return 0;
}


void vf_vlog(unsigned flags, const char *fmt, va_list ap)
{
    if (vfile_conf.log)
        vfile_conf.log(flags, fmt, ap);

    else {
        vfprintf(stdout, fmt, ap);
        fflush(stdout);
    }
}

void vf_log(unsigned flags, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vf_vlog(flags, fmt, args);
    va_end(args);
}

void vfile_set_errno(const char *ctxname, int vf_errno) 
{
    vfile_err_no = vf_errno;
    vfile_err_ctx = ctxname;
}
