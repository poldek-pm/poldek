/* 
  Copyright (C) 2000 Pawel Gajda (mis@k2.net.pl)

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
 
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

extern int vfile_verbose;
extern int (*vfile_msg_fn)(const char *fmt, ...);
extern int (*vfile_err_fn)(const char *fmt, ...);

#define VFILE_USEXT_FTP   (1 << 0)
#define VFILE_USEXT_HTTP  (1 << 1)

/* if any of args is not NULL or -1 then set up it */
void vfile_configure(const char *cachedir, int flags);


#define VFT_IO     1             /* open(2)                   */
#define VFT_STDIO  2             /* fopen(3)                  */
#define VFT_GZIO   3             /* zlib: gzopen()            */ 
#define VFT_RPMIO  4             /* rpmlib: Fopen()           */

#define VFM_RO     (1 << 0)      /* RO, this is the default   */
#define VFM_RW     (1 << 1)      
#define VFM_NORM   (1 << 2)      /* for remote files, remove tmp at close? */
#define VFM_CACHE  (1 << 3)      /* for remote files, use cached file
                                    if it exists */
struct vfile {
    int vf_type;                /* VFT_* */
    int vf_mode;                /* VFM_* */ 
    union {
        int    vfile_fd;
        FILE   *vfile_stream;
        gzFile *vfile_gzstream;
        void   *vfile_fdt;        /* RPM's FD_t */
    } vfile_fdescriptor;
    char *vf_tmpath;
};

#define	vf_fd        vfile_fdescriptor.vfile_fd
#define	vf_stream    vfile_fdescriptor.vfile_stream
#define	vf_gzstream  vfile_fdescriptor.vfile_gzstream
#define	vf_fdt       vfile_fdescriptor.vfile_fdt

#define vf_localpath(vfile)  (vfile)->vf_tmpath

struct vfile *vfile_open(const char *path, int vftype, int vfmode);
void vfile_close(struct vfile *vf);

#define VFURL_UNKNOWN (1 << 0)
#define VFURL_PATH    (1 << 1)
#define VFURL_FTP     (1 << 2)
#define VFURL_HTTP    (1 << 3)
#define VFURL_HTTPS   (1 << 4)
#define VFURL_RSYNC   (1 << 5)

int vfile_url_type(const char *url);
char *vfile_url_as_dirpath(char *buf, size_t size, const char *url);
char *vfile_url_as_path(char *buf, size_t size, const char *url);

/* external downloaders */
int vfile_register_ext_handler(unsigned urltypes, const char *fmt);
int vfile_configured_handlers(void);

int vfile_fetch(const char *destdir, const char *url, int urltype);
int vfile_fetcha(const char *destdir, tn_array *urls, int urltype);

#endif /* POLDEK_VFILE_H */
