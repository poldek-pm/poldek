/* $Id$ */
#ifndef POLDEK_VFILE_H
#define POLDEK_VFILE_H

#include <stdio.h>
#include <zlib.h>

#define VFILE_USEXT_FTP   (1 << 0)
#define VFILE_USEXT_HTTP  (1 << 1)

/* if any of args is not NULL or -1 than set up it */
void vfile_configure(const char *cachedir, int flags);


#define VFT_IO     1             /* open(2)                   */
#define VFT_STDIO  2             /* fopen(3)                  */
#define VFT_GZIO   3             /* zlib: gzopen(), obsolete  */ 
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
#ifdef VFILE_RPMIO_SUPPORT
        void   *vfile_fdt;        /* RPM's FD_t */
#endif
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
void vfile_set_tmdir(const char *tmpdir);


/* from fetch.c */
#include <trurl/narray.h>

#define VFURL_PATH    (1 << 0)
#define VFURL_FTP     (1 << 1)
#define VFURL_HTTP    (1 << 2)
#define VFURL_HTTPS   (1 << 3)
#define VFURL_RSYNC   (1 << 4)

int vfile_url_type(const char *url);
char *vfile_url_as_dirpath(char *buf, size_t size, const char *url);
char *vfile_url_as_path(char *buf, size_t size, const char *url);

/* external downloaders */
int vfile_register_ext_handler(unsigned urltypes, char *fmt);
int vfile_configured_handlers(void);

int vfile_fetch(const char *destdir, const char *url, int urltype);
int vfile_fetcha(const char *destdir, tn_array *urls, int urltype);

#endif /* POLDEK_VFILE_H */
