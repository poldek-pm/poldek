/* 
  Copyright (C) 2000, 2001 Pawel A. Gajda (mis@k2.net.pl)
 
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
#include <trurl/nhash.h>
#include <trurl/narray.h>


#include "vfile.h"

#ifdef ENABLE_VFILE_CURL
# include "vfcurl.h"
#endif

static int verbose = 0; 
int *vfile_verbose = &verbose;

static int vfmsg(const char *fmt, ...);

int (*vfile_msg_fn)(const char *fmt, ...) = vfmsg;
int (*vfile_err_fn)(const char *fmt, ...) = vfmsg;

#define VFILE_CNFCURL (1 << 15)

struct vfile_conf_s {
    char *cachedir;
    unsigned flags;
};

static struct vfile_conf_s vfile_conf = { "/tmp", 0};

#ifdef HAVE_FOPENCOOKIE

#if __GNUC_PREREQ (2,2)
int gzfseek(void *stream, _IO_off64_t *offset, int whence)
{
    z_off_t off = *offset;
    int rc;
    
    rc = gzseek(stream, off, whence);
    if (rc >= 0)
        rc = 0;

    //printf("zfseek (%p, %ld, %lld, %d) = %d\n", stream, off, *offset, whence, rc);
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


cookie_io_functions_t gzio_cookie = {
    (cookie_read_function_t*)gzread,
    (cookie_write_function_t*)gzwrite,
    gzfseek,
    (cookie_close_function_t*)gzclose
};

#endif /* HAVE_FOPENCOOKIE */

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

#ifdef ENABLE_VFILE_CURL
    if ((vfile_conf.flags & VFILE_CNFCURL) == 0) {
        vfile_curl_init();
        vfile_conf.flags |= VFILE_CNFCURL;
    }
#endif    
    
}


#ifdef ENABLE_VFILE_RPMIO 
static void *fetch_cb(const unsigned long amount, const unsigned long total) 
{
    static unsigned long prev_v = 0, vv = 0, tvv = 0;
    
    
    if (amount && amount == total) /* last notification */
        vfile_msg_fn(".done (%ld kB)\n", total/1024);

    else if (amount == 0) {
        vfile_msg_fn("%6ld kB .", tvv*4032);
        vv = 0;
    }
    else if (total == 0) {
        vv++;
        if (++vv % 63 == 0) {
            tvv++;
            vfile_msg_fn("\n%6ld kB .", tvv*4032);
        } else
            vfile_msg_fn(".");
        
            
    }
    else {
        unsigned long i;
        unsigned long v = amount * 60 / total;
        for (i=prev_v; i<v; i++)
            vfile_msg_fn(".");
        prev_v = v;
    }
    return NULL;
}


static void *fetch_cb_wrapper(const void *h __attribute__((unused)), 
                              const rpmCallbackType t __attribute__((unused)), 
                              const unsigned long amount, 
                              const unsigned long total,
                              const void *pkgKey __attribute__((unused)),
                              void * data __attribute__((unused)))
{
    return fetch_cb(amount, total);
}
#endif /* ENABLE_VFILE_RPMIO */


static
int fetch_file_internal(const char *destdir, const char *path) 
{
    char tmpath[PATH_MAX];
    int rc = -1;

    snprintf(tmpath, sizeof(tmpath), "%s/%s", destdir, n_basenam(path));

    if (!vfile_valid_path(tmpath))
        return 0;
    
    vfile_msg_fn("Retrieving %s...\n", path);

    if (vfile_conf.flags & VFILE_CNFCURL) {
#ifdef ENABLE_VFILE_CURL
        rc = vfile_curl_fetch(tmpath, path);
#endif

    } else {

#ifdef ENABLE_VFILE_RPMIO
        int rpmrc;
        urlSetCallback(fetch_cb_wrapper, NULL, 65536);
        if ((rpmrc = urlGetFile(path, tmpath)) < 0) {
            vfile_err_fn("rpmio: %s transfer failed: %s\n", path,
                         ftpStrerror(rpmrc));
        }
        rc = (rpmrc == 0);
    }
#endif
    return rc;
}


static int fetch_internal(int urltype) 
{
    int internal = 1;
    
    switch (urltype) {
        case VFURL_FTP:
            if (vfile_conf.flags & VFILE_USEXT_FTP)
                internal = 0;
            break;
            
        case VFURL_HTTP:
            if (vfile_conf.flags & VFILE_USEXT_HTTP)
                internal = 0;
            break;

        case VFURL_HTTPS:
            internal = 0;
            if (vfile_conf.flags & VFILE_CNFCURL) /* has curl? */
                internal = 1;
            break;

        default:
            internal = 0;
    }

    if (vfile_configured_handlers() == 0)
        internal = 1;

    return internal;
}

int vfile_fetcha(const char *destdir, tn_array *urls, int urltype) 
{
    int rc = 1;

    n_assert(urltype > 0);
    if (urltype == VFURL_UNKNOWN) 
        urltype = vfile_url_type(n_array_nth(urls, 0));

    
    if (fetch_internal(urltype) == 0) {
        rc = vfile_fetcha_ext(destdir, urls, urltype);
        
    } else {
        int i;
        
        for (i=0; i<n_array_size(urls); i++) 
            if (!fetch_file_internal(destdir, n_array_nth(urls, i))) {
                rc = 0;
                break;
            }
    }
    
    return rc;
}


int vfile_fetch(const char *destdir, const char *path, int urltype) 
{
    int rc;

    if (!vfile_mkdir(destdir))
        return 0;

    n_assert(urltype > 0);
    if (urltype == VFURL_UNKNOWN) 
        urltype = vfile_url_type(path);

    if (fetch_internal(urltype)) {
        rc = fetch_file_internal(destdir, path);
        
    } else if ((rc = vfile_fetch_ext(destdir, path, urltype)) == 0 &&
               (urltype & (VFURL_HTTP | VFURL_FTP))) {
        vfile_err_fn("Transfer failed, trying internal client...\n");
        rc = fetch_file_internal(destdir, path);
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


static int read_md(const char *path, char *md, int mdsize) 
{
    int fd, nread;

    if ((fd = open(path, O_RDONLY)) < 0) 
        return 0;
    	
    nread = read(fd, md, mdsize);
    close(fd);
    
    return nread;
}

/* -1 on err */
static int is_uptodate(const char *mdpath, int urltype) 
{
    char tmpdir[PATH_MAX], l_mdpath[PATH_MAX];
    int len, is_uptod = -1;
    char l_md[128], md[128];
    int l_md_size, md_size;

    len = snprintf(l_mdpath, sizeof(l_mdpath), "%s/", vfile_conf.cachedir);
    len += vfile_url_as_path(&l_mdpath[len], sizeof(l_mdpath) - len, mdpath);
    if ((l_md_size = read_md(l_mdpath, l_md, sizeof(l_md))) < 31) 
        return 0;   /* no *.md in cache */
    
    len = snprintf(tmpdir, sizeof(tmpdir), "%s/tmpmd", vfile_conf.cachedir);

    if (sizeof(tmpdir) - len < 128)
        return 0;
    
    if (!vfile_mkdir(tmpdir))
        return 0;
    
    snprintf(&tmpdir[len], sizeof(tmpdir) - len, "/%s", n_basenam(mdpath));
    
    if (!vfile_valid_path(tmpdir))
        return -1;
    
    if (access(tmpdir, R_OK) == 0) /* unlink old *.md */
        if (unlink(tmpdir) != 0) {
            vfile_err_fn("%s: %m\n", tmpdir);
            return -1;
        }

    tmpdir[len] = '\0';         /* back to dir */
    
    if (vfile_fetch(tmpdir, mdpath, urltype)) {
        char tmpath[PATH_MAX];

        is_uptod = 0;
        
        snprintf(tmpath, sizeof(tmpath), "%s/%s", tmpdir, n_basenam(mdpath));
        md_size = read_md(tmpath, md, sizeof(md));
        
        if (md_size > 31 && md_size == l_md_size && strcmp(l_md, md) == 0)
            is_uptod = 1;
    }
    
    return is_uptod;
}

char *mkmdpath(char *mdpath, int size, const char *path) 
{
    char *p;

    snprintf(mdpath, size, "%s", path);
    if ((p = strrchr(n_basenam(mdpath), '.')) == NULL || strcmp(p, ".gz") != 0)
        p = strrchr(mdpath, '\0');
    
    snprintf(p, sizeof(mdpath) - (p - mdpath), ".md");
    return mdpath;
}


struct vfile *do_vfile_open(const char *path, int vftype, int vfmode)
{
    struct vfile vf, *rvf = NULL;
    int opened, mdopened, urltype;
    char mdpath[PATH_MAX] = {'\0'};
    
    vf.vf_fdt = NULL;
    vf.vf_tmpath = NULL;
    vf.vf_mdtmpath = NULL;
    vf.vf_type = vftype;
    vf.vf_mode = vfmode;
    vf.vf_flags = 0;
    
    urltype = vfile_url_type(path);
    opened = 0;
    mdopened = 1;

    if (vfmode & VFM_MDUP)
        vfmode |= VFM_MD;
    
    if (vfmode & VFM_MD) {
        mdopened = 0;
        mkmdpath(mdpath, sizeof(mdpath), path);
    }
    
    if (urltype == VFURL_PATH) {
        if (openvf(&vf, path, vfmode)) {
            if (vfmode & VFM_MD)
                vf.vf_mdtmpath = strdup(mdpath);
            opened = 1;
        }
        
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
            int is_uptod = 1;
            
            if (vfmode & VFM_MDUP) {
                if ((is_uptod = is_uptodate(mdpath, urltype)) < 0)
                    return 0;
            }
            
            if (is_uptod > 0) {
                vfile_url_as_path(&buf[len], sizeof(buf) - len, path);
                if (access(buf, R_OK) == 0 && openvf(&vf, buf, vfmode)) {
                    vf.vf_tmpath = strdup(buf);

                    if (vfmode & VFM_MD) {
                        vfile_url_as_path(&buf[len], sizeof(buf) - len, mdpath);
                        vf.vf_mdtmpath = strdup(buf);
                    }
                    opened = 1;
                    vf.vf_flags |= VF_FRMCACHE;
                    
                } else {
                    unlink(buf);
                }
            }
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

            if (vfmode & VFM_MD) {
                char tmpath[PATH_MAX];
                int  nerr = 0;
                
                snprintf(tmpath, sizeof(tmpath), "%s/%s", tmpdir,
                         n_basenam(mdpath));
                
                if (!vfile_valid_path(tmpath))
                    nerr++;
                else 
                    unlink(tmpath);

                snprintf(tmpath, sizeof(tmpath), "%s/%s", tmpdir,
                         n_basenam(path));
                
                if (!vfile_valid_path(tmpath))
                    nerr++;
                else 
                    unlink(tmpath);
                
                if (nerr == 0) 
                    mdopened = vfile_fetch(tmpdir, mdpath, urltype);
            }
            
            
            if (mdopened && vfile_fetch(tmpdir, path, urltype)) {
                char tmpath[PATH_MAX];

                snprintf(tmpath, sizeof(tmpath), "%s/%s", tmpdir,
                         n_basenam(path));
                
                if (openvf(&vf, tmpath, VFM_RO)) {
                    vf.vf_tmpath = strdup(tmpath);
                    opened = 1;
                    vf.vf_flags |= VF_FETCHED;
                    
                    if (VFM_MD) {
                        snprintf(tmpath, sizeof(tmpath), "%s/%s", tmpdir,
                                 n_basenam(mdpath));
                        vf.vf_mdtmpath = strdup(tmpath);
                    }

                } else {
                    //unlink(tmpath); wget && co sometimes badly returns non zero 
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

struct vfile *vfile_open(const char *path, int vftype, int vfmode) 
{
    struct vfile *vf = NULL;
    int n = 0;

    
    while (1) {
        if ((vf = do_vfile_open(path, vftype, vfmode)))
            break;
        
        if ((vfmode & VFM_STBRN) == 0)
            break;

        if (n > 1000) {
            vfile_msg_fn("Give up (#%d)...\n", ++n);
            break;
        }
        
        vfile_msg_fn("Retrying %s (#%d)...\n", path, ++n);
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
    
    if (vf->vf_tmpath) {        /* set for remote files only  */
        if ((vf->vf_mode & (VFM_NORM | VFM_CACHE)) == 0 &&
            vfile_valid_path(vf->vf_tmpath))
            unlink(vf->vf_tmpath);
        free(vf->vf_tmpath);
        vf->vf_tmpath = NULL;
    }

    if (vf->vf_mdtmpath) {
        if ((vf->vf_mode & (VFM_NORM | VFM_CACHE)) == 0 &&
            vfile_valid_path(vf->vf_mdtmpath))
            unlink(vf->vf_mdtmpath);
        
        free(vf->vf_mdtmpath);
        vf->vf_mdtmpath = NULL;
    }

    free(vf);
}

static int vfmsg(const char *fmt, ...)
{
    va_list args;
    
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    fflush(stdout);
    va_end(args);
    return 1;
}
