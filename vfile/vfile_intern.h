/* $Id$ */
#ifndef POLDEK_VFILE_INTERNAL
#define POLDEK_VFILE_INTERNAL

/* only external handlers are used */
int vf_fetch_ext(const char *url, const char *destdir);
int vf_fetcha_ext(tn_array *urls, const char *destdir);

int vf_lockdir(const char *path);
int vf_lock_mkdir(const char *path);
void vf_lock_release(int fd);

void vf_log(int pri, const char *fmt, ...);
void vf_vlog(int pri, const char *fmt, va_list ap);

#define vf_logerr(fmt, args...) \
       vf_log(VFILE_LOG_ERR, fmt, ## args)

#define vf_loginfo(fmt, args...) \
       vf_log(VFILE_LOG_INFO, fmt, ## args)

#include <trurl/n_snprintf.h>
#include <trurl/nhash.h>

extern int *vfile_verbose;
struct vfile_configuration {
    char       *cachedir;
    unsigned   flags;
    tn_hash    *default_clients_ht;
    tn_hash    *proxies_ht;
    int        *verbose;
    char       *anon_passwd;
    void       (*log)(unsigned flags, const char *fmt, ...);
    int        (*sigint_reached)(int reset);
};

extern struct vfile_configuration vfile_conf;
void vfile_set_errno(const char *ctxname, int vf_errno);
int vfile_sigint_reached(int reset);

#include "vfreq.h"


struct vf_module {
    char       vfmod_name[32];
    unsigned   vf_protocols;
    
    int        (*init)(void);
    void       (*destroy)(void);
    int        (*fetch)(struct vf_request *req);
    int        (*stat)(struct vf_request *req);
    int        _pri;            /* used by vfile only */
};

/* short alias for */
#define CL_URL(url) vf_url_hidepasswd_s(url)
#define PR_URL(url) vf_url_slim_s(url, 60)

int vf_decompressable(const char *path, char *uncmpr_path, int size);
int vf_extdecompress(const char *path, const char *destpath);
int vf_extcompress(const char *path, const char *ext);

#endif 
