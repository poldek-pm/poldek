/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include <zlib.h>
#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nhash.h>
#include <trurl/narray.h>
#include <trurl/nmalloc.h>

#include "compiler.h"
#include "i18n.h"

#include "vfile.h"
#include "vfile_intern.h"

static int          vfile_err_no = 0;
static const char   *vfile_err_ctx = NULL;

static int          verbose = 0; 
int                 *vfile_verbose = &verbose;

static const char   default_anon_passwd[] = "poldek@znienacka.net";
extern struct vf_progress vf_tty_progress;

struct vfile_configuration vfile_conf = {
    NULL, VFILE_CONF_STUBBORN_RETR, 1000 /* nretries */,
    NULL, NULL, NULL,
    &verbose, 
    (char*)default_anon_passwd,
    NULL, NULL, &vf_tty_progress
};

static inline const char *vfile_cachedir(void)
{
    if (vfile_conf._cachedir == NULL)
        n_die("vfile: cachedir must be configured");
    n_assert(vfile_conf._cachedir);
    n_assert(*vfile_conf._cachedir);
    return vfile_conf._cachedir;
}

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

    if (vfile_conf.noproxy == NULL)
        vfile_conf.noproxy = n_array_new(16, free, NULL);
        
    rc = 1;
    va_start(ap, param);

    switch (param) {
        case VFILE_CONF_LOGCB:
            vfile_conf.log = va_arg(ap, void*);
            break;

        case VFILE_CONF_PROGRESS:
            if ((vv = va_arg(ap, void*)))
                vfile_conf.bar = vv;
            break;
            
        case VFILE_CONF_VERBOSE:
            vp = va_arg(ap, int*);
            if (vp)
                vfile_conf.verbose = vfile_verbose = vp;
            
            else
                vfile_conf.verbose = vfile_verbose = &verbose;
            
            break;

        case VFILE_CONF_CACHEDIR:
            vs = va_arg(ap, char*);
            if (vs) {
                vfile_conf._cachedir = n_strdup(vs);
                v = strlen(vfile_conf._cachedir);
                if (vfile_conf._cachedir[v - 1] == '/')
                    vfile_conf._cachedir[v - 1] = '\0';
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
            char *proto, *client = NULL;

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
            
        case VFILE_CONF_NOPROXY: {
            char *mask;
            
            mask = va_arg(ap, char *);
            if (mask && *mask)
                n_array_push(vfile_conf.noproxy, mask);
            
            break;    
        }

        case VFILE_CONF_STUBBORN_NRETRIES:
            v = va_arg(ap, int);
            if (v == 0)
                v = 1;
            vfile_conf.nretries = v;
            break;
            
        case VFILE_CONF_SIGINT_REACHED:
            // fails on gcc 2.95 
            // vfile_conf.sigint_reached = va_arg(ap, int (*)(int));
            vfile_conf.sigint_reached = va_arg(ap, void*);
            break;

        default: {
            v = va_arg(ap, int);
            if (v) 
                vfile_conf.flags |= param;
            else
                vfile_conf.flags &= ~param;
        }
            break;
            
    }

    va_end(ap);
    return rc;
}

/* RET: bool */
static int openvf(struct vfile *vf, const char *path, int vfmode) 
{ 
    int rc = 0;

    n_assert(vfmode & (VFM_RO | VFM_RW));
    if ((vfmode & VFM_RW) && (vfmode & VFM_APPEND) != VFM_APPEND)
        vf_unlink(path);

    switch (vf->vf_type) {
        case VFT_IO: {
            int flags = 0;
            
            if (vfmode & VFM_RW) {
                flags |= O_RDWR;
                if ((vfmode & VFM_APPEND) == VFM_APPEND)
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
            char *mode = NULL;

            if ((vfmode & VFM_APPEND) == VFM_APPEND)
                mode = "a+";
            else if (vfmode & VFM_RW)
                mode = "w";
            else
                mode = "r";

            if ((vf->vf_stream = fopen(path, mode)) != NULL) 
                rc = 1;
            else 
                vf_logerr("%s: %m\n", CL_URL(path));
        }
        break;
        
        case VFT_GZIO: {
            char *mode = NULL;

            if ((vfmode & VFM_APPEND) == VFM_APPEND)
				mode = "a";
            else if (vfmode & VFM_RW)
                mode = "w";
			else
                mode = "r";
            
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
            char *mode = NULL;
            
            if ((vfmode & VFM_APPEND) == VFM_APPEND)
				mode = "a";
            else if (vfmode & VFM_RW)
                mode = "w";
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


static const char *vfdecompr(const char *path, char *dest, int size) 
{
    char *destdir, destdir_buf[PATH_MAX], uncmpr_path[PATH_MAX];
    const char *cachedir = NULL;
    struct vflock *vflock;
    int  len, rc;
    
    
    *dest = '\0';
    
    if (!vf_decompressable(path, uncmpr_path, sizeof(uncmpr_path)))
        return path;

    cachedir = vfile_cachedir();
    
    len = strlen(cachedir);
    
       /* file under cachedir */
    if (strncmp(uncmpr_path, cachedir, len) == 0) { 
        char *bn;
        n_snprintf(destdir_buf, sizeof(destdir_buf), "%s", uncmpr_path);
        n_basedirnam(destdir_buf, &destdir, &bn);
        
        n_snprintf(dest, size, "%s", uncmpr_path);
        
    } else {
        char *s = n_strdup(uncmpr_path);
        vf_localdirpath(destdir_buf, sizeof(destdir_buf), n_dirname(s));
        free(s);
        destdir = destdir_buf;
        
        vf_localpath(dest, size, uncmpr_path);
    }
    
        
    //printf("DEST %s = %s\n", path, dest);
    n_assert(destdir);
    if ((vflock = vf_lock_mkdir(destdir)) == NULL)
        return NULL;
    
    rc = vf_extdecompress(path, dest);

    vf_lock_release(vflock);
    return rc ? dest : NULL;
}

static
struct vfile *do_vfile_open(const char *path, int vftype, int vfmode,
                            const char *urlabel)
{
    struct vfile vf, *rvf = NULL;
    int opened, urltype;
    char buf[PATH_MAX];
    char *tmpdir, *p = NULL, *tmpath = NULL;
    const char *rpath, *opath;
    enum vf_fetchrc ftrc;
    int len;

    
    if (vfile_conf.flags & VFILE_CONF_EXTCOMPR)
        vfmode |= VFM_UNCOMPR;
    
    vf.vf_stream = NULL;
    vf.vf_path = NULL;
    vf.vf_tmpath = NULL;
    vf.vf_type = vftype;
    vf.vf_mode = vfmode;
    vf.vf_flags = 0;
    
    urltype = vf_url_type(path);
    opened = 0;

    if (urltype == VFURL_PATH) {
        opath = rpath = path; 
        if (vfmode & VFM_UNCOMPR) {
            if (vfmode & VFM_RW) {
                if (vf_decompressable(path, buf, sizeof(buf)))
                    opath = buf;
                
            } else {
                if ((opath = vfdecompr(path, buf, sizeof(buf))) == NULL)
                    return 0;
            }
        }
        
        if (openvf(&vf, opath, vfmode)) 
            opened = 1;
        goto l_end;
    }
    
    if (vfmode & VFM_RW) {
        vf_logerr("%s: cannot open remote file for writing\n", CL_URL(path));
        return 0;
    }
    
    len = n_snprintf(buf, sizeof(buf), "%s/", vfile_cachedir());
    vf_url_as_path(&buf[len], sizeof(buf) - len, path);
    
    if ((vfmode & VFM_CACHE) && file_ok(buf, vfmode)) {
        char tmpath[PATH_MAX];
        
        opath = rpath = buf;
        if ((vfmode & VFM_UNCOMPR)) {
            if ((opath = vfdecompr(buf, tmpath, sizeof(tmpath))) == NULL)
                return 0;
        }
    
        if (openvf(&vf, opath, vfmode)) {
            vf.vf_tmpath = n_strdup(rpath);
            opened = 1;
            vf.vf_flags |= VF_FRMCACHE;
        }
    }
    
    if (opened) 
        goto l_end;
        
    /* fetch */
    p = NULL;
    tmpath = NULL;

    if ((vfmode & VFM_CACHE_NODEL) == 0) /* delete cached copy? */
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

    if (vfile__vf_fetch(path, tmpdir, 0, urlabel, &ftrc)) {
        char tmpath[PATH_MAX], upath[PATH_MAX];;

        snprintf(tmpath, sizeof(tmpath), "%s/%s", tmpdir,
                 n_basenam(path));

        opath = rpath = tmpath;
        if ((vfmode & VFM_UNCOMPR)) {
            if ((opath = vfdecompr(tmpath, upath, sizeof(upath))) == NULL)
                return 0;
        }
            
        if (openvf(&vf, opath, VFM_RO)) {
            vf.vf_tmpath = n_strdup(rpath);
            opened = 1;
            if (ftrc == VF_FETCHRC_FETCHED)
                vf.vf_flags |= VF_FETCHED;
                    
        } else {
            if (*vfile_verbose > 1)
                vf_loginfo("vfile: rm -f %s\n", tmpath);
            vf_localunlink(tmpath);
            //wget && co sometimes badly returns non zero -> patch it!
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

struct vfile *vfile_open_ul(const char *path, int vftype, unsigned vfmode,
                            const char *urlabel) 
{
    struct vfile *vf = NULL;

    vfile_err_no = 0;
    if ((vf = do_vfile_open(path, vftype, vfmode, urlabel))) {
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
            
        default:
            vf_logerr("vfile_close: type %d not supported\n", vf->vf_type);
            n_assert(0);
    }

    if (vf->vf_path && (vf->vf_mode & VFM_UNCOMPR) && (vf->vf_mode & VFM_RW)) {
        n_assert(vf_url_type(vf->vf_path) & VFURL_LOCAL);
        if (vf_decompressable(vf->vf_path, NULL, 0)) {
            char src[PATH_MAX], *p;
            snprintf(src, sizeof(src), vf->vf_path);
            p = strrchr(src, '.');
            n_assert(p);
            *p = '\0';
            p++;
            vf_extcompress(src, p);
        }
        
    }
    
    if (vf->vf_path) {
        free(vf->vf_path);
        vf->vf_path = NULL;
    }
    
    if (vf->vf_tmpath) {        /* set for remote files only  */
        if ((vf->vf_mode & (VFM_NODEL | VFM_CACHE)) == 0)
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

    n = n_snprintf(path, size, "%s/%s", vfile_cachedir(), dirpath);
    vf_mkdir(path);
    return n;
}

int vf_localpath(char *path, size_t size, const char *url) 
{
    int n;
    
    n = n_snprintf(path, size, "%s/", vfile_cachedir());
    return n + vf_url_as_path(&path[n], size - n, url);
}

int vf_localdirpath(char *path, size_t size, const char *url) 
{
    int n;
    
    n = n_snprintf(path, size, "%s/", vfile_cachedir());
    return n + vf_url_as_dirpath(&path[n], size - n, url);
}


int vf_cachepath(char *path, size_t size, const char *ofdirpath)
{
    const char *cachedir = vfile_cachedir();
    int n, len;
    
    len = strlen(cachedir);

    n_assert(strlen(ofdirpath) > 0);
    n_assert(ofdirpath[strlen(ofdirpath) - 1] != '/'); /* not a dir */
                           
    if (strncmp(ofdirpath, cachedir, len) == 0) { 
        n = n_snprintf(path, size, "%s", ofdirpath);
        
    } else {
        n = vf_localdirpath(path, size, ofdirpath);
    }

    return n;
}

int vf_localunlink(const char *path) 
{
    if (strncmp(path, vfile_cachedir(), strlen(vfile_cachedir())) == 0 &&
        vf_valid_path(path))
        return unlink(path) == 0;
    
    return 0;
}


void vf_vlog(int pri, const char *fmt, va_list ap)
{
    if (vfile_conf.log)
        vfile_conf.log(pri, fmt, ap);

    else {
        vfprintf(stdout, fmt, ap);
        fflush(stdout);
    }
}

void vf_log(int pri, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vf_vlog(pri, fmt, args);
    va_end(args);
}

void vfile_set_errno(const char *ctxname, int vf_errno) 
{
    vfile_err_no = vf_errno;
    vfile_err_ctx = ctxname;
}

int vfile_sigint_reached(int reset)
{
    if (vfile_conf.sigint_reached)
        return vfile_conf.sigint_reached(reset);
    return 0;
}

