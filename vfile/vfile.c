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

#define VFILE_INTERNAL
#include "vfile.h"

#ifdef ENABLE_VFILE_CURL
extern struct vf_module vf_mod_curl;
#endif

extern struct vf_module vf_mod_vftp;
extern struct vf_module vf_mod_vhttp;

struct vf_module *vfmod_tab[] = {
    &vf_mod_vftp,
    &vf_mod_vhttp,
#ifdef ENABLE_VFILE_CURL    
    &vf_mod_curl,
#endif
    NULL
};

static int          vfile_err_no = 0;
static const char   *vfile_err_ctx = NULL;

static int          verbose = 0; 
int                 *vfile_verbose = &verbose;
const char          *vfile_anonftp_passwd = "poldek@znienacka.net";

static void vfmsg(const char *fmt, ...);

void (*vfile_msgtty_fn)(const char *fmt, ...) = vfmsg;
void (*vfile_msg_fn)(const char *fmt, ...) = vfmsg;
void (*vfile_err_fn)(const char *fmt, ...) = vfmsg;

tn_hash *vfile_default_clients_ht = NULL;

struct vfile_conf_s {
    char      *cachedir;
    unsigned  flags;
    unsigned  mod_fetch_flags;   /* passed to mod->fetch() */
};

static struct vfile_conf_s vfile_conf = { "/tmp", 0, 0 };


int vfile_configure(int param, ...) 
{
    va_list  ap;
    int      v, rc;
    char     *vs;

    
    rc = 1;
    va_start(ap, param);

    switch (param) {
        
        case VFILE_CONF_CACHEDIR:
            vs = va_arg(ap, char*);
            if (vs) {
                vfile_conf.cachedir = n_strdup(vs);
                v = strlen(vfile_conf.cachedir);
                if (vfile_conf.cachedir[v - 1] == '/')
                    vfile_conf.cachedir[v - 1] = '\0';
            }
            break;
            
        case VFILE_CONF_REALUSERHOST_AS_ANONPASSWD:
            v = va_arg(ap, int);
            if (v) 
                vfile_conf.mod_fetch_flags |= VFMOD_USER_AS_ANONPASSWD;
            else
                vfile_conf.mod_fetch_flags &= ~VFMOD_USER_AS_ANONPASSWD;
            
            break;

        case VFILE_CONF_DEFAULT_CLIENT: {
            char *proto, *client;

            proto = va_arg(ap, char *);
            if (proto)
                client = va_arg(ap, char *);
            
            if (proto && client) {
                if (strcmp(client, "internal") == 0) {
                    if (n_hash_exists(vfile_default_clients_ht, proto))
                        n_hash_remove(vfile_default_clients_ht, proto);
                } else 
                    n_hash_replace(vfile_default_clients_ht, proto,
                                   n_strdup(client));
            }
            break;    
        };
    }

    va_end(ap);
    return rc;
}


void vfile_init(void) 
{
    int n;

    n = 0;
    while (vfmod_tab[n] != NULL)
        vfmod_tab[n++]->init();

    vfile_default_clients_ht = n_hash_new(7, free);
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
const struct vf_module *select_vf_module(const char *path) 
{
    const struct vf_module *mod = NULL;
    char proto[64];

    vf_url_proto(proto, sizeof(proto), path);
    
    if (!n_hash_exists(vfile_default_clients_ht, proto)) {
        unsigned urltype = vf_url_type(path);
        mod = find_vf_module(urltype);
    }
    
    return mod;
}

int vfile_fetcha(const char *destdir, tn_array *urls) 
{
    const struct vf_module *mod = NULL;
    int rc = 1;

    if (!vf_mkdir(destdir))
        return 0;
    
    if ((mod = select_vf_module(n_array_nth(urls, 0))) == NULL) {
        rc = vfile_fetcha_ext(destdir, urls);
        
    } else {
        int i;
        for (i=0; i<n_array_size(urls); i++) {
            char destpath[PATH_MAX], *url;

            url = n_array_nth(urls, i);
            snprintf(destpath, sizeof(destpath), "%s/%s", destdir,
                     n_basenam(url));
            vfile_msg_fn(_("Retrieving %s...\n"), PR_URL(url));
            if (!mod->fetch(destpath, url, vfile_conf.mod_fetch_flags)) {
                rc = 0;
                break;
            }
        }
    }
    
    return rc;
}


int vfile_fetch(const char *destdir, const char *path) 
{
    const struct vf_module *mod = NULL;
    int rc;

    if (!vf_mkdir(destdir))
        return 0;

    if ((mod = select_vf_module(path)) == NULL)
        rc = vfile_fetch_ext(destdir, path);
    
    else {
        char destpath[PATH_MAX];

        snprintf(destpath, sizeof(destpath), "%s/%s", destdir,
                 n_basenam(path));
        vfile_msg_fn(_("Retrieving %s...\n"), PR_URL(path));
        rc = mod->fetch(destpath, path, vfile_conf.mod_fetch_flags);
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
                vfile_err_fn("open %s: %m\n", CL_URL(path));
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
                        vfile_err_fn("%s: %m\n", CL_URL(path));
                    else if (Z_MEM_ERROR) 
                        vfile_err_fn("gzopen %s: insufficient memory\n", CL_URL(path));
                    else 
                        vfile_err_fn("gzopen %s: unknown error\n", CL_URL(path));
                    break;
                }

                vf->vf_stream = fopencookie(gzstream, mode, gzio_cookie);
                if (vf->vf_stream != NULL) {
                    rc = 1;
                    fseek(vf->vf_stream, 0, SEEK_SET); /* glibc BUG (?) */
                } else
                    vfile_err_fn("fopencookie %s: hgw error\n", CL_URL(path));

            } else {
#endif                
                if ((vf->vf_stream = fopen(path, mode)) != NULL) 
                    rc = 1;
                else 
                    vfile_err_fn("%s: %m\n", CL_URL(path));
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
                    vfile_err_fn("%s: %m\n", CL_URL(path));
                else if (Z_MEM_ERROR) 
                    vfile_err_fn("gzopen %s: insufficient memory\n", CL_URL(path));
                else 
                    vfile_err_fn("gzopen %s: unknown error\n", CL_URL(path));
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
                vfile_err_fn("%s: %m\n", CL_URL(path));
        }
        break;
#endif
        
#ifdef ENABLE_VFILE_RPMIO
        case VFT_RPMIO:
            if (vfmode & VFM_RW) {
                vfile_err_fn("%s: cannot open rw rpm\n", CL_URL(path));
                return 0;
            }

            vf->vf_fdt = Fopen(path, "r.fdio");
            if (vf->vf_fdt == NULL || Ferror(vf->vf_fdt)) {
                vfile_err_fn("open %s: %s\n", CL_URL(path), Fstrerror(vf->vf_fdt));
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
        vfile_err_fn("%s: cannot open remote file for writing\n", CL_URL(path));
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
            vfile_err_fn("vfile_close: type %d not supported\n",
                         vf->vf_type);
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



