/*
  Copyright (C) 2000 - 2008 Pawel A. Gajda <mis@pld-linux.org>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nhash.h>
#include <trurl/narray.h>
#include <trurl/nmalloc.h>

#include "i18n.h"
#include "compiler.h"

#include "vfile.h"
#include "vfile_intern.h"

#ifdef ENABLE_VFILE_CURL
extern struct vf_module vf_mod_curl;
#endif

extern struct vf_module vf_mod_vfff;

struct vf_module *vfmod_tab[] = {
    &vf_mod_vfff,
#ifdef ENABLE_VFILE_CURL
    &vf_mod_curl,
#endif
    NULL
};

#define REQTYPE_FETCH 0
#define REQTYPE_STAT  1

void vfile_setup(void)
{
    int n;

    n = 0;
    while (vfmod_tab[n] != NULL)
        vfmod_tab[n++]->init();
}

void vfile_destroy()
{
    int n = 0;

    while (vfmod_tab[n] != NULL)
	vfmod_tab[n++]->destroy();
}

static
const struct vf_module *find_vf_module(int reqtype, int urltype)
{
    int n = 0;

    n = 0;
    while (vfmod_tab[n] != NULL) {
        if (vfmod_tab[n]->vf_protocols & urltype) {
            int rc = 0;

            switch (reqtype) {
                case REQTYPE_FETCH:
                    rc = (vfmod_tab[n]->fetch != NULL);
                    break;

                case REQTYPE_STAT:
                    rc = (vfmod_tab[n]->stat != NULL);
                    break;

                default:
                    rc = 0;
                    n_assert(0);
                    break;
            }

            if (rc)
                return vfmod_tab[n];
        }
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

    if (!vfile_is_configured_ext_handler(path) ||
        !n_hash_exists(vfile_conf.default_clients_ht, proto)) {
        unsigned urltype = vf_url_type(path);
        mod = find_vf_module(REQTYPE_FETCH, urltype);
    }

    return mod;
}

static
int do_vfile_req(int reqtype, const struct vf_module *mod,
                 struct vf_request *req, unsigned vf_flags, const char *label)
{
    struct stat             st;
    int                     rc = 0;
    int                     end = 1, ntry = 0;

    n_assert(reqtype == REQTYPE_FETCH || reqtype == REQTYPE_STAT);

    if (reqtype == REQTYPE_FETCH) {
        n_assert(req->bar == NULL);
        if ((vf_flags & VF_FETCH_NOPROGRESS) == 0 && (vfile_conf.flags & VFILE_CONF_PROGRESS_NONE) == 0)
            req->bar = vf_progress_new(label ? label : req->url);
    }

    if (vfile_conf.flags & VFILE_CONF_STUBBORN_RETR)
        end = vfile_conf.nretries;

    while (end-- > 0) {
        if (vfile_sigint_reached(0)) {
            //vfile_set_errno(mod->vfmod_name, EINTR); /* FIXME */
            break;
        }

        if (ntry++ && (vfile_conf.flags & VFILE_CONF_STUBBORN_RETR)) {
            vf_loginfo(_("Retrying...(#%d)\n"), ntry);
            sleep(1);
        }

        req->req_errno = 0;
        vf_request_resetflags(req);

        switch (reqtype) {
            case REQTYPE_FETCH:
                if (req->bar)
                    vf_progress_reset(req->bar);
                rc = mod->fetch(req);
                break;

            case REQTYPE_STAT:
                rc = mod->stat(req);
                break;

            default:
                rc = 0;
                n_assert(0);
                break;
        }

        if (rc)
            break;

        switch (req->req_errno) {
            case ENOENT:
            case EINTR:
            case ENOSPC:
                goto l_endloop;
                break;
        }

        if (reqtype == REQTYPE_FETCH) {
            fsync(req->dest_fd);

            if (fstat(req->dest_fd, &st) != 0) {
                vf_logerr("fstat %s: %m\n", req->destpath);
                break;
            }
            req->dest_fdoff = st.st_size;
        }

        if (req->flags & VF_REQ_INT_REDIRECTED) {
            rc = 0;
            break;
        }
    }

 l_endloop:
    if (!rc && req->destpath)
        vf_unlink(req->destpath);

    if (req->bar) {
        vf_progress_free(req->bar);
        req->bar = NULL;
    }

    return rc;
}

static int make_url_label(char *buf, size_t size,
                          const char *counter, const char *label,
                          const char *url, int preserved_space)
{
    char *url_unescaped = vf_url_unescape(url);
    const char *purl = url_unescaped;

    if (!label && preserved_space > 0 && vfile_conf.term_width) {
        int w = vfile_conf.term_width() - preserved_space;
        if (w < 40)
            w = 40;

        purl = vf_url_slim_s(url_unescaped, w);
    }

    int n = n_snprintf(buf, size, "%s%s%s%s",
                       counter ? counter : "",
                       label ? label : "",
                       label ? "::" : "",
                       label ? n_basenam(url_unescaped) : purl);

    free(url_unescaped);
    return n;
}

int vfile__vf_fetch(const char *url, const char *dest_dir, unsigned flags,
                    const char *counter, const char *urlabel,
                    enum vf_fetchrc *ftrc)
{
    const struct vf_module  *mod = NULL;
    const char              *destdir = NULL;
    struct vflock           *vflock = NULL;
    struct vf_request       *req = NULL;
    char                    destpath[PATH_MAX];
    char                    url_label[PATH_MAX];
    int                     rc = 0;

    if (*vfile_verbose <= 0)
        flags |= VF_FETCH_NOLABEL|VF_FETCH_NOPROGRESS;

    *ftrc = VF_FETCHRC_NIL;
    if (dest_dir)
        destdir = dest_dir;

    else {
        char *p = alloca(PATH_MAX + 1);
        vf_localdirpath(p, PATH_MAX, url);
        destdir = p;
    }

    n_assert(destdir);

    const char *logfmt = _("Retrieving %s...\n");
    make_url_label(url_label, sizeof(url_label), counter, urlabel, url, strlen(logfmt));

    if ((mod = select_vf_module(url)) == NULL) { /* no internal module found */
        if ((flags & VF_FETCH_NOLABEL) == 0)
            vf_loginfo(logfmt, url_label);

        rc = vf_fetch_ext(url, destdir);
        goto l_end;
    }

    if ((vflock = vf_lock_mkdir(destdir)) == NULL)
        return 0;

    snprintf(destpath, sizeof(destpath), "%s/%s", destdir, n_basenam(url));
    if ((req = vf_request_new(url, destpath)) == NULL)
        goto l_end;

    if (req->proxy_url) {
        if ((mod = select_vf_module(req->proxy_url)) == NULL) {
            rc = vf_fetch_ext(url, destdir);
            vf_request_free(req);
            req = NULL;
            goto l_end;
        }
    }

    if (req->dest_fdoff > 0) { /* non-empty local file  */
        struct vf_stat vfst;

        if ((rc = vf_stat(req->url, destdir, &vfst, urlabel)) &&
            vfst.vf_size > 0 && vfst.vf_mtime > 0 &&
            vfst.vf_size  == vfst.vf_local_size &&
            vfst.vf_mtime == vfst.vf_local_mtime) {

            vf_request_free(req);
            req = NULL;
            *ftrc = VF_FETCHRC_UPTODATE;
            goto l_end;

        } else {
            if (*vfile_verbose > 1) {
                if (!rc || vfst.vf_size <= 0 || vfst.vf_mtime <= 0) {
                    vf_loginfo("vf_fetch: %s: remove local copy because of"
                               " uncomplete status reached\n",
                               n_basenam(req->url));
                } else {
                    vf_loginfo("vf_fetch: %s: remove uncomplete "
                               "local copy\n", n_basenam(req->url));
                }
            }

            vf_unlink(req->destpath);
            vf_request_close_destpath(req);
            vf_request_open_destpath(req);
        }
    }

    /* label displaying moved to vf_progress */
    if (*vfile_verbose > 2 && (flags & VF_FETCH_NOLABEL) == 0)
        vf_loginfo(logfmt, url_label);

    if ((rc = do_vfile_req(REQTYPE_FETCH, mod, req, flags, url_label)) == 0) {
        if ((req->flags & VF_REQ_INT_REDIRECTED) == 0) {
            vfile_set_errno(mod->vfmod_name, req->req_errno);

        } else {            /* redirected */
            char redir_url[PATH_MAX];

            snprintf(redir_url, sizeof(redir_url), "%s", req->url);
            vf_request_free(req);
            req = NULL;
            rc = vf_fetch(redir_url, destdir, flags, NULL, NULL);
        }
    }
    if (req)
        vf_request_free(req);

 l_end:
    if (vflock)
        vf_lock_release(vflock);

    if (rc && *ftrc == VF_FETCHRC_NIL)
        *ftrc = VF_FETCHRC_FETCHED;

    return rc;
}

int vf_fetch(const char *url, const char *dest_dir, unsigned flags,
             const char *counter, const char *urlabel)
{
    enum vf_fetchrc ftrc;
    return vfile__vf_fetch(url, dest_dir, flags, counter, urlabel, &ftrc);
}

int vf_stat(const char *url, const char *destdir, struct vf_stat *vfstat,
            const char *urlabel)
{
    const struct vf_module *mod = NULL;
    struct vf_request *req = NULL;
    unsigned urltype = 0, flags = 0;
    int rc = 0;

    if (*vfile_verbose <= 0)
        flags |= VF_FETCH_NOLABEL|VF_FETCH_NOPROGRESS;

    if ((req = vf_request_new(url, NULL)) == NULL)
        return 0;

    memset(vfstat, 0, sizeof(*vfstat));

    if (req->proxy_url)
        urltype = vf_url_type(req->proxy_url);
    else
        urltype = vf_url_type(req->url);

    if ((mod = find_vf_module(REQTYPE_STAT, urltype)) == NULL) {
        if (*vfile_verbose > 0)
            vf_log(VFILE_LOG_WARN, "%s could not find \"stat\" handler\n",
                   req->proto);
    } else {
        if ((flags & VF_FETCH_NOLABEL) == 0) {
            const char *logfmt = _("Retrieving status of %s...\n");
            const char *purl = NULL;
            int w = 40;

            if (vfile_conf.term_width) {
                w = vfile_conf.term_width() - strlen(logfmt);
                if (w < 40)
                    w = 40;
            }

            purl = vf_url_slim_s(req->url, w);
            vf_loginfo(logfmt, urlabel ? urlabel : purl);
        }

        if ((rc = do_vfile_req(REQTYPE_STAT, mod, req, flags, NULL))) {
            vfstat->vf_size = req->st_remote_size > 0 ? req->st_remote_size : 0;
            vfstat->vf_mtime = req->st_remote_mtime > 0 ? req->st_remote_mtime : 0;

        } else if (req->flags & VF_REQ_INT_REDIRECTED) {
            vf_request_free(req);
            req = NULL;
            rc = vf_stat(destdir, req->url, vfstat, NULL);

        } else {
            vfile_set_errno(mod->vfmod_name, req->req_errno);
        }
    }

    if (req)
        vf_request_free(req);

    if (rc) {
        char path[PATH_MAX];
        struct stat st;

        if (destdir)
            snprintf(path, sizeof(path), "%s/%s", destdir, n_basenam(url));
        else
            vf_localpath(path, sizeof(path), url);

        if (stat(path, &st) == 0) {
            vfstat->vf_local_size = st.st_size;
            vfstat->vf_local_mtime = st.st_mtime;
        }
    }
#if 0                           /* debug */
    printf("%ld, %ld    %ld, %ld\n",
           vfstat->vf_size, vfstat->vf_mtime,
           vfstat->vf_local_size, vfstat->vf_local_mtime);
    printf("%s\n", ctime(&vfstat->vf_mtime));
    printf("%s\n", ctime(&vfstat->vf_local_mtime));
#endif
    return rc;
}

int vf_fetcha(tn_array *urls, const char *destdir, unsigned flags,
              const char *urlabel, int begin, int max)
{
    const struct vf_module *mod = NULL;
    char counter[32];
    int rc = 1;

    if ((mod = select_vf_module(n_array_nth(urls, 0))) == NULL) {
        rc = vf_fetcha_ext(urls, destdir);

    } else {
        int i;

        for (i=0; i < n_array_size(urls); i++) {
            const char *url = n_array_nth(urls, i);
            snprintf(counter, sizeof(counter), "[%d/%d] ", begin + i + 1 , max);
            if (!vf_fetch(url, destdir, flags, max > 1 ? counter : NULL, urlabel)) {
                rc = 0;
                break;
            }
        }
    }

    return rc;
}
