/* 
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

#include <stdio.h>

#include <zlib.h>
#include <trurl/narray.h>

extern int *vfile_verbose;
extern const char *vfile_anonftp_passwd;
extern void (*vfile_msg_fn)(const char *fmt, ...);
extern void (*vfile_msgtty_fn)(const char *fmt, ...);
extern void (*vfile_err_fn)(const char *fmt, ...);

#define VFILE_USEXT_FTP    (1 << 0)
#define VFILE_USEXT_HTTP   (1 << 1)
#define VFILE_USEXT_HTTPS  (1 << 2)

#define VFILE_REALUSERHOST_AS_ANONPASSWD (1 << 5)

/* if any of args is not NULL or -1 then set up it */
void vfile_configure(const char *cachedir, int flags);


#define VFT_IO     1             /* open(2)                   */
#define VFT_STDIO  2             /* fopen(3)                  */
#define VFT_GZIO   3             /* zlib: gzopen()            */ 
#define VFT_RPMIO  4             /* rpmlib: Fopen()           */

#define VFM_RO         (1 << 0)  /* RO, this is the default   */
#define VFM_RW         (1 << 1)
#define VFM_APPEND     (1 << 3)  /* a+ */

#define VFM_NORM       (1 << 4)  /* (NoReMove) for remote files,
                                    remove tmp at close? */

#define VFM_CACHE      (1 << 5)  /* for remote files, use cached
                                                 file if it exists */

#define VFM_CACHE_ONLY (1 << 6)  /* for remote files, use cached file
                                    if it not exists return NULL */

#define VFM_NORMCACHE  (1 << 7)  /* for remote files, use cached file
                                    if it exists */

#define VFM_STBRN      (1 << 10)  /* infinite retrying to open file  */


#define VFM_NOEMPTY    (1 << 11)  /* treat empty files as non-existing ones */

#define VFM_UNCOMPR    (1 << 12)  /* uncompress file before open  */

/* flags  */
#define VF_FETCHED     (1 << 15) /* for remote files, file downloaded */
#define VF_FRMCACHE    (1 << 16) /* file remote file, file taken form cache */

struct vfile {
    int       vf_type;                /* VFT_*   */
    unsigned  vf_urltype;             /* VFURL_* */
    unsigned  vf_mode;                /* VFM_*   */
    unsigned  vf_flags;               /* VF_*    */ 
    union {
        int    vfile_fd;
        FILE   *vfile_stream;
        gzFile *vfile_gzstream;
        void   *vfile_fdt;        /* RPM's FD_t */
    } vfile_fdescriptor;

    char          *vf_path;
    char          *vf_tmpath;
    int16_t       _refcnt;
};

#define	vf_fd        vfile_fdescriptor.vfile_fd
#define	vf_stream    vfile_fdescriptor.vfile_stream
#define	vf_gzstream  vfile_fdescriptor.vfile_gzstream
#define	vf_fdt       vfile_fdescriptor.vfile_fdt

#define vfile_localpath(vf)  ((vf)->vf_tmpath ? (vf)->vf_tmpath : (vf)->vf_path)

struct vfile *vfile_open(const char *path, int vftype, unsigned vfmode);
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
#define VFURL_REMOTE   (VFURL_FTP | VFURL_HTTP | VFURL_HTTPS | VFURL_RSYNC)


#define vfile_is_remote(vf) ((vf)->vf_urltype & VFURL_REMOTE)

/* external downloaders */
int vfile_register_ext_handler(unsigned urltypes, const char *fmt);
int vfile_configured_handlers(void);

int vfile_fetch_ext(const char *destdir, const char *url, int urltype);
int vfile_fetcha_ext(const char *destdir, tn_array *urls, int urltype);

int vfile_fetch(const char *destdir, const char *url, int urltype);
int vfile_fetcha(const char *destdir, tn_array *urls, int urltype);


int vf_url_type(const char *url);
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


#ifdef VFILE_INTERNAL

#include <trurl/n_snprintf.h>

#define VF_PROGRESS_VIRGIN    0
#define VF_PROGRESS_DISABLED  1
#define VF_PROGRESS_RUNNING   2

struct vf_progress_bar {
    int     width;
    int     state;
    int     is_tty;
    long    prev_n;
};

void vfile_progress_init(struct vf_progress_bar *bar);
void vfile_progress(long total, long amount, void *data);

void vfile_set_errno(const char *ctxname, int vf_errno);

#define VFMOD_INFINITE_RETR       (1 << 0) /* retry download */
#define VFMOD_USER_AS_ANONPASSWD  (1 << 1) /* send login@host as FTP password  */

struct vf_module {
    char       vfmod_name[32];
    unsigned   vf_protocols;
    int  (*init)(void);
    void (*destroy)(void);
    int  (*fetch)(const char *dest, const char *url, unsigned flags);

    int        _pri;            /* used by vfile only */
};


/* short alias for */
#define CL_URL(url) vf_url_hidepasswd_s(url)
#define PR_URL(url) vf_url_slim_s(url, 60)

#endif /* VFILE_INTERNAL */


int vf_uncompr_able(const char *path);
int vf_uncompr_do(const char *path, const char *destpath);

void *sigint_establish(void);
void sigint_restore(void *sigint_fn);
int sigint_reached(void);

#endif /* POLDEK_VFILE_H */

