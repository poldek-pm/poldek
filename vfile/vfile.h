/*
  Not so transparent IO layer. 
  Copyright (C) 2000 - 2002 Pawel A. Gajda (mis@k2.net.pl)

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 as published
  by the Free Software Foundation;
 
  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  59 Place - Suite 330, Boston, MA 02111-1307, USA.  
*/

/* $Id$ */

#ifndef POLDEK_VFILE_H
#define POLDEK_VFILE_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>

#include <zlib.h>
#include <trurl/narray.h>

#undef ENABLE_VFILE_TRURLIO
#define ENABLE_VFILE_TRURLIO    /* is ON, cond compilation NFY  */
#ifdef ENABLE_VFILE_TRURLIO
# include <trurl/nstream.h>
#endif

#define VFILE_LOG_INFO  (1 << 0)
#define VFILE_LOG_WARN  (1 << 1)
#define VFILE_LOG_ERR   (1 << 2)
#define VFILE_LOG_TTY   (1 << 8)


#define VFILE_CONF_CACHEDIR               (1 << 0) /*const char *path        */
#define VFILE_CONF_DEFAULT_CLIENT         (1 << 1) /*const char *proto, *name */
#define VFILE_CONF_SYSUSER_AS_ANONPASSWD  (1 << 2)  /*int 0/non zero          */
#define VFILE_CONF_VERBOSE                (1 << 3) /*int &verbose_level      */
#define VFILE_CONF_PROXY                  (1 << 4) /*const char *proto, *url */
#define VFILE_CONF_ANONFTP_PASSWD         (1 << 5) /*const char *passwd      */
#define VFILE_CONF_LOGCB                  (1 << 6) /*vf_vlog() like fn     */
#define VFILE_CONF_PROGRESSCB             (1 << 7)
#define VFILE_CONF_PROGRESSDATA           (1 << 8)
#define VFILE_CONF_STUBBORN_RETR          (1 << 9)
#define VFILE_CONF_EXTCOMPR               (1 << 10) /* use external script to
                                                       file (de)compression */
#define VFILE_CONF_SIGINT_REACHED         (1 << 11)
int vfile_configure(int param, ...);

/* run it after configuration is done */
void vfile_setup(void);



#define VFT_IO       1             /* open(2)                   */
#define VFT_STDIO    2             /* fopen(3)                  */
#define VFT_GZIO     3             /* zlib: gzopen()            */ 
#define VFT_TRURLIO  5             /* trurlib's tn_stream       */

#define VFM_RO          (1 << 0)  /* O_RDONLY, this is the default   */
#define VFM_RW          (1 << 1)  /* O_RDWR */
#define VFM_APPEND      (VFM_RW | (1 << 3))  /* O_RDWR | O_APPEND */
#define VFM_NODEL       (1 << 4)  /* do not delete cached copy at close */
#define VFM_NORM        VFM_NODEL /* legacy */
#define VFM_CACHE       (1 << 5) /* use cached file if it exists, implies NODEL*/
#define VFM_CACHE_ONLY  (1 << 6) /* use cached file or fail if it not exists   */
#define VFM_CACHE_NODEL (1 << 7) /* don't delete cached file before downloading */
#define VFM_STBRN       (1 << 8) /* infinite retrying to open file  */
#define VFM_NOEMPTY     (1 << 9) /* treat empty files as non-existing ones */
#define VFM_UNCOMPR     (1 << 10) /* uncompress file before open  */

/* flags  */
#define VF_FETCHED     (1 << 12) /* for remote files, file downloaded */
#define VF_FRMCACHE    (1 << 13) /* file remote file, file taken form cache */

struct vfile {
    int       vf_type;                /* VFT_*   */
    unsigned  vf_urltype;             /* VFURL_* */
    unsigned  vf_mode;                /* VFM_*   */
    unsigned  vf_flags;               /* VF_*    */ 
    union {
        int        vfile_fd;
        FILE       *vfile_stream;
        gzFile     *vfile_gzstream;
#ifdef ENABLE_VFILE_TRURLIO        
        tn_stream  *vfile_tnstream;
#endif        
    } vfile_fdescriptor;
    char          *vf_path;
    char          *vf_tmpath;
    int16_t       _refcnt;
};

#define	vf_fd        vfile_fdescriptor.vfile_fd
#define	vf_stream    vfile_fdescriptor.vfile_stream
#define	vf_gzstream  vfile_fdescriptor.vfile_gzstream
#ifdef ENABLE_VFILE_TRURLIO
# define vf_tnstream  vfile_fdescriptor.vfile_tnstream
#endif

#define vfile_localpath(vf)  ((vf)->vf_tmpath ? (vf)->vf_tmpath : (vf)->vf_path)

struct vfile *vfile_open(const char *path, int vftype, unsigned vfmode);
struct vfile *vfile_open_sl(const char *path, int vftype, unsigned vfmode,
                            const char *site_label);
void vfile_close(struct vfile *vf);
struct vfile *vfile_incref(struct vfile *vf);

int vfile_unlink(struct vfile *vf);


#define VFURL_UNKNOWN (1 << 0)
#define VFURL_PATH    (1 << 1)
#define VFURL_FTP     (1 << 2)
#define VFURL_HTTP    (1 << 3)
#define VFURL_HTTPS   (1 << 4)
#define VFURL_RSYNC   (1 << 5)
#define VFURL_CDROM   (1 << 6)

#define VFURL_LOCAL    (VFURL_CDROM | VFURL_PATH)
#define VFURL_REMOTE   ~(VFURL_LOCAL)


#define vfile_is_remote(vf) ((vf)->vf_urltype & VFURL_REMOTE)

/* external fetchers */
int vfile_register_ext_handler(const char *name, tn_array *protocols,
                               const char *cmd);
int vfile_is_configured_ext_handler(const char *url);

struct vf_stat {
    off_t   vf_size;
    time_t  vf_mtime;

    off_t   vf_local_size;
    time_t  vf_local_mtime;
};

int vf_stat(const char *url, const char *destdir, struct vf_stat *vfstat);

#if 0                           /* NFY */
#define VF_FETCH_DEFAULT  0
#define VF_FETCH_NOEXT    (0 << 1) /* do not use external handlers */
#define VF_FETCH_NOREST   (0 << 2) /* download file from scratch   */
#endif

int vf_fetch_sl(const char *url, const char *dest_dir, const char *site_label);
int vf_fetch(const char *url, const char *destdir);
int vf_fetcha_sl(tn_array *urls, const char *destdir, const char *site_label);
int vf_fetcha(tn_array *urls, const char *destdir);

int vf_url_type(const char *url);
char *vf_url_proto(char *proto, int size, const char *url);
int vf_url_as_dirpath(char *buf, size_t size, const char *url);
int vf_url_as_path(char *buf, size_t size, const char *url);

/* replace password with "x" * len(password) */
const char *vf_url_hidepasswd(char *buf, int size, const char *url);
const char *vf_url_hidepasswd_s(const char *url);

/* applies vf_url_hidepasswd() + slim down url string to maxl */
const char *vf_url_slim(char *buf, int size, const char *url, int maxl);
const char *vf_url_slim_s(const char *url, int maxl);


int vf_valid_path(const char *path);
int vf_mkdir(const char *path);
int vf_unlink(const char *path);

/* mkdir under cachedir */
int vf_mksubdir(char *path, int size, const char *dirpath);

/* url to local path */
int vf_localpath(char *path, size_t size, const char *url);
int vf_localdirpath(char *path, size_t size, const char *url);

/* unlink local copy */
int vf_localunlink(const char *path);

int vf_userathost(char *buf, int size);
int vf_cleanpath(char *buf, int size, const char *path);

int vf_find_external_command(char *cmdpath, int size, const char *cmd,
                             const char *PATH);
#endif /* POLDEK_VFILE_H */

